# Windows Cover Art & ESLyric Lyrics — Implementation Plan

## 1. Baseline

- [ ] Run `trellis-before-dev`; confirm task status, clean intent, applicable
  specs ([windows-library-import.md](../../spec/frontend/windows-library-import.md),
  [windows-localization.md](../../spec/frontend/windows-localization.md)) and
  the research contract
  ([research/eslyric-integration.md](research/eslyric-integration.md)).
- [ ] Record baselines: current `Windows/NavidromePluginWin.cpp`,
  `Windows/SubsonicClientWin.*`, `Windows/NavidromeInputWin.cpp`, both
  vcxproj files; SHA-256 of the last shipped x64 DLL. Create a fresh isolated
  build tree (do not overwrite previous dist artifacts).

## 2. MediaEnrichmentLogic (pure, SDK-free)

- [ ] Add `Windows/MediaEnrichmentLogic.h/.cpp`: move `uriDecode` (and the
  param scanner) out of `NavidromeInputWin.cpp`'s anonymous namespace into
  this module; `NavidromeInputWin.cpp` consumes it with identical semantics.
- [ ] Implement `resolveArtId(path)` with precedence coverArt → id → path
  segment, single percent-decode (design §2.2).
- [ ] Implement `classifyHttp(status)`, `classifyBody(contentType, bytes)`
  incl. image magic table and Subsonic-error-envelope mapping (70→NotFound,
  40/41/44→Auth, else ServerError) (design §2.3-2.4).
- [ ] Implement cover cache policy: `identityKey` reusing the import-state
  normalization, LRU with `kMaxCacheEntries=32` / `kMaxCacheBytes=48MB`,
  success-only.
- [ ] Implement `jsEscape` and `buildEsLyricConfigJs(context, headers,
  version)` returning the full config.js text; assert/never include the raw
  password (design §3.3).
- [ ] Register the new files in `Windows/foo_navidrome.vcxproj` (shared item
  group; `/utf-8` untouched).

## 3. Cover transport + extractor

- [ ] Add `SubsonicClientWin::httpGetBinary(context, url, maxBytes, abort)`
  → `BinaryFetchResult` with explicit `WinHttpSetTimeouts(0,15000,15000,30000)`,
  TLS setup, custom headers, abort checks (before connect/send, per read
  chunk), size cap, status + Content-Type capture (design §2.3).
- [ ] Replace `NavidromeArtInstance`/`NavidromeArtFallback`
  (`Windows/NavidromePluginWin.cpp:438-546`) with the
  `album_art_extractor` + instance from design §2.1: cache-first query,
  cover_front only, classified failures → deduplicated console diagnostics
  (no secrets), all failures surface as `exception_album_art_not_found`,
  aborts as `exception_aborted`.
- [ ] Delete the dead placeholder lines (old 447-450) with the class they
  live in; verify no other consumer of `urlParam` remains.

## 4. ESLyric bridge

- [ ] Add `Windows/EsLyricScript.h`: `navidrome.js` source as raw-string
  literal implementing design §3.4 exactly (pure helper functions,
  `getConfig`/`getLyrics`, structured→LRC conversion with baked offset,
  404-only legacy fallback, session `unsupportedServers`, `checkAbort`, no
  `location`, no secret logging, `debug` gate for the one-time rawPath log).
  Include a content version marker constant.
