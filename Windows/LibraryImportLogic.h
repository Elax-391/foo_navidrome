#pragma once

#include "../SubsonicTypes.h"
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace navidrome {

enum class PageAppendStatus {
    Continue,
    Complete,
    BoundaryMismatch,
    EmptySongId,
    DuplicateSongId,
};

struct SongPageAccumulator {
    std::vector<Song> songs;
    std::unordered_set<std::string> seenIds;
    std::string previousLastId;
};

enum class StablePageStatus {
    Success,
    Cancelled,
    ApiError,
    Unsupported,
    Scanning,
    BoundaryMismatch,
    EmptySongId,
    DuplicateSongId,
    ScanChanged,
};

struct StablePageResult {
    StablePageStatus status = StablePageStatus::Success;
    std::vector<Song> songs;
    std::string apiError;
};

using SongPageFetcher = std::function<std::vector<Song>(
    std::size_t offset, std::size_t count,
    std::string& outError, bool& outUnsupported)>;
using ScanStatusFetcher = std::function<ScanStatus(std::string& outError)>;
using ImportCancelled = std::function<bool()>;
using PageProgress = std::function<void(std::size_t scanned)>;

StablePageResult readStableSongPages(
    std::size_t startOffset,
    std::size_t pageSize,
    std::size_t maxAttempts,
    bool validateScanToken,
    SongPageFetcher fetchPage,
    ScanStatusFetcher fetchScanStatus,
    ImportCancelled isCancelled,
    PageProgress onProgress = {});

PageAppendStatus appendSongPage(SongPageAccumulator& accumulator,
                                std::vector<Song> page,
                                std::size_t requestedPageSize);

std::vector<std::string> tailSongIds(const std::vector<Song>& songs,
                                     std::size_t limit);

std::size_t findOrderedAnchors(const std::vector<Song>& songs,
                               const std::vector<std::string>& anchors);

std::vector<Song> collectUnknownSongs(
    const std::vector<Song>& songs,
    std::size_t first,
    std::unordered_set<std::string>& knownIds);

bool isStableScanWindow(const ScanStatus& before, const ScanStatus& after);

bool isValidAppendRange(std::size_t insertPos, std::size_t count,
                        std::size_t playlistCount);

bool didRollbackRestoreCount(std::size_t countBefore,
                             std::size_t countAfter,
                             std::size_t removedCount);

struct CompensatedCommitResult {
    bool committed = false;
    bool rolledBack = false;
    std::string error;
};

using StateCommit = std::function<bool(std::string& outError)>;
using PlaylistRollback = std::function<bool()>;

CompensatedCommitResult commitWithPlaylistCompensation(
    bool hasPlaylistAppend,
    StateCommit commit,
    PlaylistRollback rollback);

} // namespace navidrome
