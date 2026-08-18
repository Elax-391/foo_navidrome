#include "stdafx.h"

#include "ServerProfileConfig.h"

#include "ServerConnectionHub.h"
#include "MediaEnrichmentLogic.h"

#include <SDK/cfg_var.h>

#include <objbase.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>

#pragma comment(lib, "ole32.lib")

namespace {

constexpr GUID guid_cfg_server_profiles = {
    0xa1b2c3d4, 0x1111, 0x2222,
    {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e}};

navidrome::ServerProfileStatus configFailure(
        navidrome::ServerProfileError error, std::string detail) {
    navidrome::ServerProfileStatus status;
    status.error = error;
    status.detail = std::move(detail);
    return status;
}

navidrome::ServerProfileStatus toStatus(
        const navidrome::ServerProfileResult<navidrome::ServerProfileState>&
            result) {
    if (result) return {};
    return configFailure(result.error, result.detail);
}

} // namespace

namespace navidrome {

extern cfg_string cfg_server_url;
extern cfg_string cfg_username;
extern cfg_string cfg_password;

cfg_string cfg_server_profiles(guid_cfg_server_profiles, "");

ServerProfileConfig& ServerProfileConfig::get() {
    static ServerProfileConfig instance;
    return instance;
}

void ServerProfileConfig::initialize() {
    if (m_initialized) return;
    m_initialized = true;

    const std::string document = cfg_server_profiles.get().c_str();
    if (!document.empty()) {
        const auto decoded = deserializeServerProfileState(document);
        if (decoded) {
            const auto connection = projectActiveConnection(decoded.value);
            if (connection) {
                m_state = decoded.value;
                installRuntimePlan();
                mirrorLegacy(connection.value);
                if (document.compare(0, 7, "NDSP/1\n") == 0) {
                    const auto upgraded = serializeServerProfileState(m_state);
                    if (upgraded) cfg_server_profiles.set(upgraded.value.c_str());
                }
                return;
            }
            m_recovered = true;
            m_recoveryError = connection.error;
        } else {
            m_recovered = true;
            m_recoveryError = decoded.error;
        }
        // Preserve the malformed/unknown document for diagnosis. The legacy
        // variables remain the recovery source until a successful commit.
    }

    m_state = migrateLegacyServerProfile(cfg_server_url.get().c_str(),
                                         cfg_username.get().c_str(),
                                         cfg_password.get().c_str());
    const auto connection = projectActiveConnection(m_state);
    installRuntimePlan();
    if (connection) mirrorLegacy(connection.value);

    if (document.empty()) {
        const auto encoded = serializeServerProfileState(m_state);
        if (encoded) cfg_server_profiles.set(encoded.value.c_str());
    }
}

const ServerProfileState& ServerProfileConfig::state() const noexcept {
    return m_state;
}

bool ServerProfileConfig::recoveredFromInvalidDocument() const noexcept {
    return m_recovered;
}

ServerProfileError ServerProfileConfig::recoveryError() const noexcept {
    return m_recoveryError;
}

ServerProfileStatus ServerProfileConfig::commitState(
        const ServerProfileState& state) {
    try {
        ServerProfileState canonical = state;
        for (auto& profile : canonical.profiles)
            profile = canonicalizeServerProfile(std::move(profile));
        const auto encoded = serializeServerProfileState(canonical);
        if (!encoded)
            return configFailure(encoded.error, encoded.detail);
        const auto connection = projectActiveConnection(canonical);
        if (!connection)
            return configFailure(connection.error, connection.detail);

        const std::string previousUrl = cfg_server_url.get().c_str();
        const std::string previousUsername = cfg_username.get().c_str();
        const std::string previousPassword = cfg_password.get().c_str();
        const auto previousRuntime = m_runtime.snapshot();
        cfg_server_profiles.set(encoded.value.c_str());
        m_state = std::move(canonical);
        installRuntimePlan();
        mirrorLegacy(connection.value);
        m_recovered = false;
        m_recoveryError = ServerProfileError::None;
        if (previousRuntime.profileId != connection.value.profileId ||
            previousRuntime.effectiveRouteId != connection.value.routeId ||
            previousUrl != connection.value.serverUrl ||
            previousUsername != connection.value.username ||
            previousPassword != connection.value.password) {
            CoverCache::instance().clear();
            publishConnection(connection.value);
        }
        return {};
    } catch (const std::exception& error) {
        return configFailure(ServerProfileError::AllocationFailure,
                             error.what());
    } catch (...) {
        return configFailure(ServerProfileError::AllocationFailure,
                             "profile persistence failed");
    }
}

ServerProfileStatus ServerProfileConfig::selectProfile(
        const std::string& profileId) {
    const auto selected = selectServerProfile(m_state, profileId);
    if (!selected) return toStatus(selected);
    return commitState(selected.value);
}

ServerProfileStatus ServerProfileConfig::selectRoute(
        const std::string& profileId, ServerRoute route) {
    const auto selected = selectServerRoute(m_state, profileId, route);
    if (!selected) return toStatus(selected);
    return commitState(selected.value);
}

ServerProfileStatus ServerProfileConfig::selectRoute(
        const std::string& profileId, const std::string& routeId) {
    const auto selected = selectServerRoute(m_state, profileId, routeId);
    if (!selected) return toStatus(selected);
    return commitState(selected.value);
}

RoutePlanSnapshot ServerProfileConfig::runtimeSnapshot() const {
    return m_runtime.snapshot();
}

ServerRouteRuntime& ServerProfileConfig::runtime() noexcept {
    return m_runtime;
}

ServerProfileStatus ServerProfileConfig::applyRuntimeRoute(
        const std::string& routeId, std::uint64_t planRevision,
        const std::string& reason) {
    const auto snapshot = m_runtime.snapshot();
    if (snapshot.revision != planRevision ||
        snapshot.effectiveRouteId != routeId) {
        return configFailure(ServerProfileError::InvalidRoute,
                             "stale runtime route");
    }
    const auto connection = projectServerRouteConnection(
        m_state, snapshot.profileId, routeId);
    if (!connection)
        return configFailure(connection.error, connection.detail);

    const bool changed = cfg_server_url.get().c_str() !=
                             connection.value.serverUrl ||
                         cfg_username.get().c_str() !=
                             connection.value.username ||
                         cfg_password.get().c_str() !=
                             connection.value.password;
    mirrorLegacy(connection.value);
    if (changed) CoverCache::instance().clear();
    publishConnection(connection.value, reason, true);
    return {};
}

std::string ServerProfileConfig::generateProfileId() {
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid))) return {};
    char value[37] = {};
    const int written = std::snprintf(
        value, sizeof(value),
        "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        static_cast<unsigned long>(guid.Data1),
        static_cast<unsigned int>(guid.Data2),
        static_cast<unsigned int>(guid.Data3),
        static_cast<unsigned int>(guid.Data4[0]),
        static_cast<unsigned int>(guid.Data4[1]),
        static_cast<unsigned int>(guid.Data4[2]),
        static_cast<unsigned int>(guid.Data4[3]),
        static_cast<unsigned int>(guid.Data4[4]),
        static_cast<unsigned int>(guid.Data4[5]),
        static_cast<unsigned int>(guid.Data4[6]),
        static_cast<unsigned int>(guid.Data4[7]));
    return written == 36 ? std::string(value, 36) : std::string();
}

