#include "stdafx.h"
#include "LibraryImporter.h"
#include "LibraryImportLogic.h"
#include "Localization.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_set>

namespace {

constexpr std::size_t kPageSize = 500;
constexpr std::size_t kTailAnchorCount = 64;
constexpr std::size_t kRecursiveWorkers = 4;

bool cancelled(const std::shared_ptr<std::atomic_bool>& token) {
    return token && token->load();
}

bool equalsNoCase(std::string lhs, std::string rhs) {
    std::transform(lhs.begin(), lhs.end(), lhs.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    std::transform(rhs.begin(), rhs.end(), rhs.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lhs == rhs;
}

struct PageRun {
    std::vector<navidrome::Song> songs;
    std::string error;
    bool unsupported = false;
};

PageRun readStablePages(
        const navidrome::SubsonicRequestContext& context,
        std::size_t startOffset,
        const std::shared_ptr<std::atomic_bool>& cancel,
        bool validateScanToken,
        const navidrome::LibraryImportProgressCallback& progress,
        std::size_t knownCount) {
    PageRun run;
    auto& client = navidrome::SubsonicClientWin::get();
    auto core = navidrome::readStableSongPages(
        startOffset, kPageSize, 2, validateScanToken,
        [&client, &context](std::size_t offset, std::size_t count,
                std::string& error, bool& unsupported) {
            return client.getSongsPage(context, offset, count, error, &unsupported);
        },
        [&client, &context](std::string& error) {
            return client.getScanStatus(context, error);
        },
        [cancel]() { return cancelled(cancel); },
        [progress, knownCount](std::size_t scanned) {
            if (!progress) return;
            navidrome::LibraryImportProgress info;
            info.scanned = scanned;
            info.added = scanned > knownCount ? scanned - knownCount : 0;
            progress(info);
        });
    run.songs = std::move(core.songs);
    run.unsupported = core.status == navidrome::StablePageStatus::Unsupported;
    switch (core.status) {
    case navidrome::StablePageStatus::Success:
    case navidrome::StablePageStatus::Cancelled:
        break;
    case navidrome::StablePageStatus::ApiError:
    case navidrome::StablePageStatus::Unsupported:
        run.error = std::move(core.apiError);
        break;
    case navidrome::StablePageStatus::Scanning:
        run.error = navidrome::l10n::libraryScanning;
        break;
    case navidrome::StablePageStatus::BoundaryMismatch:
        run.error = navidrome::l10n::pagingInvalid;
        break;
    case navidrome::StablePageStatus::EmptySongId:
        run.error = navidrome::l10n::emptySongId;
        break;
    case navidrome::StablePageStatus::DuplicateSongId:
        run.error = navidrome::l10n::duplicateSongId;
        break;
    case navidrome::StablePageStatus::ScanChanged:
        run.error = navidrome::l10n::pagingUnstable;
        break;
    }
    return run;
}

navidrome::LibraryImportResult recursiveFallback(
        const navidrome::SubsonicRequestContext& context,
        const navidrome::LoadedImportState& loaded,
        const navidrome::ServerInfo& server,
        const std::string& libraryFingerprint,
        std::uint64_t operationId,
        const std::shared_ptr<std::atomic_bool>& cancel,
        const navidrome::LibraryImportProgressCallback& progress,
        std::shared_ptr<navidrome::ImportLease> lease) {
    navidrome::LibraryImportResult result;
    result.mode = navidrome::LibraryImportMode::RecursiveFallback;
    result.lease = std::move(lease);
    auto& client = navidrome::SubsonicClientWin::get();
    std::string rootError;
    auto artists = client.getArtists(context, rootError);
    if (!rootError.empty()) { result.error = rootError; return result; }
    if (cancelled(cancel)) { result.cancelled = true; return result; }

    std::atomic_size_t nextArtist{0};
    std::atomic_size_t completed{0};
    std::mutex resultMutex;
    std::vector<navidrome::Song> songs;
    std::size_t failed = 0;
    auto worker = [&]() {
        std::vector<navidrome::Song> local;
        std::size_t localFailed = 0;
        while (!cancelled(cancel)) {
            std::size_t index = nextArtist.fetch_add(1);
            if (index >= artists.size()) break;
            std::string error;
            auto albums = client.getAlbumsForArtist(context, artists[index].id, error);
            if (!error.empty()) ++localFailed;
            for (const auto& album : albums) {
                if (cancelled(cancel)) break;
                std::string albumError;
                auto fetched = client.getSongsForAlbum(context, album.id, albumError);
                if (!albumError.empty()) ++localFailed;
                local.insert(local.end(),
                    std::make_move_iterator(fetched.begin()),
                    std::make_move_iterator(fetched.end()));
            }
            std::size_t done = completed.fetch_add(1) + 1;
            if (progress) {
                navidrome::LibraryImportProgress info;
                info.completed = done;
                info.total = artists.size();
                info.scanned = local.size();
                info.failed = localFailed;
                info.recursive = true;
                progress(info);
            }
        }
        std::lock_guard<std::mutex> lock(resultMutex);
        failed += localFailed;
        songs.insert(songs.end(), std::make_move_iterator(local.begin()),
                     std::make_move_iterator(local.end()));
    };
    std::vector<std::thread> workers;
    const std::size_t count = (std::min)(kRecursiveWorkers, artists.size());
    for (std::size_t i = 0; i < count; ++i) workers.emplace_back(worker);
    for (auto& thread : workers) thread.join();
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    if (failed > 0) {
        result.error = navidrome::l10n::recursiveImportFailed(failed);
        return result;
    }

    std::unordered_set<std::string> oldIds;
    if (loaded.valid) oldIds.insert(loaded.state.knownSongIds.begin(),
                                   loaded.state.knownSongIds.end());
    std::unordered_set<std::string> currentIds;
    std::vector<navidrome::Song> unique;
    for (auto& song : songs) {
        if (cancelled(cancel)) { result.cancelled = true; return result; }
        if (song.id.empty()) {
            result.error = navidrome::l10n::emptySongId;
            return result;
        }
        if (!currentIds.insert(song.id).second) continue;
        if (oldIds.find(song.id) == oldIds.end()) result.candidates.push_back(song);
        unique.push_back(std::move(song));
    }
    result.scanned = unique.size();
    navidrome::LibraryImportState state;
    state.identity = navidrome::importIdentity(context);
    state.serverType = server.type;
    state.serverVersion = server.version;
    state.libraryFingerprint = libraryFingerprint;
    state.cursorCount = unique.size();
    for (const auto& song : unique) state.knownSongIds.push_back(song.id);
    // The recursive traversal has no protocol-defined stable song ordering.
    // Keep anchors empty so later runs never mistake it for a fast-tail baseline.
    state.tailAnchors.clear();
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    result.preparedState = navidrome::PreparedImportState::create(
        loaded, state, operationId, result.error);
    return result;
}

} // namespace

navidrome::LibraryImportResult navidrome::runLibraryImport(
        const SubsonicRequestContext& context, bool forceFull,
        std::uint64_t operationId,
        const std::shared_ptr<std::atomic_bool>& cancel,
        LibraryImportProgressCallback progress) {
    LibraryImportResult result;
    const auto identity = importIdentity(context);
    result.lease = ImportLease::tryAcquire(identity);
    if (!result.lease) { result.error = navidrome::l10n::accountImportBusy; return result; }

    auto loaded = loadImportState(context);
    if (!loaded.error.empty() && loaded.exists) {
        loaded.valid = false;
        loaded.error.clear();
    } else if (!loaded.error.empty()) {
        result.error = loaded.error;
        return result;
    }

    auto& client = SubsonicClientWin::get();
    std::string error;
    auto server = client.getServerInfo(context, error);
    if (!error.empty()) { result.error = error; return result; }
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    auto folders = client.getMusicFolders(context, error);
    if (!error.empty()) { result.error = error; return result; }
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    const auto libraryFingerprint = makeLibraryFingerprint(folders);
    const bool isNavidrome = equalsNoCase(server.type, "navidrome");
    const bool compatibleState = loaded.exists && loaded.valid &&
        loaded.state.identity == identity &&
        loaded.state.serverType == server.type &&
        loaded.state.serverVersion == server.version &&
        loaded.state.libraryFingerprint == libraryFingerprint;

    bool fast = !forceFull && isNavidrome && compatibleState &&
        !loaded.state.tailAnchors.empty();
    PageRun pages;
    std::size_t pageStart = 0;
    std::size_t lastAnchor = SIZE_MAX;
    if (fast) {
        pageStart = loaded.state.cursorCount > kPageSize
            ? static_cast<std::size_t>(loaded.state.cursorCount - kPageSize) : 0;
        while (true) {
            pages = readStablePages(context, pageStart, cancel, true, progress,
                                    loaded.state.knownSongIds.size());
            if (cancelled(cancel)) { result.cancelled = true; return result; }
            if (!pages.error.empty()) break;
            lastAnchor = findOrderedAnchors(pages.songs, loaded.state.tailAnchors);
            if (lastAnchor != SIZE_MAX || pageStart == 0) break;
            pageStart = pageStart > kPageSize ? pageStart - kPageSize : 0;
        }
        if (!pages.error.empty() || lastAnchor == SIZE_MAX) fast = false;
    }

    if (!fast) {
        pages = readStablePages(context, 0, cancel, isNavidrome, progress,
                                compatibleState ? loaded.state.knownSongIds.size() : 0);
        if (cancelled(cancel)) { result.cancelled = true; return result; }
        if (!pages.error.empty()) {
            if (pages.unsupported) return recursiveFallback(context, loaded, server,
                libraryFingerprint, operationId, cancel, progress, result.lease);
            result.error = pages.error;
            return result;
        }
        result.mode = LibraryImportMode::FullReconcile;
    } else {
        result.mode = LibraryImportMode::FastTail;
    }

    const bool reusableKnownIds = loaded.exists && loaded.valid &&
        loaded.state.identity == identity;
    std::unordered_set<std::string> knownIds;
    if (reusableKnownIds)
        knownIds.insert(loaded.state.knownSongIds.begin(), loaded.state.knownSongIds.end());
    const std::size_t firstCandidate = fast ? lastAnchor + 1 : 0;
    result.candidates = collectUnknownSongs(pages.songs, firstCandidate, knownIds);

    LibraryImportState state;
    state.identity = identity;
    state.serverType = server.type;
    state.serverVersion = server.version;
    state.libraryFingerprint = libraryFingerprint;
    if (fast) {
        state.knownSongIds.assign(knownIds.begin(), knownIds.end());
        state.cursorCount = pageStart + pages.songs.size();
        state.tailAnchors = tailSongIds(pages.songs, kTailAnchorCount);
    } else {
        state.cursorCount = pages.songs.size();
        for (const auto& song : pages.songs) state.knownSongIds.push_back(song.id);
        state.tailAnchors = tailSongIds(pages.songs, kTailAnchorCount);
    }
    result.scanned = state.cursorCount;
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    result.preparedState = PreparedImportState::create(
        loaded, state, operationId, result.error);
    return result;
}
