#include "ServerProfiles.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace {

using navidrome::ServerProfile;
using navidrome::ServerProfileError;
using navidrome::ServerProfileResult;
using navidrome::ServerProfileState;
using navidrome::ServerProfileStatus;
using navidrome::ServerRoute;
using navidrome::ServerRouteEntry;

constexpr char kHeaderV1[] = "NDSP/1\n";
constexpr char kHeaderV2[] = "NDSP/2\n";

ServerProfileStatus failure(ServerProfileError error, std::string detail) {
    ServerProfileStatus result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

template <typename T>
ServerProfileResult<T> resultFailure(ServerProfileError error,
                                     std::string detail) {
    ServerProfileResult<T> result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

template <typename T>
ServerProfileResult<T> resultSuccess(T value) {
    ServerProfileResult<T> result;
    result.value = std::move(value);
    return result;
}

bool validRoute(ServerRoute route) noexcept {
    return route == ServerRoute::Public || route == ServerRoute::Internal;
}

bool isAsciiWhitespace(unsigned char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\f' || ch == '\v';
}

std::string normalizedName(std::string_view name) {
    std::string result = navidrome::trimServerProfileName(name);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) {
                       return ch < 0x80
                           ? static_cast<char>(std::tolower(ch))
                           : static_cast<char>(ch);
                   });
    return result;
}

bool validUtf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto lead = static_cast<unsigned char>(value[offset]);
        if (lead <= 0x7f) {
            ++offset;
            continue;
        }

        std::size_t count = 0;
        unsigned int codePoint = 0;
        unsigned int minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            count = 2;
            codePoint = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            count = 3;
            codePoint = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            count = 4;
            codePoint = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (count > value.size() - offset) return false;
        for (std::size_t index = 1; index < count; ++index) {
            const auto continuation =
                static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (continuation & 0x3fU);
        }
        if (codePoint < minimum || codePoint > 0x10ffffU ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }
        offset += count;
    }
    return true;
}

ServerProfileStatus validateField(std::string_view value,
                                  const char* fieldName) {
    if (value.size() > navidrome::kMaxServerProfileFieldBytes)
        return failure(ServerProfileError::FieldTooLarge, fieldName);
    if (value.find('\0') != std::string_view::npos)
        return failure(ServerProfileError::EmbeddedNul, fieldName);
    if (!validUtf8(value))
        return failure(ServerProfileError::InvalidUtf8, fieldName);
    return {};
}

const ServerProfile* findProfile(const ServerProfileState& state,
                                 std::string_view id) noexcept {
    const auto it = std::find_if(
        state.profiles.begin(), state.profiles.end(),
        [&](const ServerProfile& profile) { return profile.id == id; });
    return it == state.profiles.end() ? nullptr : &*it;
}

ServerProfile* findProfile(ServerProfileState& state,
                           std::string_view id) noexcept {
    const auto it = std::find_if(
        state.profiles.begin(), state.profiles.end(),
        [&](const ServerProfile& profile) { return profile.id == id; });
    return it == state.profiles.end() ? nullptr : &*it;
}

std::string_view routeUrl(const ServerProfile& profile,
                          ServerRoute route) noexcept {
    if (route == ServerRoute::Public) return profile.publicUrl;
    if (route == ServerRoute::Internal) return profile.internalUrl;
    return {};
}

const ServerRouteEntry* findRoute(const ServerProfile& profile,
                                  std::string_view id) noexcept {
    const auto it = std::find_if(
        profile.routes.begin(), profile.routes.end(),
        [&](const ServerRouteEntry& route) { return route.id == id; });
    return it == profile.routes.end() ? nullptr : &*it;
}

ServerRouteEntry* findRoute(ServerProfile& profile,
                            std::string_view id) noexcept {
    const auto it = std::find_if(
        profile.routes.begin(), profile.routes.end(),
        [&](const ServerRouteEntry& route) { return route.id == id; });
    return it == profile.routes.end() ? nullptr : &*it;
}