- [ ] Add `Windows/EsLyricBridge.h/.cpp`: profile-path resolution (reuse
  LibraryImportState's mechanism), `eslyric-data` existence gate, directory
  creation, atomic write (tmp + `MoveFileExW`), rewrite-on-content-change
  logic, `refresh()` entry point (design §3.1).
- [ ] Wire triggers: new `initquit` service; call after `saveSettings()` in
  `NavidromePrefsInstance::apply()` (`Windows/NavidromePluginWin.cpp:213`);
  call from `NavidromeHeadersWindow::OnSave`
  (`Windows/NavidromePluginWin.cpp:166-169`) (design §3.2).
- [ ] Add `Localization.h` entries for the new user-visible/console strings
  (bridge written / eslyric-data missing / bridge write failure) per
  [windows-localization.md](../../spec/frontend/windows-localization.md).

## 5. Deterministic tests

- [ ] Add `Windows/tests/MediaEnrichmentTests.vcxproj` + `.cpp` mirroring the
  existing test project (console exe, `Release|x64`, v143 declaration,
  `/utf-8`, W4+WX, links `MediaEnrichmentLogic.cpp` only).
- [ ] Test id resolution: precedence, decode-once (UTF-8/Chinese id, `%2F`,
  `%2B`, `+` handling), legacy `stream.view` URLs, missing-id cases.
- [ ] Test classification matrix: 200+image (each magic type + content-type
  path), 200+Subsonic error 70/40/44/other, 401/403, 404, 5xx, transport,
  oversize boundary (== cap ok, cap+1 rejected), non-image junk.
- [ ] Test cache: identity normalization (trailing slash, case, different
  user ⇒ different key), LRU order, entry-count and byte-budget eviction.
- [ ] Test config.js generation: escaping (quotes/backslash/CRLF/control/CJK
  /emoji), token vector `md5hex(password+salt)` against a precomputed value,
  headers map splitting, `debug:false` default, raw-password-absent
  invariant, stable output for stable input (idempotent rewrite check).
- [ ] Run BOTH test exes; require 0 failed each.

## 6. Static and consistency checks

- [ ] Strict UTF-8 decode of every changed source file.
- [ ] Parse both vcxproj files as XML: new files registered, `/utf-8`
  present, platform configuration list unchanged (Win32/x64/ARM64EC).
- [ ] `git diff --check`.
- [ ] Review: no secrets (password/salt/token/header values, cover bytes,
  lyric text) in any log path — C++ `console::print` and script
  `console.log`; `lyricMeta.location` not set; art instance holds no window
  references; abort paths close all WinHTTP handles.
- [ ] Grep first-party `Windows/` sources for leftover references to the
  removed fallback classes.

Suggested commands:

```powershell
git diff --check
git grep -n -E "NavidromeArtFallback|album_art_fallback|urlParam" -- Windows
git grep -n -E "location\s*=" -- Windows/EsLyricScript.h
```

## 7. Build, package, verify

- [ ] Copy product files into the isolated build tree (verified SDK/ATL/WTL
  sibling layout from the previous task).
- [ ] Build both test projects and the component with VS Build Tools v145:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' `
  '<build-tree>\workspace\foo_navidrome\Windows\foo_navidrome.vcxproj' `
  /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 `
  /p:SolutionDir='<build-tree>\workspace\foo_navidrome\' `
  /p:OutDir='<build-tree>\out\x64\' /m /nologo /v:minimal
```

  (same invocation shape for `Windows\tests\*.vcxproj`; run both exes, 0
  failed).
- [ ] Verify DLL PE machine = x64; new symbols present (extractor + bridge).
- [ ] Package `dist/cover-lyrics-win-x64/foo_navidrome_<ver>.fb2k-component`
  containing exactly `x64/foo_navidrome.dll`; inner vs standalone DLL SHA-256
  equal.

## 8. Real-host smoke (AC13)

- [ ] Install component + ESLyric (≥1.0.6.x) on the Windows host/VM; confirm
  `eslyric-data\scripts\searcher\navidrome.js` and
  `scripts\lib\foo_navidrome\config.js` appear after restart, and that
  `config.js` contains token (not password).
- [ ] Temporarily set `debug:true` in config.js → play a Navidrome track →
  fb2k console shows `meta.rawPath` beginning `navidrome://track/` (research
  risk (a) closed). Restore `debug:false` (any settings apply regenerates).
- [ ] AC1/AC2: cover shows for a `navidrome://` item and a legacy
  `/rest/stream.view` item; second display of the same album issues no new
  server hit (server log or one-shot observation).
- [ ] AC6/AC7/AC9: synced track scrolls with correct timing (offset track if
  available); Chinese/emoji plain-lyrics track renders intact; no-lyrics
  track shows a clean empty result.
- [ ] AC8: point config at a non-OpenSubsonic/legacy server if available —
  otherwise verify by code review + console: no fallback request after
  simulated 5xx/auth failure (temporarily wrong token), exactly one legacy
  attempt after a forced 404.
- [ ] AC10: run `添加全部` on a non-trivial library while watching the server
  access log — zero `getCoverArt`/`getLyrics*` requests during import.
- [ ] Credential rotation: change salt → apply → restart foobar → lyrics
  still fetch (new token in config.js).
- [ ] Regression: single add, play now, search, refresh, context menu,
  `完整核对`, streaming with custom headers all behave as before.

## 9. Risky files / rollback points

- `Windows/NavidromePluginWin.cpp` — service replacement (extractor) + new
  triggers. Rollback: restore fallback classes, drop trigger calls.
- `Windows/NavidromeInputWin.cpp` — helper relocation only; behavior must be
  diff-provably identical. Rollback: restore local helpers.
- `Windows/SubsonicClientWin.*` — additive (`httpGetBinary`). Rollback:
  remove method.
- Bridge files on user machines are regenerated blobs; stale copies fail
  closed (script returns without config) — no uninstall migration needed.

## 10. Before `task.py start`

- [ ] `prd.md`, `design.md`, this plan reviewed by the user.
- [ ] `implement.jsonl` / `check.jsonl` contain the curated spec/research
  entries (no seed rows).
- [ ] Open questions: none — the single runtime unknown (`meta.rawPath`
  value) is closed by smoke step 8.2 before lyrics work is declared done.
