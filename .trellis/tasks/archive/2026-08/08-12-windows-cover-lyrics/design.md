# Windows Cover Art & ESLyric Lyrics — Technical Design

## 1. Architecture and Boundaries

Two independent delivery paths share one new pure-logic module:

```text
Cover art (C++):
  foobar2000 art viewer
    -> NavidromeArtExtractor (album_art_extractor, replaces album_art_fallback)
    -> MediaEnrichmentLogic  (id resolution, classification, cache policy — SDK-free)
    -> SubsonicClientWin::httpGetBinary (WinHTTP, timeouts, abort, size cap)
    -> in-memory LRU cover cache

Lyrics (QuickJS script inside ESLyric):
  ESLyric search (playback start / manual)
    -> eslyric-data\scripts\searcher\navidrome.js   (static, our original code)
    -> import of eslyric-data\scripts\lib\foo_navidrome\config.js (generated)
    -> ESLyric request() -> Navidrome getLyricsBySongId / legacy getLyrics
    -> man.addLyric(LRC or plain text)

Bridge maintenance (C++):
  component init (initquit)  \
  prefs apply()               > EsLyricBridge::refresh() -> write script+config
  headers window OnSave      /
```

Boundaries:

- `MediaEnrichmentLogic` (new `Windows/MediaEnrichmentLogic.h/.cpp`) holds all
  decision logic that must be deterministic-testable: URI/id handling,
  response classification, image sniffing, cache keying/eviction, bridge file
  content generation (JS escaping). No SDK, no WinHTTP includes
  (`NAVIDROME_ENRICHMENT_PURE_TEST`-style isolation is not needed — the module
  is SDK-free by construction).
- `SubsonicClientWin` gains only the binary transport (`httpGetBinary`) —
  no lyrics endpoints (decision D2: lyric fetching lives in the script).
- `NavidromePluginWin.cpp` keeps service registration; the fallback classes
  (`NavidromeArtInstance`, `NavidromeArtFallback`,
  `Windows/NavidromePluginWin.cpp:438-546`) are removed and replaced.
- `EsLyricBridge` (new `Windows/EsLyricBridge.h/.cpp`) owns bridge file
  paths, atomic writes, and refresh triggers. The searcher script source is a
  raw-string literal in `Windows/EsLyricScript.h` (single source of truth; no
  separate `.js` file in the repo to avoid drift).

## 2. Cover Art

### 2.1 Extractor registration

```cpp
class NavidromeArtExtractor : public album_art_extractor {
    bool is_our_path(const char* p, const char* ext) override;  // navidrome:// or /rest/stream.view
    album_art_extractor_instance_v2::ptr open(file_ptr, const char* path, abort_callback&) override;
};
```

- `is_our_path` mirrors the macOS matcher: prefix `navidrome://` OR substring
  `/rest/stream.view`. Returning true guarantees `open()` is called (macOS
  lesson — CLAUDE.md).
- `open()` resolves the art id via `MediaEnrichmentLogic::resolveArtId(path)`
  and returns an instance bound to (id, `SubsonicClientWin::snapshot()`).
  Missing id → `exception_album_art_not_found` immediately.
- Instance `query()`: only `album_art_ids::cover_front`; everything else →
  not-found. Flow: cache lookup → `coverArtURL(id, 0)` → `httpGetBinary` →
  classify → on success validate+cache+return, else map to
  `exception_album_art_not_found` (after one deduplicated console diagnostic
  for non-not-found classes).

### 2.2 Id resolution (decode exactly once)

```text
resolveArtId(path):
  1. value of "coverArt=" query param        (percent-decode once)
  2. else value of "id=" query param         (percent-decode once)
  3. else navidrome://track/<seg> path segment before '?' (percent-decode once)
  -> empty string when nothing found
```

The percent-decoder is the existing `uriDecode` semantics
(`Windows/NavidromeInputWin.cpp:42-58`); it moves into `MediaEnrichmentLogic`
and `NavidromeInputWin.cpp` re-uses it from there (single definition; input
behavior unchanged).

### 2.3 Hardened binary transport

```cpp
struct BinaryFetchResult {
    FetchClass cls;          // Ok | NotFound | Auth | ServerError | Transport | InvalidContent | Aborted
    DWORD      httpStatus;   // 0 when transport failed
    std::string contentType; // response header, may be empty
    std::string body;        // only meaningful when cls == Ok (or pre-validation)
};
BinaryFetchResult SubsonicClientWin::httpGetBinary(
    const SubsonicRequestContext& ctx, const std::string& url,
    std::size_t maxBytes, abort_callback& abort) const;
```

- Same WinHTTP setup as `httpGet` (`Windows/SubsonicClientWin.cpp:290-357`):
  `WinHttpSetTimeouts(hSess, 0, 15000, 15000, 30000)`, TLS 1.2/1.3, custom
  headers from the context.