ServerProfile canonicalizeProfile(ServerProfile profile) {
    if (profile.routes.empty()) {
        profile.routes.push_back({"route-1", profile.publicUrl});
        if (!profile.internalUrl.empty())
            profile.routes.push_back({"route-2", profile.internalUrl});
        profile.preferredRouteId =
            profile.selectedRoute == ServerRoute::Internal &&
                    profile.routes.size() > 1
                ? profile.routes[1].id
                : profile.routes[0].id;
    } else if (profile.preferredRouteId.empty()) {
        profile.preferredRouteId = profile.routes.front().id;
    }

    profile.publicUrl = profile.routes.empty() ? std::string()
                                                : profile.routes[0].url;
    profile.internalUrl = profile.routes.size() > 1
        ? profile.routes[1].url : std::string();
    const auto preferred = std::find_if(
        profile.routes.begin(), profile.routes.end(),
        [&](const ServerRouteEntry& route) {
            return route.id == profile.preferredRouteId;
        });
    profile.selectedRoute = preferred != profile.routes.end() &&
                                    preferred != profile.routes.begin()
        ? ServerRoute::Internal : ServerRoute::Public;
    return profile;
}

ServerProfileStatus validateSelectable(const ServerProfile& profile) {
    const ServerProfile canonical = canonicalizeProfile(profile);
    const auto* route = findRoute(canonical, canonical.preferredRouteId);
    if (!route)
        return failure(ServerProfileError::MissingPreferredRoute, profile.id);
    if (route->url.empty())
        return failure(ServerProfileError::MissingRouteUrl, profile.id);
    return {};
}

void appendNumber(std::string& output, std::size_t value) {
    output += std::to_string(value);
    output.push_back('\n');
}

void appendField(std::string& output, std::string_view value) {
    appendNumber(output, value.size());
    output.append(value.data(), value.size());
    output.push_back('\n');
}

class Reader {
public:
    explicit Reader(std::string_view input) noexcept : m_input(input) {}

    bool number(std::size_t maximum, std::size_t& value,
                ServerProfileError& error) noexcept {
        const std::size_t start = m_offset;
        std::size_t digits = 0;
        std::size_t parsed = 0;
        while (m_offset < m_input.size() && m_input[m_offset] != '\n') {
            const unsigned char ch =
                static_cast<unsigned char>(m_input[m_offset]);
            if (ch < '0' || ch > '9' || digits >= 10) {
                error = ServerProfileError::InvalidLength;
                return false;
            }
            const std::size_t digit = static_cast<std::size_t>(ch - '0');
            if (digit > maximum || parsed > (maximum - digit) / 10) {
                error = ServerProfileError::InvalidLength;
                return false;
            }
            parsed = parsed * 10 + digit;
            ++m_offset;
            ++digits;
        }
        if (m_offset >= m_input.size()) {
            error = ServerProfileError::Truncated;
            return false;
        }
        if (digits == 0 || (digits > 1 && m_input[start] == '0')) {
            error = ServerProfileError::InvalidLength;
            return false;
        }
        ++m_offset;
        value = parsed;
        return true;
    }

    bool field(std::string& value, ServerProfileError& error) {
        std::size_t length = 0;
        if (!number(navidrome::kMaxServerProfileFieldBytes, length, error))
            return false;
        if (length > m_input.size() - m_offset) {
            error = ServerProfileError::Truncated;
            return false;
        }
        value.assign(m_input.data() + m_offset, length);
        m_offset += length;
        if (m_offset >= m_input.size()) {
            error = ServerProfileError::Truncated;
            return false;
        }
        if (m_input[m_offset] != '\n') {
            error = ServerProfileError::InvalidLength;
            return false;
        }
        ++m_offset;
        return true;
    }

    bool done() const noexcept { return m_offset == m_input.size(); }

private:
    std::string_view m_input;
    std::size_t m_offset = 0;
};

ServerProfileResult<ServerProfileState> validatedStateResult(
    ServerProfileState state) {
    const auto status = navidrome::validateServerProfileState(state);
    if (!status)
        return resultFailure<ServerProfileState>(status.error, status.detail);
    return resultSuccess(std::move(state));
}

} // namespace

