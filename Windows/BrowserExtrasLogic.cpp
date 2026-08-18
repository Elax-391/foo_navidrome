#include "BrowserExtrasLogic.h"
#include "TrackUriMetadata.h"

#include <algorithm>
#include <iterator>

navidrome::GroupedNavigationDefaults navidrome::groupedNavigationDefaults() {
    return {
        {NavigationGroupKind::SmartLists,
         NavigationGroupKind::ServerPlaylists,
         NavigationGroupKind::Genres,
         NavigationGroupKind::Artists},
        {SmartListKind::StarredSongs,
         SmartListKind::StarredAlbums,
         SmartListKind::StarredArtists,
         SmartListKind::NewestAlbums,
         SmartListKind::FrequentAlbums,
         SmartListKind::RecentAlbums,
         SmartListKind::RandomAlbums},
    };
}

navidrome::SmartListKind navidrome::favoriteSmartListKind(
        FavoriteKind kind) noexcept {
    if (kind == FavoriteKind::Album) return SmartListKind::StarredAlbums;
    if (kind == FavoriteKind::Artist) return SmartListKind::StarredArtists;
    return SmartListKind::StarredSongs;
}

bool navidrome::isBrowserContainer(BrowserNodeKind kind) noexcept {
    return kind == BrowserNodeKind::NavigationGroup ||
           kind == BrowserNodeKind::Artist || kind == BrowserNodeKind::Album ||
           kind == BrowserNodeKind::Genre ||
           kind == BrowserNodeKind::SmartList ||
           kind == BrowserNodeKind::ServerPlaylist;
}

bool navidrome::isBrowserPlayable(BrowserNodeKind kind) noexcept {
    return kind == BrowserNodeKind::Artist || kind == BrowserNodeKind::Album ||
           kind == BrowserNodeKind::Song || kind == BrowserNodeKind::Genre ||
           kind == BrowserNodeKind::SmartList ||
           kind == BrowserNodeKind::ServerPlaylist;
}

std::string navidrome::formatSongLabel(const Song& song,
                                       const std::string& fallbackTitle) {
    std::string label;
    if (song.starred) label += u8"★ ";
    if (song.track > 0) label += std::to_string(song.track) + ". ";
    label += song.title.empty() ? fallbackTitle : song.title;
    if (song.userRating && *song.userRating > 0)
        label += "  [" + std::to_string(*song.userRating) + u8"/5 星]";
    return label;
}

std::string navidrome::formatPlaylistLabel(const ServerPlaylist& playlist) {
    return playlist.name + " (" + std::to_string(playlist.songCount) + u8" 首)";
}

std::string navidrome::downloadFileName(const Song& song) {
    std::string name;
    if (song.track > 0) {
        if (song.track < 10) name += "0";
        name += std::to_string(song.track) + ". ";
    }
    if (!song.artist.empty()) name += song.artist + " - ";
    name += song.title.empty() ? "untitled" : song.title;
    name = sanitizeFileName(name);
    if (!song.suffix.empty()) name += "." + song.suffix;
    return name;
}

navidrome::PlaylistUriMapping navidrome::mapActivePlaylistUris(
        const std::vector<std::string>& orderedPaths) {
    PlaylistUriMapping mapping;
    for (const auto& path : orderedPaths) {
        if (path.empty()) {
            ++mapping.skippedEmpty;
            continue;
        }
        if (path.rfind("navidrome://", 0) != 0) {
            ++mapping.skippedUnsupported;
            continue;
        }
        Song song;
        if (!parseTrackURI(path, song) || song.id.empty()) {
            ++mapping.skippedMalformed;
            continue;
        }
        mapping.orderedSongIds.push_back(std::move(song.id));
    }
    return mapping;
}