void ServerProfileConfig::mirrorLegacy(
        const ActiveConnection& connection) {
    cfg_server_url.set(connection.serverUrl.c_str());
    cfg_username.set(connection.username.c_str());
    cfg_password.set(connection.password.c_str());
}

bool ServerProfileConfig::sameConnection(
        const ActiveConnection& left,
        const ActiveConnection& right) noexcept {
    return left.profileId == right.profileId &&
           left.routeId == right.routeId &&
           left.serverUrl == right.serverUrl &&
           left.username == right.username && left.password == right.password;
}

void ServerProfileConfig::installRuntimePlan() {
    const auto profile = std::find_if(
        m_state.profiles.begin(), m_state.profiles.end(),
        [&](const ServerProfile& candidate) {
            return candidate.id == m_state.activeProfileId;
        });
    if (profile == m_state.profiles.end()) return;
    const ServerProfile canonical = canonicalizeServerProfile(*profile);
    RoutePlan plan;
    plan.revision = ++m_planRevision;
    plan.profileId = canonical.id;
    plan.preferredRouteId = canonical.preferredRouteId;
    plan.autoFailover = canonical.autoFailover;
    plan.candidates.reserve(canonical.routes.size());
    for (const auto& route : canonical.routes)
        plan.candidates.push_back({route.id, route.url});
    m_runtime.install(std::move(plan));
}

void ServerProfileConfig::publishConnection(
        const ActiveConnection& connection, std::string reason,
        bool automatic) {
    ServerConnectionEvent event;
    event.profileId = connection.profileId;
    event.routeId = connection.routeId;
    event.routeIndex = connection.routeIndex;
    event.route = connection.route;
    event.serverUrl = connection.serverUrl;
    event.username = connection.username;
    event.reason = std::move(reason);
    event.automatic = automatic;
    ServerConnectionHub::get().publish(std::move(event));
}

} // namespace navidrome