std::string navidrome::trimServerProfileName(std::string_view name) {
    std::size_t start = 0;
    while (start < name.size() &&
           isAsciiWhitespace(static_cast<unsigned char>(name[start]))) {
        ++start;
    }
    std::size_t end = name.size();
    while (end > start &&
           isAsciiWhitespace(static_cast<unsigned char>(name[end - 1]))) {
        --end;
    }
    return std::string(name.substr(start, end - start));
}

ServerProfile navidrome::canonicalizeServerProfile(ServerProfile profile) {
    return canonicalizeProfile(std::move(profile));
}

ServerProfileStatus navidrome::validateServerProfileState(
    const ServerProfileState& state) {
    if (state.profiles.empty())
        return failure(ServerProfileError::EmptyProfiles, "profiles");
    if (state.profiles.size() > kMaxServerProfiles)
        return failure(ServerProfileError::TooManyProfiles, "profiles");

    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> names;
    ids.reserve(state.profiles.size());
    names.reserve(state.profiles.size());
    for (const auto& profile : state.profiles) {
        if (profile.routes.empty() && !validRoute(profile.selectedRoute))
            return failure(ServerProfileError::InvalidRoute, profile.id);
        const ServerProfile canonical = canonicalizeProfile(profile);
        const std::pair<std::string_view, const char*> fields[] = {
            {canonical.id, "id"},
            {canonical.name, "name"},
            {canonical.preferredRouteId, "preferredRouteId"},
            {canonical.username, "username"},
            {canonical.password, "password"},
        };
        for (const auto& field : fields) {
            const auto status = validateField(field.first, field.second);
            if (!status) return status;
        }
        if (canonical.id.empty())
            return failure(ServerProfileError::EmptyId, "id");
        if (!ids.insert(canonical.id).second)
            return failure(ServerProfileError::DuplicateId, canonical.id);

        const std::string trimmed = trimServerProfileName(canonical.name);
        if (trimmed.empty())
            return failure(ServerProfileError::EmptyName, canonical.id);
        if (trimmed != canonical.name)
            return failure(ServerProfileError::UntrimmedName, canonical.id);
        if (!names.insert(normalizedName(canonical.name)).second)
            return failure(ServerProfileError::DuplicateName, canonical.name);

        if (canonical.routes.empty())
            return failure(ServerProfileError::EmptyRoutes, canonical.id);
        if (canonical.routes.size() > kMaxServerRoutesPerProfile)
            return failure(ServerProfileError::TooManyRoutes, canonical.id);
        std::unordered_set<std::string> routeIds;
        routeIds.reserve(canonical.routes.size());
        for (const auto& route : canonical.routes) {
            const auto idStatus = validateField(route.id, "routeId");
            if (!idStatus) return idStatus;
            const auto urlStatus = validateField(route.url, "routeUrl");
            if (!urlStatus) return urlStatus;
            if (route.id.empty())
                return failure(ServerProfileError::EmptyRouteId,
                               canonical.id);
            if (!routeIds.insert(route.id).second)
                return failure(ServerProfileError::DuplicateRouteId,
                               route.id);
        }
        if (!findRoute(canonical, canonical.preferredRouteId))
            return failure(ServerProfileError::MissingPreferredRoute,
                           canonical.id);
    }

    const auto activeStatus = validateField(state.activeProfileId,
                                            "activeProfileId");
    if (!activeStatus) return activeStatus;
    if (!findProfile(state, state.activeProfileId))
        return failure(ServerProfileError::MissingActiveProfile,
                       state.activeProfileId);
    return {};
}

ServerProfileResult<std::string> navidrome::serializeServerProfileState(
    const ServerProfileState& state) {
    try {
        ServerProfileState canonical = state;
        for (auto& profile : canonical.profiles)
            profile = canonicalizeProfile(std::move(profile));
        const auto status = validateServerProfileState(canonical);
        if (!status)
            return resultFailure<std::string>(status.error, status.detail);

        std::string output(kHeaderV2);
        appendField(output, canonical.activeProfileId);
        appendNumber(output, canonical.profiles.size());
        for (const auto& profile : canonical.profiles) {
            appendField(output, profile.id);
            appendField(output, profile.name);
            appendField(output, profile.username);
            appendField(output, profile.password);
            appendNumber(output, profile.autoFailover ? 1U : 0U);
            appendField(output, profile.preferredRouteId);
            appendNumber(output, profile.routes.size());
            for (const auto& route : profile.routes) {
                appendField(output, route.id);
                appendField(output, route.url);
            }
            if (output.size() > kMaxServerProfileDocumentBytes)
                return resultFailure<std::string>(
                    ServerProfileError::DocumentTooLarge, "document");
        }
        return resultSuccess(std::move(output));
    } catch (const std::bad_alloc&) {
        return resultFailure<std::string>(ServerProfileError::AllocationFailure,
                                          "document");
    }
}