std::vector<navidrome::ServerPlaylist> navidrome::exactNameMatches(
        const std::vector<ServerPlaylist>& playlists, const std::string& desiredName) {
    std::vector<ServerPlaylist> result;
    std::copy_if(playlists.begin(), playlists.end(), std::back_inserter(result),
                 [&](const ServerPlaylist& playlist) {
        return playlist.name == desiredName;
    });
    return result;
}

std::string navidrome::firstAvailableNumberedName(
        const std::string& baseName, const std::vector<ServerPlaylist>& playlists) {
    for (std::size_t number = 2;; ++number) {
        const auto candidate = baseName + " (" + std::to_string(number) + ")";
        const auto collision = std::any_of(
            playlists.begin(), playlists.end(), [&](const ServerPlaylist& playlist) {
                return playlist.name == candidate;
            });
        if (!collision) return candidate;
    }
}

navidrome::BrowserUploadPlan navidrome::makeUploadPlan(
        const std::string& activePlaylistName, const PlaylistUriMapping& mapping,
        UploadChoice choice, const std::optional<std::string>& selectedPlaylistId,
        const std::vector<ServerPlaylist>& catalog, std::size_t sourceItemCount) {
    BrowserUploadPlan plan;
    plan.orderedSongIds = mapping.orderedSongIds;
    plan.sourceItemCount = sourceItemCount;
    plan.skippedCount = mapping.skippedCount();
    if (choice == UploadChoice::Cancel) {
        plan.status = UploadPlanStatus::Cancelled;
        return plan;
    }
    if (activePlaylistName.empty() || mapping.orderedSongIds.empty()) return plan;

    if (choice == UploadChoice::Replace) {
        if (!selectedPlaylistId || selectedPlaylistId->empty()) return plan;
        const auto matches = exactNameMatches(catalog, activePlaylistName);
        const auto found = std::any_of(matches.begin(), matches.end(),
            [&](const ServerPlaylist& playlist) {
                return playlist.id == *selectedPlaylistId;
            });
        if (!found) return plan;
        plan.targetPlaylistId = selectedPlaylistId;
        plan.targetName = activePlaylistName;
    } else if (choice == UploadChoice::NumberedCopy) {
        plan.targetName = firstAvailableNumberedName(activePlaylistName, catalog);
    } else {
        if (!exactNameMatches(catalog, activePlaylistName).empty()) return plan;
        plan.targetName = activePlaylistName;
    }
    plan.status = UploadPlanStatus::Ready;
    return plan;
}

navidrome::BrowserWriteOutcome navidrome::classifyWriteOutcome(
        const WriteEvidence& evidence) {
    if (evidence.verifiedCurrentIds &&
        *evidence.verifiedCurrentIds == evidence.requestedIds)
        return BrowserWriteOutcome::Complete;
    if (evidence.restorationAttempted) {
        if (!evidence.verifiedRestoredIds) return BrowserWriteOutcome::Unknown;
        if (evidence.originalIds &&
            *evidence.verifiedRestoredIds == *evidence.originalIds)
            return BrowserWriteOutcome::Restored;
        return BrowserWriteOutcome::Partial;
    }
    if (evidence.verifiedCurrentIds) {
        if (evidence.originalIds && *evidence.verifiedCurrentIds == *evidence.originalIds)
            return BrowserWriteOutcome::FailedNoChange;
        return BrowserWriteOutcome::Partial;
    }
    if (evidence.mutationAttempted || evidence.restorationAttempted ||
        evidence.transportState == PlaylistWriteState::Accepted ||
        evidence.transportState == PlaylistWriteState::Partial ||
        evidence.transportState == PlaylistWriteState::Unknown)
        return BrowserWriteOutcome::Unknown;
    return BrowserWriteOutcome::FailedNoChange;
}

bool navidrome::shouldRefreshPlaylistCatalog(
        BrowserWriteOutcome outcome) noexcept {
    return outcome == BrowserWriteOutcome::Complete ||
           outcome == BrowserWriteOutcome::Partial ||
           outcome == BrowserWriteOutcome::Unknown;
}
