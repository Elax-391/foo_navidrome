#include "../LibraryImportLogic.h"
#include "../LibraryImportState.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace {

int g_failed = 0;

void expect(bool condition, const char* name) {
    if (!condition) {
        ++g_failed;
        std::cerr << "FAILED: " << name << '\n';
    }
}

navidrome::Song song(const std::string& id) {
    navidrome::Song result;
    result.id = id;
    result.title = "title-" + id;
    return result;
}

std::vector<navidrome::Song> songs(std::initializer_list<const char*> ids) {
    std::vector<navidrome::Song> result;
    for (const auto* id : ids) result.push_back(song(id));
    return result;
}

void testPaging() {
    navidrome::SongPageAccumulator acc;
    expect(navidrome::appendSongPage(acc, songs({"1", "2", "3"}), 3) ==
        navidrome::PageAppendStatus::Continue, "first full page continues");
    expect(navidrome::appendSongPage(acc, songs({"3", "4"}), 3) ==
        navidrome::PageAppendStatus::Complete, "overlapped short page completes");
    expect(acc.songs.size() == 4 && acc.songs.back().id == "4",
           "overlap consumed once");

    navidrome::SongPageAccumulator mismatch;
    navidrome::appendSongPage(mismatch, songs({"1", "2"}), 2);
    expect(navidrome::appendSongPage(mismatch, songs({"9", "3"}), 2) ==
        navidrome::PageAppendStatus::BoundaryMismatch, "boundary mismatch rejected");

    navidrome::SongPageAccumulator duplicate;
    expect(navidrome::appendSongPage(duplicate, songs({"1", "1"}), 3) ==
        navidrome::PageAppendStatus::DuplicateSongId, "page duplicate rejected");

    navidrome::SongPageAccumulator crossPageDuplicate;
    navidrome::appendSongPage(crossPageDuplicate, songs({"1", "2", "3"}), 3);
    expect(navidrome::appendSongPage(
        crossPageDuplicate, songs({"3", "2"}), 3) ==
        navidrome::PageAppendStatus::DuplicateSongId,
        "unexpected cross-page duplicate rejected");

    navidrome::SongPageAccumulator empty;
    expect(navidrome::appendSongPage(empty, songs({"1", ""}), 3) ==
        navidrome::PageAppendStatus::EmptySongId, "empty id rejected");
}

void testAnchorsAndDiff() {
    auto current = songs({"1", "2", "3", "4", "5"});
    expect(navidrome::findOrderedAnchors(current, {"2", "3", "4"}) == 3,
           "ordered anchors locate final index");
    expect(navidrome::findOrderedAnchors(current, {"2", "4"}) ==
        (std::numeric_limits<std::size_t>::max)(), "non-contiguous anchors rejected");
    auto tail = navidrome::tailSongIds(current, 2);
    expect(tail == std::vector<std::string>({"4", "5"}), "tail anchors bounded");

    std::unordered_set<std::string> known = {"1", "2", "4"};
    auto added = navidrome::collectUnknownSongs(current, 2, known);
    expect(added.size() == 2 && added[0].id == "3" && added[1].id == "5",
           "exact id diff includes only unknown songs");
}

void testScanAndRollbackLogic() {
    navidrome::ScanStatus stableBefore{false, "100"};
    navidrome::ScanStatus stableAfter{false, "100"};
    navidrome::ScanStatus changed{false, "101"};
    navidrome::ScanStatus scanning{true, "100"};
    expect(navidrome::isStableScanWindow(stableBefore, stableAfter),
           "stable scan token accepted");
    expect(!navidrome::isStableScanWindow(stableBefore, changed),
           "changed scan token rejected");
    expect(!navidrome::isStableScanWindow(scanning, stableAfter),
           "active scan rejected");
    expect(navidrome::isValidAppendRange(10, 5, 15), "append receipt range valid");
    expect(!navidrome::isValidAppendRange(11, 5, 15), "append receipt overflow rejected");
    expect(navidrome::didRollbackRestoreCount(15, 10, 5), "rollback count restored");
    expect(!navidrome::didRollbackRestoreCount(15, 11, 5), "partial rollback rejected");

    bool rollbackCalled = false;
    auto compensated = navidrome::commitWithPlaylistCompensation(
        true,
        [](std::string& error) { error = "commit failed"; return false; },
        [&rollbackCalled]() { rollbackCalled = true; return true; });
    expect(!compensated.committed && compensated.rolledBack && rollbackCalled &&
           compensated.error == "commit failed", "commit failure compensates append");
    auto committed = navidrome::commitWithPlaylistCompensation(
        true, [](std::string&) { return true; },
        [&rollbackCalled]() { rollbackCalled = true; return false; });
    expect(committed.committed, "successful commit skips compensation");
}