ServerProfileResult<ServerProfileState>
navidrome::deserializeServerProfileState(std::string_view document) {
    try {
        if (document.size() > kMaxServerProfileDocumentBytes)
            return resultFailure<ServerProfileState>(
                ServerProfileError::DocumentTooLarge, "document");
        const std::string_view headerV1(kHeaderV1, sizeof(kHeaderV1) - 1);
        const std::string_view headerV2(kHeaderV2, sizeof(kHeaderV2) - 1);
        const bool version1 = document.size() >= headerV1.size() &&
            document.substr(0, headerV1.size()) == headerV1;
        const bool version2 = document.size() >= headerV2.size() &&
            document.substr(0, headerV2.size()) == headerV2;
        if (!version1 && !version2) {
            if (document.size() >= 5 && document.substr(0, 5) == "NDSP/")
                return resultFailure<ServerProfileState>(
                    ServerProfileError::UnknownVersion, "header");
            return resultFailure<ServerProfileState>(
                ServerProfileError::InvalidHeader, "header");
        }

        Reader reader(document.substr(version1 ? headerV1.size()
                                               : headerV2.size()));
        ServerProfileState state;
        ServerProfileError parseError = ServerProfileError::None;
        if (!reader.field(state.activeProfileId, parseError))
            return resultFailure<ServerProfileState>(parseError,
                                                     "activeProfileId");

        std::size_t count = 0;
        if (!reader.number(kMaxServerProfiles, count, parseError))
            return resultFailure<ServerProfileState>(parseError, "profiles");
        if (count == 0)
            return resultFailure<ServerProfileState>(
                ServerProfileError::EmptyProfiles, "profiles");
        state.profiles.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            ServerProfile profile;
            if (version1) {
                if (!reader.field(profile.id, parseError) ||
                    !reader.field(profile.name, parseError) ||
                    !reader.field(profile.publicUrl, parseError) ||
                    !reader.field(profile.internalUrl, parseError) ||
                    !reader.field(profile.username, parseError) ||
                    !reader.field(profile.password, parseError)) {
                    return resultFailure<ServerProfileState>(parseError,
                                                             "profile field");
                }
                std::size_t route = 0;
                if (!reader.number(1, route, parseError))
                    return resultFailure<ServerProfileState>(parseError,
                                                             "route");
                profile.selectedRoute = route == 0 ? ServerRoute::Public
                                                   : ServerRoute::Internal;
                profile.autoFailover = true;
                state.profiles.push_back(
                    canonicalizeProfile(std::move(profile)));
                continue;
            }

            if (!reader.field(profile.id, parseError) ||
                !reader.field(profile.name, parseError) ||
                !reader.field(profile.username, parseError) ||
                !reader.field(profile.password, parseError)) {
                return resultFailure<ServerProfileState>(parseError,
                                                         "profile field");
            }
            std::size_t autoFailover = 0;
            if (!reader.number(1, autoFailover, parseError) ||
                !reader.field(profile.preferredRouteId, parseError)) {
                return resultFailure<ServerProfileState>(parseError,
                                                         "profile route policy");
            }
            profile.autoFailover = autoFailover != 0;
            std::size_t routeCount = 0;
            if (!reader.number(kMaxServerRoutesPerProfile, routeCount,
                               parseError)) {
                return resultFailure<ServerProfileState>(parseError,
                                                         "routes");
            }
            if (routeCount == 0)
                return resultFailure<ServerProfileState>(
                    ServerProfileError::EmptyRoutes, profile.id);
            profile.routes.reserve(routeCount);
            for (std::size_t routeIndex = 0; routeIndex < routeCount;
                 ++routeIndex) {
                ServerRouteEntry route;
                if (!reader.field(route.id, parseError) ||
                    !reader.field(route.url, parseError)) {
                    return resultFailure<ServerProfileState>(parseError,
                                                             "route field");
                }
                profile.routes.push_back(std::move(route));
            }
            state.profiles.push_back(canonicalizeProfile(std::move(profile)));
        }
        if (!reader.done())
            return resultFailure<ServerProfileState>(
                ServerProfileError::TrailingData, "document");
        return validatedStateResult(std::move(state));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "document");
    }
}