- `abort.check()` before connect, before send, and at each
  `WinHttpQueryDataAvailable` iteration; on abort, close handles and return
  `Aborted` (mapped to `exception_aborted` by the caller). Worst-case blocking
  is bounded by the explicit timeouts.
- Read loop stops when accumulated size exceeds `maxBytes` (constant
  `kMaxCoverBytes = 20 MB`) → `InvalidContent`.
- Status mapping in `MediaEnrichmentLogic::classifyHttp(status)`:
  200 → candidate-Ok; 401/403 → Auth; 404/410 → NotFound; 5xx → ServerError;
  anything else → Transport. WinHTTP call failure → Transport.

### 2.4 Content validation (200 responses)

`MediaEnrichmentLogic::classifyBody(contentType, bytes)`:

- Accept when `contentType` starts with `image/` OR magic bytes match
  JPEG (`FF D8 FF`), PNG (`89 50 4E 47 0D 0A 1A 0A`), GIF (`GIF8`),
  BMP (`BM`), or WebP (`RIFF….WEBP`).
- Otherwise, if the body parses as a Subsonic error envelope (existing
  `jstr`-level scan is enough here — error code, not lyric text):
  code 70 → NotFound; 40/41/44 → Auth; else ServerError.
- Otherwise → InvalidContent.

### 2.5 Cover cache

- Key: `identityKey(serverUrl, username) + '\n' + coverArtId`, where
  `identityKey` reuses the normalization already used by the import state
  (lowercased scheme/host, trailing-slash-stripped URL + username) so both
  features agree on "same server".
- Value: image bytes. Success-only; no negative caching.
- Bounds: `kMaxCacheEntries = 32`, `kMaxCacheBytes = 48 MB`, LRU eviction;
  guarded by `std::mutex` (art queries arrive on worker threads).
- Process-lifetime only; never touches disk (C5).
- Console diagnostics for Auth/ServerError/Transport/InvalidContent are
  deduplicated per (class, id) for the session to avoid spam.

## 3. ESLyric Bridge (C++)

### 3.1 Files and paths

```text
<profile>\eslyric-data\scripts\searcher\navidrome.js        (static, versioned content)
<profile>\eslyric-data\scripts\lib\foo_navidrome\config.js  (regenerated)
```

- Profile directory resolution reuses the same mechanism as
  `LibraryImportState` (foobar profile path).
- Gate: write only when `<profile>\eslyric-data\` already exists (ESLyric
  creates it). The `scripts\searcher` / `scripts\lib\foo_navidrome`
  subdirectories are created if missing. When absent: skip, one console info
  line per session.
- Writes are atomic: write `*.tmp` in the target directory, `MoveFileExW(...,
  MOVEFILE_REPLACE_EXISTING)`. `navidrome.js` is rewritten only when its
  embedded content version marker differs from the file on disk (avoids mtime
  churn); `config.js` is rewritten whenever computed content differs.

### 3.2 Triggers

- `initquit::on_init` (new service in `NavidromePluginWin.cpp`) → refresh.
- `NavidromePrefsInstance::apply()` (`Windows/NavidromePluginWin.cpp:213`) →
  refresh after `saveSettings()`.
- `NavidromeHeadersWindow::OnSave` (`Windows/NavidromePluginWin.cpp:166-169`)
  → refresh.
- Refresh runs on the calling thread (file I/O only, no network).

### 3.3 config.js content

```js
// generated by foo_navidrome <version> — do not edit; regenerated on settings change
export const config = {
  serverUrl: "<normalized, no trailing slash>",
  username: "<escaped>",
  token: "<md5hex(password+salt)>",
  salt: "<escaped>",
  headers: { "CF-Access-Client-Id": "…", ... },   // parsed custom header lines
  debug: false,
};
```

- Token derivation is identical to `authParams`
  (`Windows/SubsonicClientWin.cpp:259-265`). The raw password never appears —
  generation code asserts the password substring is absent from the output
  (also unit-tested).
- JS string escaping (in `MediaEnrichmentLogic::jsEscape`): `\\`, `"`, `\n`,
  `\r`, `\t`, control chars < 0x20 as `\uXXXX`; UTF-8 passes through (file
  written as UTF-8, no BOM).
- `headers` come from `parseHeaderLines` (`SubsonicTypes.h:69-89`) split at
  the first `:`.

### 3.4 navidrome.js behavior (embedded in `Windows/EsLyricScript.h`)

```js
import { config } from '../lib/foo_navidrome/config.js';

export function getConfig(cfg) {
  cfg.name = 'Navidrome (foo_navidrome)';
  cfg.version = '<component version>';
  cfg.author = 'foo_navidrome';
  cfg.useRawMeta = false;
}

export function getLyrics(meta, man) { … }
```

