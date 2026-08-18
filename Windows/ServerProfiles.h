#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace navidrome {

constexpr std::size_t kMaxServerProfiles = 64;
constexpr std::size_t kMaxServerRoutesPerProfile = 8;
constexpr std::size_t kMaxServerProfileFieldBytes = 64 * 1024;
constexpr std::size_t kMaxServerProfileDocumentBytes = 1024 * 1024;

enum class ServerRoute {
    Public = 0,
    Internal = 1,
};

struct ServerRouteEntry {
    std::string id;
    std::string url;
};

inline bool operator==(const ServerRouteEntry& left,
                       const ServerRouteEntry& right) noexcept {
    return left.id == right.id && left.url == right.url;
}

struct ServerProfile {
    std::string id;
    std::string name;
    std::vector<ServerRouteEntry> routes;
    std::string preferredRouteId;
    bool autoFailover = true;
    std::string publicUrl;
    std::string internalUrl;
    std::string username;
    std::string password;
    ServerRoute selectedRoute = ServerRoute::Public;
};

struct ServerProfileState {
    std::vector<ServerProfile> profiles;
    std::string activeProfileId;
};

struct ActiveConnection {
    std::string profileId;
    std::string routeId;
    std::size_t routeIndex = 0;
    ServerRoute route = ServerRoute::Public;
    std::string serverUrl;
    std::string username;
    std::string password;
};

enum class ServerProfileError {
    None,
    EmptyProfiles,
    TooManyProfiles,
    EmptyRoutes,
    TooManyRoutes,
    EmptyRouteId,
    DuplicateRouteId,
    MissingPreferredRoute,
    EmptyId,
    DuplicateId,
    EmptyName,
    UntrimmedName,
    DuplicateName,
    MissingActiveProfile,
    InvalidRoute,
    MissingRouteUrl,
    MissingCredentials,
    FieldTooLarge,
    DocumentTooLarge,
    EmbeddedNul,
    InvalidUtf8,
    InvalidHeader,
    UnknownVersion,
    InvalidLength,
    Truncated,
    TrailingData,
    CannotDeleteLastProfile,
    CannotDeleteLastRoute,
    ProfileNotFound,
    RouteNotFound,
    AllocationFailure,
};

struct ServerProfileStatus {
    ServerProfileError error = ServerProfileError::None;
    std::string detail;

    explicit operator bool() const noexcept {
        return error == ServerProfileError::None;
    }
};

template <typename T>
struct ServerProfileResult {
    T value{};
    ServerProfileError error = ServerProfileError::None;
    std::string detail;

    explicit operator bool() const noexcept {
        return error == ServerProfileError::None;
    }
};

// Names are trimmed using ASCII whitespace and compared case-insensitively for
// ASCII characters. Non-ASCII UTF-8 bytes are preserved exactly.
std::string trimServerProfileName(std::string_view name);

// Structural validation permits blank credentials and a blank inactive route
// so legacy settings can always be represented. Connection projection and
// route/profile selection apply the stricter active-route requirements.
ServerProfileStatus validateServerProfileState(const ServerProfileState& state);

ServerProfileResult<std::string> serializeServerProfileState(
    const ServerProfileState& state);
ServerProfileResult<ServerProfileState> deserializeServerProfileState(
    std::string_view document);

ServerProfileState migrateLegacyServerProfile(std::string serverUrl,
                                              std::string username,
                                              std::string password);

ServerProfileResult<ActiveConnection> projectActiveConnection(
    const ServerProfileState& state);
ServerProfileResult<ActiveConnection> projectServerRouteConnection(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId);
ServerProfileResult<ServerProfileState> addServerProfile(
    const ServerProfileState& state, ServerProfile profile);
ServerProfileResult<ServerProfileState> updateServerProfile(
    const ServerProfileState& state, ServerProfile profile);
ServerProfileResult<ServerProfileState> renameServerProfile(
    const ServerProfileState& state, std::string_view profileId,
    std::string name);
ServerProfileResult<ServerProfileState> deleteServerProfile(
    const ServerProfileState& state, std::string_view profileId);
ServerProfileResult<ServerProfileState> selectServerProfile(
    const ServerProfileState& state, std::string_view profileId);
ServerProfileResult<ServerProfileState> selectServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    ServerRoute route);
ServerProfileResult<ServerProfileState> selectServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId);
ServerProfileResult<ServerProfileState> addServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    ServerRouteEntry route);
ServerProfileResult<ServerProfileState> deleteServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId);
ServerProfileResult<ServerProfileState> moveServerRoute(
    const ServerProfileState& state, std::string_view profileId,
    std::string_view routeId, int direction);

// Converts the fixed NDSP/1 public/internal fields to the route-list model.
// It is also used for legacy in-memory callers while the old enum API remains
// available as a compatibility shim.
ServerProfile canonicalizeServerProfile(ServerProfile profile);

} // namespace navidrome