ServerProfileState navidrome::migrateLegacyServerProfile(
    std::string serverUrl, std::string username, std::string password) {
    ServerProfile profile;
    profile.id = "legacy-default";
    profile.name = u8"默认服务器";
    profile.publicUrl = std::move(serverUrl);
    profile.username = std::move(username);
    profile.password = std::move(password);
    profile.selectedRoute = ServerRoute::Public;
    profile.autoFailover = true;
    profile = canonicalizeProfile(std::move(profile));

    ServerProfileState state;
    state.activeProfileId = profile.id;
    state.profiles.push_back(std::move(profile));
    return state;
}

ServerProfileResult<navidrome::ActiveConnection>
navidrome::projectActiveConnection(const ServerProfileState& state) {
    const auto status = validateServerProfileState(state);
    if (!status)
        return resultFailure<ActiveConnection>(status.error, status.detail);
    const auto* profile = findProfile(state, state.activeProfileId);
    const ServerProfile canonical = canonicalizeProfile(*profile);
    const auto selectable = validateSelectable(canonical);
    if (!selectable)
        return resultFailure<ActiveConnection>(selectable.error,
                                               selectable.detail);
    const auto preferred = std::find_if(
        canonical.routes.begin(), canonical.routes.end(),
        [&](const ServerRouteEntry& route) {
            return route.id == canonical.preferredRouteId;
        });
    if (preferred == canonical.routes.end())
        return resultFailure<ActiveConnection>(
            ServerProfileError::MissingPreferredRoute, canonical.id);
    ActiveConnection connection;
    connection.profileId = canonical.id;
    connection.routeId = preferred->id;
    connection.routeIndex = static_cast<std::size_t>(
        std::distance(canonical.routes.begin(), preferred));
    connection.route = connection.routeIndex == 1
        ? ServerRoute::Internal : ServerRoute::Public;
    connection.serverUrl = preferred->url;
    connection.username = canonical.username;
    connection.password = canonical.password;
    return resultSuccess(std::move(connection));
}

ServerProfileResult<navidrome::ActiveConnection>
navidrome::projectServerRouteConnection(const ServerProfileState& state,
                                        std::string_view profileId,
                                        std::string_view routeId) {
    const auto status = validateServerProfileState(state);
    if (!status)
        return resultFailure<ActiveConnection>(status.error, status.detail);
    const auto* stored = findProfile(state, profileId);
    if (!stored)
        return resultFailure<ActiveConnection>(
            ServerProfileError::ProfileNotFound, std::string(profileId));
    const ServerProfile profile = canonicalizeProfile(*stored);
    const auto route = std::find_if(
        profile.routes.begin(), profile.routes.end(),
        [&](const ServerRouteEntry& candidate) {
            return candidate.id == routeId;
        });
    if (route == profile.routes.end())
        return resultFailure<ActiveConnection>(
            ServerProfileError::RouteNotFound, std::string(routeId));
    if (route->url.empty())
        return resultFailure<ActiveConnection>(
            ServerProfileError::MissingRouteUrl, std::string(routeId));

    ActiveConnection connection;
    connection.profileId = profile.id;
    connection.routeId = route->id;
    connection.routeIndex = static_cast<std::size_t>(
        std::distance(profile.routes.begin(), route));
    connection.route = connection.routeIndex == 1
        ? ServerRoute::Internal : ServerRoute::Public;
    connection.serverUrl = route->url;
    connection.username = profile.username;
    connection.password = profile.password;
    return resultSuccess(std::move(connection));
}