Flow of `getLyrics` (all helpers are top-level pure functions for
reviewability):

1. `parseSongId(meta.rawPath)`: accept `navidrome://track/<id>` (tolerating a
   percent-encoded id, decode once; stop at `?`). Non-matching path (e.g.
   `meta.path` needed instead, or a legacy `/rest/stream.view` URL with `id=`
   param — also supported) → return silently. If `config.debug`, log the raw
   path once per session (smoke uses this to confirm the rawPath assumption —
   research risk (a)).
2. If `config.serverUrl`/`token` empty → return.
3. Structured attempt unless `unsupportedServers[config.serverUrl]` (module
   scope, session lifetime): GET
   `<serverUrl>/rest/getLyricsBySongId.view?u=&t=&s=&v=1.16.1&c=foo_navidrome&f=json&id=<enc(songId)>`
   with `{ timeout: 5000, headers: config.headers }`.
   - `err != 0` → return (transport; no fallback).
   - `statusCode 404` → mark server unsupported → proceed to step 5 (legacy).
   - `statusCode 401/403/5xx/other non-200` → return (no fallback).
   - 200 → `JSON.parse` in try/catch; parse failure → return (no fallback).
     Subsonic `status:"failed"` → treat code 70 as no-lyrics return; any other
     code → return.
4. Convert `lyricsList.structuredLyrics[]`:
   - Sort candidates: synced entries first.
   - Synced entry → LRC: for each line,
     `t = max(0, (line.start ?? 0) - (entry.offset ?? 0))` →
     `[mm:ss.xx]` + value (minutes may exceed 99 as `[mmm:ss.xx]`; centisecond
     precision, floor).
   - Unsynced entry → `line.value` array joined with `\n`.
   - For each entry: `l = man.createLyric(); l.title = meta.title;
     l.artist = meta.artist; l.album = meta.album; l.lyricText = text;
     man.addLyric(l);` — `location` deliberately NOT set (auth-in-URL leak,
     PRD L6). `man.checkAbort()` between entries.
   - Empty/absent array → return (normal no-lyrics, L4).
5. Legacy path (only via 404 above): GET `getLyrics.view` with
   `artist=enc(meta.rawArtist || meta.artist)`, `title=enc(meta.rawTitle ||
   meta.title)` + auth + `f=json`, same timeout/headers/error handling; a
   `lyrics.value` non-empty → one plain-text candidate; anything else →
   return.

Notes:

- `encodeURIComponent` handles query encoding in-script.
- No `setSvcData` usage (persistent store would go stale across server
  changes; module state resets per session — matches L3).
- The script never logs `config.token`/`salt`/header values, `debug` included
  (S2).

## 4. Compatibility

- `NavidromeInputWin` behavior is unchanged; it only re-uses the relocated
  decode helpers. URI format, metadata fields, and decode flow stay identical
  (S1).
- Legacy `/rest/stream.view` playlist items keep art via the same extractor
  (C1/AC2); they also get lyrics via the script's `id=` param path when
  played.
- macOS sources untouched. `SubsonicTypes.h` untouched (no shared-type
  changes needed).
- vcxproj: new `.cpp/.h` files added to the existing three platform configs
  (Win32/x64/ARM64EC inherit the shared item group); `/utf-8` stays global.
  The new test project is `Release|x64`-only like `LibraryImportTests`.

## 5. Trade-offs

- **Script-side fetch vs C++ fetch**: chosen for native ESLyric lifecycle
  (lazy, cancellable, cached by ESLyric's own save/search settings), strict
  `JSON.parse`, and zero IPC. Cost: the conversion logic is not covered by
  the C++ deterministic tests (mitigated by pure-function script structure +
  AC13 smoke; PRD D6).
- **Token+salt on disk** (config.js) vs no-lyrics: accepted — same profile
  directory already holds the raw password in foobar's own config (PRD D3).
  Changing the salt in preferences rotates the token.
- **Offset baked into timestamps** vs `[offset:]` header: ESLyric's header
  support is only indirectly evidenced (research §5.5) — baking is
  player-agnostic and exact.
- **Restart-required pickup** after install/credential change: module-import
  caching is unverified; we document restart rather than building fragile
  hot-reload guesses (PRD Known Limitations).

## 6. Rollout / Rollback

- All changes are additive and Windows-only. Rollback = revert the extractor
  registration to the previous fallback classes and stop writing bridge
  files; already-written `navidrome.js`/`config.js` are inert without the
  component (they only fail closed — script returns when config is missing)
  and can be deleted manually.
- No persistent state formats are introduced (cover cache is memory-only;
  bridge files are regenerated blobs), so no migration concerns.
