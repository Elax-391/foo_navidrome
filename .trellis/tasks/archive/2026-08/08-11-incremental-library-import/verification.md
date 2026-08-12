# Verification: 全库并发与增量导入

## Automated Results

- Windows `Release|x64` full rebuild: PASS, MSBuild exit code 0, toolset `v145`.
- Production output: `foo_navidrome.dll`, PE `8664 machine (x64)`.
- Windows deterministic tests: PASS with `/W4 /WX`; output `All library import tests passed`.
- State tests: codec round trip, truncated/magic/version/duplicate/empty/oversize rejection, real Win32 temp-file RAII cleanup, atomic replace, generation conflict, and identity-scoped lease.
- Import logic tests: page overlap, boundary drift retry, scan-in-progress retry, scan-token retry, unsupported API classification, anchor matching, exact ID diff, compensation callback, and rollback range/count validation.
- Static checks: strict UTF-8 decode for all changed product/test files, vcxproj XML parse, common `/utf-8`, and `git diff --check` PASS.
- Lifecycle review: background workers do not capture `BrowserWindow*`; dropped main-thread callbacks retain payload ownership through RAII; stale operation payloads are immediately owned/deleted by handlers; cancellation gates run after synchronous identity/root requests and before state preparation.
- Security review: persistent state stores normalized server URL + username, server/library identity, cursor, anchors and song IDs only. Password, salt, auth token and custom headers remain request-context memory only.

## Delivery

- DLL: `dist/zh-CN-win-x64-incremental-import/foo_navidrome.dll`
- DLL size: `355328` bytes
- DLL SHA-256: `FA0C28862B0B1487AC58FB268C46E71C8F6D24D1DBC2C70C07F7EC4D7BF9716B`
- Component: `dist/zh-CN-win-x64-incremental-import/foo_navidrome_1.3.0_zh-CN_incremental-import_win-x64.fb2k-component`
- Component size: `159147` bytes
- Component SHA-256: `5B01507E5875FB26328ECE26342304672F48510EEE42DA3660F33B92B1AB952F`
- Archive entries: exactly `x64/foo_navidrome.dll`
- Inner/outer DLL SHA-256: identical
- UTF-16 marker `完整核对`: present
- UTF-8 marker `没有新增歌曲，音乐库已是最新`: present

The previous bulk-import delivery was not modified:

- Old DLL SHA-256: `A59E58141F18D16B5F1C976CA2D3274AE4C9F027155F968CC63602A4734A2337`
- Old component SHA-256: `80D8DAB5AD1E09963B1844239D0B1C3AA4B3D4EB26DF695558A08197B13F24B9`

## Acceptance Evidence

- AC1-AC16: production data flow implemented and covered by build, deterministic logic/state/fault tests, and focused code review. Host/network behavior is subject to the manual smoke boundary below.
- AC17: existing single-item queue, play, search, refresh, context-menu, Enter and `navidrome://` code paths remain registered and compile; live foobar2000 regression requires host smoke testing.
- AC18: PASS by clean x64 rebuild, PE validation, package structure, localization markers and SHA-256 equality.

## Manual Smoke Boundary

No controllable Navidrome test server or automated foobar2000 UI harness was available in this workspace. The following remain explicit post-install checks on the user's real server:

1. First `添加全部`: all accessible songs append once and a baseline is created.
2. Second `添加全部` with no changes: no tracks append and the UI reports that the library is current.
3. Add a song to an existing album, rescan Navidrome, then `添加全部`: only the new song ID appends.
4. Click `完整核对`: it scans the current library and appends only previously unknown IDs.
5. Confirm single-item add, immediate play, search, refresh, right-click, Enter and stream playback still work in foobar2000.

Known protocol limitation: fast-tail mode depends on Navidrome's current empty-query natural row order. Anchor, scan-token, version and library-fingerprint failures fall back to a full reconciliation; same-folder permission changes that do not alter any exposed identity may require manual `完整核对`.