ServerProfileResult<ServerProfileState> navidrome::addServerProfile(
    const ServerProfileState& state, ServerProfile profile) {
    profile.name = trimServerProfileName(profile.name);
    profile = canonicalizeProfile(std::move(profile));
    if (profile.username.empty() || profile.password.empty())
        return resultFailure<ServerProfileState>(
            ServerProfileError::MissingCredentials, profile.id);
    const auto selectable = validateSelectable(profile);
    if (!selectable)
        return resultFailure<ServerProfileState>(selectable.error,
                                                 selectable.detail);
    try {
        ServerProfileState next = state;
        next.profiles.push_back(std::move(profile));
        next.activeProfileId = next.profiles.back().id;
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::updateServerProfile(
    const ServerProfileState& state, ServerProfile profile) {
    profile.name = trimServerProfileName(profile.name);
    profile = canonicalizeProfile(std::move(profile));
    try {
        ServerProfileState next = state;
        auto* target = findProfile(next, profile.id);
        if (!target)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, profile.id);
        const bool updatingActive = next.activeProfileId == target->id;
        *target = std::move(profile);
        const auto validation = validatedStateResult(std::move(next));
        if (!validation) return validation;
        if (updatingActive) {
            const auto projected = projectActiveConnection(validation.value);
            if (!projected)
                return resultFailure<ServerProfileState>(projected.error,
                                                         projected.detail);
        }
        return validation;
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::renameServerProfile(
    const ServerProfileState& state, std::string_view profileId,
    std::string name) {
    try {
        ServerProfileState next = state;
        auto* profile = findProfile(next, profileId);
        if (!profile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        profile->name = trimServerProfileName(name);
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::deleteServerProfile(
    const ServerProfileState& state, std::string_view profileId) {
    if (state.profiles.size() <= 1)
        return resultFailure<ServerProfileState>(
            ServerProfileError::CannotDeleteLastProfile,
            std::string(profileId));
    try {
        ServerProfileState next = state;
        const auto it = std::find_if(
            next.profiles.begin(), next.profiles.end(),
            [&](const ServerProfile& profile) { return profile.id == profileId; });
        if (it == next.profiles.end())
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        const std::size_t index =
            static_cast<std::size_t>(std::distance(next.profiles.begin(), it));
        const bool deletingActive = next.activeProfileId == profileId;
        next.profiles.erase(it);
        if (deletingActive) {
            const std::size_t fallback =
                std::min(index, next.profiles.size() - 1);
            next.activeProfileId = next.profiles[fallback].id;
            const auto selectable = validateSelectable(next.profiles[fallback]);
            if (!selectable)
                return resultFailure<ServerProfileState>(selectable.error,
                                                         selectable.detail);
        }
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::selectServerProfile(
    const ServerProfileState& state, std::string_view profileId) {
    const auto status = validateServerProfileState(state);
    if (!status)
        return resultFailure<ServerProfileState>(status.error, status.detail);
    const auto* profile = findProfile(state, profileId);
    if (!profile)
        return resultFailure<ServerProfileState>(
            ServerProfileError::ProfileNotFound, std::string(profileId));
    const auto selectable = validateSelectable(*profile);
    if (!selectable)
        return resultFailure<ServerProfileState>(selectable.error,
                                                 selectable.detail);
    try {
        ServerProfileState next = state;
        next.activeProfileId.assign(profileId.data(), profileId.size());
        return resultSuccess(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::selectServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    ServerRoute route) {
    if (!validRoute(route))
        return resultFailure<ServerProfileState>(
            ServerProfileError::InvalidRoute, std::string(profileId));
    const auto* profile = findProfile(state, profileId);
    if (!profile)
        return resultFailure<ServerProfileState>(
            ServerProfileError::ProfileNotFound, std::string(profileId));
    const ServerProfile canonical = canonicalizeProfile(*profile);
    const std::size_t index = route == ServerRoute::Internal ? 1 : 0;
    if (index >= canonical.routes.size())
        return resultFailure<ServerProfileState>(
            ServerProfileError::MissingRouteUrl, std::string(profileId));
    return selectServerRoute(state, profileId, canonical.routes[index].id);
}

ServerProfileResult<ServerProfileState> navidrome::selectServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId) {
    try {
        ServerProfileState next = state;
        auto* profile = findProfile(next, profileId);
        if (!profile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        *profile = canonicalizeProfile(std::move(*profile));
        auto* selected = findRoute(*profile, routeId);
        if (!selected)
            return resultFailure<ServerProfileState>(
                ServerProfileError::RouteNotFound, std::string(routeId));
        if (selected->url.empty())
            return resultFailure<ServerProfileState>(
                ServerProfileError::MissingRouteUrl, std::string(profileId));
        profile->preferredRouteId = selected->id;
        const std::size_t selectedIndex = static_cast<std::size_t>(
            selected - profile->routes.data());
        profile->selectedRoute = selectedIndex == 1
            ? ServerRoute::Internal : ServerRoute::Public;
        next.activeProfileId.assign(profileId.data(), profileId.size());
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "profiles");
    }
}

ServerProfileResult<ServerProfileState> navidrome::addServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    ServerRouteEntry route) {
    try {
        ServerProfileState next = state;
        auto* profile = findProfile(next, profileId);
        if (!profile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        *profile = canonicalizeProfile(std::move(*profile));
        if (profile->routes.size() >= kMaxServerRoutesPerProfile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::TooManyRoutes, std::string(profileId));
        profile->routes.push_back(std::move(route));
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "routes");
    }
}

ServerProfileResult<ServerProfileState> navidrome::deleteServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId) {
    try {
        ServerProfileState next = state;
        auto* profile = findProfile(next, profileId);
        if (!profile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        *profile = canonicalizeProfile(std::move(*profile));
        if (profile->routes.size() <= 1)
            return resultFailure<ServerProfileState>(
                ServerProfileError::CannotDeleteLastRoute,
                std::string(routeId));
        const auto it = std::find_if(
            profile->routes.begin(), profile->routes.end(),
            [&](const ServerRouteEntry& route) { return route.id == routeId; });
        if (it == profile->routes.end())
            return resultFailure<ServerProfileState>(
                ServerProfileError::RouteNotFound, std::string(routeId));
        const bool deletingPreferred = profile->preferredRouteId == routeId;
        const std::size_t index = static_cast<std::size_t>(
            std::distance(profile->routes.begin(), it));
        profile->routes.erase(it);
        if (deletingPreferred) {
            const std::size_t fallback =
                std::min(index, profile->routes.size() - 1);
            profile->preferredRouteId = profile->routes[fallback].id;
            profile->selectedRoute = fallback == 1
                ? ServerRoute::Internal : ServerRoute::Public;
        }
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "routes");
    }
}

ServerProfileResult<ServerProfileState> navidrome::moveServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId, int direction) {
    if (direction != -1 && direction != 1)
        return resultFailure<ServerProfileState>(
            ServerProfileError::InvalidRoute, std::string(routeId));
    try {
        ServerProfileState next = state;
        auto* profile = findProfile(next, profileId);
        if (!profile)
            return resultFailure<ServerProfileState>(
                ServerProfileError::ProfileNotFound, std::string(profileId));
        *profile = canonicalizeProfile(std::move(*profile));
        const auto it = std::find_if(
            profile->routes.begin(), profile->routes.end(),
            [&](const ServerRouteEntry& route) { return route.id == routeId; });
        if (it == profile->routes.end())
            return resultFailure<ServerProfileState>(
                ServerProfileError::RouteNotFound, std::string(routeId));
        const auto index = static_cast<std::ptrdiff_t>(
            std::distance(profile->routes.begin(), it));
        const auto target = index + direction;
        if (target < 0 || target >= static_cast<std::ptrdiff_t>(
                                  profile->routes.size()))
            return validatedStateResult(std::move(next));
        std::iter_swap(profile->routes.begin() + index,
                       profile->routes.begin() + target);
        return validatedStateResult(std::move(next));
    } catch (const std::bad_alloc&) {
        return resultFailure<ServerProfileState>(
            ServerProfileError::AllocationFailure, "routes");
    }
}