void testStablePageRetries() {
    int scanCalls = 0;
    int pageCalls = 0;
    auto afterScanning = navidrome::readStableSongPages(
        0, 3, 2, true,
        [&pageCalls](std::size_t, std::size_t, std::string&, bool&) {
            ++pageCalls;
            return songs({"1", "2"});
        },
        [&scanCalls](std::string&) {
            ++scanCalls;
            return navidrome::ScanStatus{scanCalls == 1, "10"};
        }, []() { return false; });
    expect(afterScanning.status == navidrome::StablePageStatus::Success &&
           scanCalls == 3 && pageCalls == 1,
           "scanning first attempt retries once");

    int attempt = 0;
    int statusCall = 0;
    auto changedThenStable = navidrome::readStableSongPages(
        0, 3, 2, true,
        [&attempt](std::size_t, std::size_t, std::string&, bool&) {
            ++attempt;
            return songs({"1", "2"});
        },
        [&statusCall](std::string&) {
            ++statusCall;
            if (statusCall == 1) return navidrome::ScanStatus{false, "10"};
            if (statusCall == 2) return navidrome::ScanStatus{false, "11"};
            return navidrome::ScanStatus{false, "20"};
        }, []() { return false; });
    expect(changedThenStable.status == navidrome::StablePageStatus::Success &&
           attempt == 2, "scan token change retries from start");

    int unstableStatusCalls = 0;
    auto alwaysChanging = navidrome::readStableSongPages(
        0, 3, 2, true,
        [](std::size_t, std::size_t, std::string&, bool&) {
            return songs({"1", "2"});
        },
        [&unstableStatusCalls](std::string&) {
            ++unstableStatusCalls;
            return navidrome::ScanStatus{
                false, std::to_string(unstableStatusCalls)};
        }, []() { return false; });
    expect(alwaysChanging.status == navidrome::StablePageStatus::ScanChanged &&
           unstableStatusCalls == 4, "scan instability stops after two attempts");

    int boundaryAttempt = 0;
    auto boundaryRetry = navidrome::readStableSongPages(
        0, 3, 2, false,
        [&boundaryAttempt](std::size_t offset, std::size_t,
                std::string&, bool&) {
            const int current = boundaryAttempt / 2;
            ++boundaryAttempt;
            if (offset == 0) return songs({"1", "2", "3"});
            return current == 0 ? songs({"9", "4"}) : songs({"3", "4"});
        }, {}, []() { return false; });
    expect(boundaryRetry.status == navidrome::StablePageStatus::Success &&
           boundaryRetry.songs.size() == 4 && boundaryAttempt == 4,
           "boundary drift retries whole enumeration");

    int unsupportedCalls = 0;
    auto unsupported = navidrome::readStableSongPages(
        0, 3, 2, false,
        [&unsupportedCalls](std::size_t, std::size_t, std::string& error,
                bool& isUnsupported) {
            ++unsupportedCalls;
            error = "unsupported";
            isUnsupported = true;
            return std::vector<navidrome::Song>{};
        }, {}, []() { return false; });
    expect(unsupported.status == navidrome::StablePageStatus::Unsupported &&
           unsupportedCalls == 1, "unsupported api does not retry as transient");
}

navidrome::LibraryImportState sampleState() {
    navidrome::LibraryImportState state;
    state.identity = "https://example.test/Navidrome\nuser";
    state.serverType = "navidrome";
    state.serverVersion = "0.58.0";
    state.libraryFingerprint = "abc";
    state.cursorCount = 2;
    state.knownSongIds = {"song-a", "song-b"};
    state.tailAnchors = {"song-a", "song-b"};
    return state;
}

