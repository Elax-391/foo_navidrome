# Windows Library Import Contract

## Scenario: Incremental Full-Library Import

### 1. Scope / Trigger

Apply this contract when changing the Windows `添加全部` or `完整核对` workflow, Subsonic full-library enumeration, import state, playlist append, or background-window handoff. The workflow crosses network, storage and native UI boundaries, so a successful compile alone is not sufficient.

### 2. Signatures

```cpp
SubsonicRequestContext SubsonicClientWin::snapshot() const;

StablePageResult readStableSongPages(
    std::size_t startOffset, std::size_t pageSize,
    std::size_t maxAttempts, bool validateScanToken,
    SongPageFetcher fetchPage, ScanStatusFetcher fetchScanStatus,
    ImportCancelled isCancelled, PageProgress onProgress = {});

LibraryImportResult runLibraryImport(
    const SubsonicRequestContext& context, bool forceFull,
    std::uint64_t operationId,
    const std::shared_ptr<std::atomic_bool>& cancel,
    LibraryImportProgressCallback progress);

CompensatedCommitResult commitWithPlaylistCompensation(
    bool hasPlaylistAppend, StateCommit commit,
    PlaylistRollback rollback);
```

### 3. Contracts

- Capture server URL, username, password, salt and custom headers once at operation start. Every request in one import uses that immutable context.
- Persist only normalized server URL + username, server/library identity, cursor count, exact song IDs and ordered tail anchors. Never persist password, salt, token or custom-header values.
- Navidrome paging uses `search3.view` with `query=`, song count/offset, 500-item pages, and a one-item overlap between adjacent pages.
- Check `getScanStatus` before and after a Navidrome page run. A run is stable only when both are not scanning and `lastScan` is unchanged.
- A versioned binary state file is written to an operation-specific temporary file, flushed, then atomically moved over the final file after verifying its loaded generation.
- One process-global `ImportLease` exists per normalized server URL + username across standalone and embedded windows.
- The completion payload owns both the prepared-state RAII object and the lease. A main-thread callback must own its payload through RAII even if the scheduler drops the callback.
- Append playlist items on the main thread, retain playlist/index/count and inserted handles, then commit state. On commit failure, delete only when the same handles still occupy the receipt range.

### 4. Validation & Error Matrix

| Condition | Required result |
|---|---|
| Empty song ID | Fail the run; do not append or commit |
| Unexpected page duplicate | Retry the whole run once, then fail |
| Page overlap mismatch | Retry the whole run once, then fail |
| Scan already active or token changes | Retry once, then report unstable/scanning |
| Search API explicitly unsupported | Enter the fixed four-worker recursive fallback |
| Network/HTTP/parse error | Fail; never misclassify as unsupported |
| State invalid/corrupt | Ignore for cursor trust and perform full reconciliation |
| Server version/library fingerprint changed | Disable fast tail, but reuse valid exact IDs for deduplication |
| State generation changed before commit | Reject commit and compensate playlist append |
| Receipt handles no longer match the range | Refuse removal and report rollback failure |
| Cancellation after a synchronous request | Stop before issuing the next request or preparing state |

### 5. Good / Base / Bad Cases

- Good: a compatible state finds ordered anchors near `cursorCount`; only unknown IDs after the anchor are appended and the state advances.
- Base: no state or `forceFull=true`; enumerate from offset zero, append unknown exact IDs, and rebuild the current baseline.
- Bad: silently deduplicate an unexpected page duplicate, submit an empty baseline after an HTTP failure, or discard known IDs merely because the server version changed.

### 6. Tests Required

- Paging: stable overlap, short final page, boundary mismatch, duplicate ID, empty ID and retry exhaustion.
- Scan: active-before retry and before/after token-change retry.
- Diff/anchors: no change, new ID in an old album, ordered anchor success and mismatch fallback.
- State: round trip, bad magic/version, truncation, length limit, duplicate ID, real temporary-file destructor cleanup, atomic commit and generation conflict.
- Concurrency: same-identity lease exclusion, different-identity success and RAII release.
- Compensation: commit failure calls rollback; success does not; invalid/moved receipt handles are not deleted.
- Release build: Windows `Release|x64`, PE machine x64, strict UTF-8, package entry `x64/foo_navidrome.dll`, and inner/outer SHA-256 equality.
- Real host smoke: first import, unchanged second import, an ID added to an existing album, forced reconciliation and existing single-item/play/search behavior.

### 7. Wrong vs Correct

Wrong:

```cpp
auto page = client.getSongsPage(offset, 500, error);
if (!error.empty()) return recursiveFallback();
playlist_insert_items(...);
preparedState.commit(error);
```

Correct:

```cpp
auto context = client.snapshot();
auto result = runLibraryImport(context, forceFull, operationId, cancel, progress);
auto receipt = enqueueNodes(result.candidates, false);
auto commitResult = commitWithPlaylistCompensation(
    receipt.count != 0,
    [&](std::string& error) { return result.preparedState->commit(error); },
    [&] { return rollbackAppend(receipt); });
```

The correct flow distinguishes unsupported APIs from transient failure, validates a stable page window, snapshots configuration, and compensates cross-store commit failure.
