#pragma once

#include "../SubsonicTypes.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace navidrome {

enum class BrowserNodeKind {
    NavigationGroup,
    Artist,
    Album,
    Song,
    Genre,
    SmartList,
    ServerPlaylist,
    Loading,
    Error,
};

enum class NavigationGroupKind {
    SmartLists,
    ServerPlaylists,
    Genres,
    Artists,
};

enum class SmartListKind {
    StarredSongs,
    StarredAlbums,
    StarredArtists,
    NewestAlbums,
    FrequentAlbums,
    RecentAlbums,
    RandomAlbums,
};

struct GroupedNavigationDefaults {
    std::vector<NavigationGroupKind> groups;
    std::vector<SmartListKind> smartLists;
};

GroupedNavigationDefaults groupedNavigationDefaults();
SmartListKind favoriteSmartListKind(FavoriteKind kind) noexcept;

bool isBrowserContainer(BrowserNodeKind kind) noexcept;
bool isBrowserPlayable(BrowserNodeKind kind) noexcept;
std::string formatSongLabel(const Song& song, const std::string& fallbackTitle);
std::string formatPlaylistLabel(const ServerPlaylist& playlist);
std::string downloadFileName(const Song& song);

enum class EnqueueDisposition {
    Append,
    ReplaceActive,
};

struct PlaylistUriMapping {
    std::vector<std::string> orderedSongIds;
    std::size_t skippedEmpty = 0;
    std::size_t skippedUnsupported = 0;
    std::size_t skippedMalformed = 0;

    std::size_t skippedCount() const noexcept {
        return skippedEmpty + skippedUnsupported + skippedMalformed;
    }
};

PlaylistUriMapping mapActivePlaylistUris(const std::vector<std::string>& orderedPaths);
std::vector<ServerPlaylist> exactNameMatches(
    const std::vector<ServerPlaylist>& playlists, const std::string& desiredName);
std::string firstAvailableNumberedName(
    const std::string& baseName, const std::vector<ServerPlaylist>& playlists);

enum class UploadChoice {
    Create,
    Replace,
    NumberedCopy,
    Cancel,
};

enum class UploadPlanStatus {
    Ready,
    Cancelled,
    Invalid,
};

struct BrowserUploadPlan {
    UploadPlanStatus status = UploadPlanStatus::Invalid;
    std::optional<std::string> targetPlaylistId;
    std::string targetName;
    std::vector<std::string> orderedSongIds;
    std::size_t sourceItemCount = 0;
    std::size_t skippedCount = 0;
};

BrowserUploadPlan makeUploadPlan(
    const std::string& activePlaylistName, const PlaylistUriMapping& mapping,
    UploadChoice choice, const std::optional<std::string>& selectedPlaylistId,
    const std::vector<ServerPlaylist>& catalog, std::size_t sourceItemCount);

enum class BrowserWriteOutcome {
    Complete,
    Partial,
    Restored,
    Unknown,
    FailedNoChange,
};

struct WriteEvidence {
    bool mutationAttempted = false;
    PlaylistWriteState transportState = PlaylistWriteState::Failed;
    std::vector<std::string> requestedIds;
    std::optional<std::vector<std::string>> verifiedCurrentIds;
    std::optional<std::vector<std::string>> originalIds;
    bool restorationAttempted = false;
    std::optional<std::vector<std::string>> verifiedRestoredIds;
};

BrowserWriteOutcome classifyWriteOutcome(const WriteEvidence& evidence);
bool shouldRefreshPlaylistCatalog(BrowserWriteOutcome outcome) noexcept;

} // namespace navidrome
