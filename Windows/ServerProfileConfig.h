#pragma once

#include "ServerProfiles.h"
#include "ServerRouteRuntime.h"

#include <string>

namespace navidrome {

// Windows persistence adapter. The profile document is authoritative once it
// is valid; legacy cfg variables are maintained as the active-connection
// projection for the existing request, playback and scrobble paths.
class ServerProfileConfig {
public:
    static ServerProfileConfig& get();

    void initialize();
    const ServerProfileState& state() const noexcept;
    bool recoveredFromInvalidDocument() const noexcept;
    ServerProfileError recoveryError() const noexcept;

    ServerProfileStatus commitState(const ServerProfileState& state);
    ServerProfileStatus selectProfile(const std::string& profileId);
    ServerProfileStatus selectRoute(const std::string& profileId,
                                    ServerRoute route);
    ServerProfileStatus selectRoute(const std::string& profileId,
                                    const std::string& routeId);
    RoutePlanSnapshot runtimeSnapshot() const;
    ServerRouteRuntime& runtime() noexcept;
    ServerProfileStatus applyRuntimeRoute(
        const std::string& routeId, std::uint64_t planRevision,
        const std::string& reason);

    // Returns a lower-case GUID without braces, or an empty string if Windows
    // cannot generate one.
    static std::string generateProfileId();

private:
    ServerProfileConfig() = default;

    static void mirrorLegacy(const ActiveConnection& connection);
    static bool sameConnection(const ActiveConnection& left,
                               const ActiveConnection& right) noexcept;
    void installRuntimePlan();
    void publishConnection(const ActiveConnection& connection,
                           std::string reason = {},
                           bool automatic = false);

    ServerProfileState m_state;
    ServerRouteRuntime m_runtime;
    std::uint64_t m_planRevision = 0;
    bool m_initialized = false;
    bool m_recovered = false;
    ServerProfileError m_recoveryError = ServerProfileError::None;
};

} // namespace navidrome
