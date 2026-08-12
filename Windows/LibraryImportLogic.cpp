#include "LibraryImportLogic.h"
#include <algorithm>
#include <limits>
#include <utility>

navidrome::PageAppendStatus navidrome::appendSongPage(
        SongPageAccumulator& accumulator, std::vector<Song> page,
        std::size_t requestedPageSize) {
    if (page.empty()) return PageAppendStatus::Complete;

    std::size_t first = 0;
    if (!accumulator.previousLastId.empty()) {
        if (page.front().id != accumulator.previousLastId)
            return PageAppendStatus::BoundaryMismatch;
        first = 1;
    }
    const std::string lastId = page.back().id;
    for (std::size_t i = first; i < page.size(); ++i) {
        if (page[i].id.empty()) return PageAppendStatus::EmptySongId;
        if (!accumulator.seenIds.insert(page[i].id).second)
            return PageAppendStatus::DuplicateSongId;
        accumulator.songs.push_back(std::move(page[i]));
    }
    if (page.size() < requestedPageSize) return PageAppendStatus::Complete;
    accumulator.previousLastId = lastId;
    return PageAppendStatus::Continue;
}

navidrome::StablePageResult navidrome::readStableSongPages(
        std::size_t startOffset, std::size_t pageSize,
        std::size_t maxAttempts, bool validateScanToken,
        SongPageFetcher fetchPage, ScanStatusFetcher fetchScanStatus,
        ImportCancelled isCancelled, PageProgress onProgress) {
    StablePageResult result;
    if (pageSize < 2 || maxAttempts == 0 || !fetchPage ||
        (validateScanToken && !fetchScanStatus)) {
        result.status = StablePageStatus::ApiError;
        return result;
    }

    for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt) {
        SongPageAccumulator accumulator;
        ScanStatus before;
        if (validateScanToken) {
            before = fetchScanStatus(result.apiError);
            if (!result.apiError.empty()) {
                result.status = StablePageStatus::ApiError;
                return result;
            }
            if (before.scanning) {
                if (attempt + 1 < maxAttempts) continue;
                result.status = StablePageStatus::Scanning;
                return result;
            }
        }

        std::size_t offset = startOffset;
        StablePageStatus attemptStatus = StablePageStatus::Success;
        while (!isCancelled || !isCancelled()) {
            std::string error;
            bool unsupported = false;
            auto page = fetchPage(offset, pageSize, error, unsupported);
            if (!error.empty()) {
                result.apiError = std::move(error);
                attemptStatus = unsupported && accumulator.songs.empty()
                    ? StablePageStatus::Unsupported : StablePageStatus::ApiError;
                break;
            }
            if (page.empty()) break;
            const std::size_t received = page.size();
            switch (appendSongPage(accumulator, std::move(page), pageSize)) {
            case PageAppendStatus::Continue:
                offset += received - 1;
                if (onProgress) onProgress(startOffset + accumulator.songs.size());
                continue;
            case PageAppendStatus::Complete:
                break;
            case PageAppendStatus::BoundaryMismatch:
                attemptStatus = StablePageStatus::BoundaryMismatch;
                break;
            case PageAppendStatus::EmptySongId:
                attemptStatus = StablePageStatus::EmptySongId;
                break;
            case PageAppendStatus::DuplicateSongId:
                attemptStatus = StablePageStatus::DuplicateSongId;
                break;
            }
            break;
        }
        if (isCancelled && isCancelled()) {
            result.status = StablePageStatus::Cancelled;
            return result;
        }
        if (attemptStatus != StablePageStatus::Success) {
            const bool retryable =
                attemptStatus == StablePageStatus::BoundaryMismatch ||
                attemptStatus == StablePageStatus::DuplicateSongId;
            if (retryable && attempt + 1 < maxAttempts) continue;
            result.status = attemptStatus;
            return result;
        }

        if (validateScanToken) {
            std::string error;
            auto after = fetchScanStatus(error);
            if (!error.empty()) {
                result.status = StablePageStatus::ApiError;
                result.apiError = std::move(error);
                return result;
            }
            if (!isStableScanWindow(before, after)) {
                if (attempt + 1 < maxAttempts) continue;
                result.status = StablePageStatus::ScanChanged;
                return result;
            }
        }
        result.status = StablePageStatus::Success;
        result.songs = std::move(accumulator.songs);
        return result;
    }
    result.status = StablePageStatus::ScanChanged;
    return result;
}

std::vector<std::string> navidrome::tailSongIds(
        const std::vector<Song>& songs, std::size_t limit) {
    const auto first = songs.size() > limit ? songs.size() - limit : 0;
    std::vector<std::string> result;
    result.reserve(songs.size() - first);
    for (std::size_t i = first; i < songs.size(); ++i)
        result.push_back(songs[i].id);
    return result;
}

std::size_t navidrome::findOrderedAnchors(
        const std::vector<Song>& songs,
        const std::vector<std::string>& anchors) {
    if (anchors.empty() || songs.size() < anchors.size())
        return (std::numeric_limits<std::size_t>::max)();
    for (std::size_t i = 0; i + anchors.size() <= songs.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < anchors.size(); ++j) {
            if (songs[i + j].id != anchors[j]) {
                match = false;
                break;
            }
        }
        if (match) return i + anchors.size() - 1;
    }
    return (std::numeric_limits<std::size_t>::max)();
}

std::vector<navidrome::Song> navidrome::collectUnknownSongs(
        const std::vector<Song>& songs, std::size_t first,
        std::unordered_set<std::string>& knownIds) {
    std::vector<Song> result;
    if (first >= songs.size()) return result;
    for (std::size_t i = first; i < songs.size(); ++i) {
        if (knownIds.insert(songs[i].id).second) result.push_back(songs[i]);
    }
    return result;
}

bool navidrome::isStableScanWindow(
        const ScanStatus& before, const ScanStatus& after) {
    return !before.scanning && !after.scanning &&
        before.lastScan == after.lastScan;
}

bool navidrome::isValidAppendRange(
        std::size_t insertPos, std::size_t count,
        std::size_t playlistCount) {
    return insertPos <= playlistCount && count <= playlistCount - insertPos;
}

bool navidrome::didRollbackRestoreCount(
        std::size_t countBefore, std::size_t countAfter,
        std::size_t removedCount) {
    return countAfter <= countBefore && removedCount == countBefore - countAfter;
}

navidrome::CompensatedCommitResult navidrome::commitWithPlaylistCompensation(
        bool hasPlaylistAppend, StateCommit commit, PlaylistRollback rollback) {
    CompensatedCommitResult result;
    result.committed = commit && commit(result.error);
    if (result.committed) return result;
    result.rolledBack = !hasPlaylistAppend || (rollback && rollback());
    return result;
}