void testStateCodec() {
    auto state = sampleState();
    std::string error;
    auto bytes = navidrome::encodeImportState(state, error);
    expect(error.empty() && !bytes.empty(), "state encodes");
    navidrome::LibraryImportState decoded;
    expect(navidrome::decodeImportState(bytes, decoded, error), "state decodes");
    expect(decoded.identity == state.identity && decoded.cursorCount == 2 &&
           decoded.knownSongIds.size() == 2, "state round trip");

    auto truncated = bytes;
    truncated.pop_back();
    expect(!navidrome::decodeImportState(truncated, decoded, error),
           "truncated state rejected");
    auto badMagic = bytes;
    badMagic[0] ^= 0xff;
    expect(!navidrome::decodeImportState(badMagic, decoded, error),
           "bad magic rejected");
    auto badVersion = bytes;
    badVersion[8] = 2;
    expect(!navidrome::decodeImportState(badVersion, decoded, error),
           "bad version rejected");

    auto duplicate = sampleState();
    duplicate.knownSongIds = {"same", "same"};
    navidrome::encodeImportState(duplicate, error);
    expect(!error.empty(), "duplicate ids rejected on encode");
    auto empty = sampleState();
    empty.knownSongIds = {""};
    navidrome::encodeImportState(empty, error);
    expect(!error.empty(), "empty ids rejected on encode");
    auto tooLong = sampleState();
    tooLong.identity.assign(8193, 'x');
    navidrome::encodeImportState(tooLong, error);
    expect(!error.empty(), "oversized field rejected");
}

void testIdentityAndLease() {
    expect(navidrome::normalizeServerUrl(" HTTPS://Example.COM/Navidrome/ ") ==
        "https://example.com/Navidrome", "url normalizes scheme and authority only");
    expect(navidrome::normalizeServerUrl("https://example.com/A") !=
        navidrome::normalizeServerUrl("https://example.com/a"),
        "url path remains case sensitive");

    auto first = navidrome::ImportLease::tryAcquire("server\nuser");
    auto duplicate = navidrome::ImportLease::tryAcquire("server\nuser");
    auto other = navidrome::ImportLease::tryAcquire("server\nother");
    expect(first && !duplicate && other, "identity lease mutual exclusion");
    first.reset();
    expect(navidrome::ImportLease::tryAcquire("server\nuser") != nullptr,
           "identity lease releases with RAII");
}

std::wstring createTestDirectory() {
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    const auto directory = std::wstring(tempPath) + L"foo_navidrome-import-tests-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    expect(CreateDirectoryW(directory.c_str(), nullptr) != FALSE,
           "test state directory created");
    return directory;
}

std::vector<std::wstring> findStateFiles(const std::wstring& directory) {
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return result;
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
            result.push_back(directory + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return result;
}

void testPreparedStateFiles() {
    const auto directory = createTestDirectory();
    navidrome::setImportStateTestDirectory(directory);
    navidrome::SubsonicRequestContext context;
    context.serverUrl = "https://example.test/Navidrome";
    context.username = "user";
    auto loaded = navidrome::loadImportState(context);
    expect(loaded.valid && !loaded.exists && loaded.generation == "missing",
           "missing state loads as valid baseline");

    auto state = sampleState();
    std::string error;
    auto prepared = navidrome::PreparedImportState::create(loaded, state, 1, error);
    expect(prepared && prepared->isReady(), "state temp file prepared");
    expect(findStateFiles(directory).size() == 1, "prepared temp file exists");
    prepared.reset();
    expect(findStateFiles(directory).empty(), "prepared temp removed by RAII");

    prepared = navidrome::PreparedImportState::create(loaded, state, 2, error);
    expect(prepared && prepared->commit(error), "state atomically committed");
    auto committed = navidrome::loadImportState(context);
    expect(committed.valid && committed.exists &&
           committed.state.knownSongIds.size() == 2,
           "committed state reloads");

    auto nextState = state;
    nextState.knownSongIds.push_back("song-c");
    nextState.cursorCount = 3;
    auto conflict = navidrome::PreparedImportState::create(
        committed, nextState, 3, error);
    auto competing = state;
    competing.serverVersion = "changed";
    auto competingPrepared = navidrome::PreparedImportState::create(
        committed, competing, 4, error);
    expect(competingPrepared && competingPrepared->commit(error),
           "competing state committed");
    error.clear();
    expect(conflict && !conflict->commit(error) && !error.empty(),
           "generation conflict rejected");
    conflict.reset();

    for (const auto& file : findStateFiles(directory)) DeleteFileW(file.c_str());
    expect(RemoveDirectoryW(directory.c_str()) != FALSE,
           "test state directory removed");
}

} // namespace

int main() {
    testPaging();
    testAnchorsAndDiff();
    testScanAndRollbackLogic();
    testStablePageRetries();
    testStateCodec();
    testIdentityAndLease();
    testPreparedStateFiles();
    if (g_failed != 0) {
        std::cerr << g_failed << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All library import tests passed\n";
    return EXIT_SUCCESS;
}
