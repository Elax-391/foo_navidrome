# Windows Cover Art and Lyrics Support (via ESLyric)

## Goal

Make Navidrome tracks added by the Windows component expose working front-cover
art in foobar2000's album-art viewers, and deliver server-provided lyrics
(synchronized and plain) to the **ESLyric** component for display — without
changing the existing browsing, incremental import, or streaming behavior.

User value: playback on Windows gains the same "native" cover experience the
macOS build already has, plus lyrics sourced from the user's own Navidrome
server rendered by the lyrics component the user already runs.

## Key Decisions

- **D1 (user, 2026-08-12): lyrics presentation targets ESLyric.** This plugin
  does not implement its own lyrics panel.
- **D2: lyric fetching lives in an ESLyric searcher script, not in C++.**
  ESLyric's designed extension point is a QuickJS ES-module lyric source; the
  script receives the playing track (including its `navidrome://` URI via
  `meta.rawPath`), performs the Subsonic HTTP calls itself via ESLyric's
  `request()`, and returns lyrics through `man.addLyric()`. Consequences: JSON
  parsing uses the engine's strict `JSON.parse` (no hand-written C++ JSON for
  lyrics), fetches are naturally lazy (ESLyric searches only on playback
  start / manual search), and the C++ side only generates bridge files.
- **D3: credentials reach the script via a generated config module.** ESLyric
  scripts have no filesystem access and no per-source settings UI, so
  foo_navidrome generates `eslyric-data\scripts\lib\foo_navidrome\config.js`
  (normalized server URL, username, **md5 token + salt — never the raw
  password**, custom header lines) and installs a static original searcher
  `eslyric-data\scripts\searcher\navidrome.js` that imports it. Risk note: the
  token+salt pair is credential-equivalent for the Subsonic API, but foobar2000
  already persists the raw password in plaintext in the same profile directory
  (`cfg_string`), so this does not weaken the existing threat model.
- **D4: cover art becomes a full `album_art_extractor`** (path-owning,
  `is_our_path` = `navidrome://` or legacy `/rest/stream.view`), replacing the
  current `album_art_fallback` — the same reliability decision already proven
  on macOS (`NavidromeArtExtractor.mm`).
- **D5: structured-lyrics `offset` is baked into per-line LRC timestamps**
  (`effective = max(0, start - offset)`) instead of emitting an `[offset:]`
  header, because ESLyric's `[offset:]` support is only indirectly evidenced.
- **D6: deterministic-test split.** C++ unit tests cover everything that runs
  in C++ (id decoding, URL construction, response classification, cache
  identity/limits, bridge-file generation and escaping). The QuickJS script
  logic is verified by the real-host smoke (AC13) plus review; no Node/QuickJS
  test dependency is added to the toolchain.

## Background and Confirmed Facts

### Current Windows code defects (verified in source)

- Cover art is registered as `album_art_fallback`
  (`Windows/NavidromePluginWin.cpp:519-546`), not as a path-owning extractor —
  the macOS build moved to a full `album_art_extractor` precisely because the
  fallback path is unreliable for streamed content (see CLAUDE.md gotcha).
- The cover-art id is **double-encoded**: `urlParam()` extracts the
  still-percent-encoded `coverArt` value from the URI without decoding
  (`Windows/NavidromePluginWin.cpp:429-436,532-533`), and `coverArtURL()`
  percent-encodes it again (`Windows/SubsonicClientWin.cpp:582-587`). It only
  works today because typical Navidrome ids are alphanumeric. `makeTrackURI`
  writes the param percent-encoded (`Windows/NavidromeInputWin.cpp:214`).
- **Correction of an earlier planning claim**: the WinHTTP request does NOT
  lose the URL query. MSDN (WinHttpCrackUrl remarks): "When lpszExtraInfo is
  not set or dwExtraInfoLength is left as 0 …, if lpszUrlPath is set the query
  and/or fragment component of the URL will be included in that field." This
  matches the empirical evidence that every API call in the shipped component
  uses the same crack/open pattern (`Windows/SubsonicClientWin.cpp:290-317`)
  and authenticates fine.
- The art fetch (`Windows/NavidromePluginWin.cpp:438-517`) has real transport
  defects: no `WinHttpSetTimeouts` call (the API client sets 15s/15s/30s at
  `Windows/SubsonicClientWin.cpp:309`), no `abort_callback` wiring, no
  response-size cap, no content validation (a Subsonic JSON error body would be
  returned as "image" bytes), no cache, all failures collapse into a silent
  not-found, and lines 447-450 are dead placeholder code.
- Windows has no lyrics API surface (`Windows/SubsonicClientWin.h:27-50`), and
  the hand-written JSON helpers do not decode JSON escapes or `\uXXXX`
  (`Windows/SubsonicClientWin.cpp:92-105,156-195`) — which is why lyric text
  must not be parsed by C++ (see D2).
- `ServerInfo.openSubsonic` is already parsed from `ping.view`
  (`Windows/SubsonicClientWin.cpp:371-382`).

### External contracts (verified against primary sources)

- **OpenSubsonic `songLyrics` extension** [opensubsonic/open-subsonic-api,
  fetched 2026-08-12]: `getLyricsBySongId?id=<songId>` returns
  `lyricsList.structuredLyrics[]`; each entry requires `lang`, `synced`,
  `line[]` (each line: required `value`, optional `start` in ms — omitted when
  unsynced), optional `offset` (ms; positive = lyrics appear sooner, assume 0
  when absent), `displayArtist`, `displayTitle`. Without `enhanced=true` the
  response contains only v1 line-level data (no karaoke/translation layers).
  **HTTP 404 = extension not supported.** Absent/empty `structuredLyrics` on a
  200 response = successful no-lyrics result.
- **Legacy `getLyrics?artist=&title=`** returns `lyrics{value*, artist,
  title}`; missing `value` = no lyrics.
- **ESLyric integration surface** — full findings with citations in
  [research/eslyric-integration.md](research/eslyric-integration.md)
  (authoritative for implementation). Load-bearing facts: ESLyric
  (`foo_uie_eslyric`, by ohyeah, closed-source freeware, current 1.0.6.7,
  fb2k v2 x64 verified) runs searcher scripts as QuickJS-ng ES modules from
  `<fb2k profile>\eslyric-data\scripts\searcher\`; a script exports
  `getConfig(cfg)` and `getLyrics(meta, man)`; `meta.rawPath` carries the
  track's full path (expected: our `navidrome://track/<id>?...` URI — needs
  one runtime confirmation); network via global
  `request({url, headers, timeout}, (err, res, body) => …)`; lyrics are
  returned by `man.createLyric()` → `lyricText` (LRC timestamps auto-detected;
  plain text = unsynced) → `man.addLyric()`; the lyric object must echo
  `meta.title`/`meta.artist` to survive ESLyric's match-threshold filter;
  scripts have **no filesystem access and no settings UI**; module imports
  from `scripts\lib\` are exercised by official scripts; script pickup after
  file drop is assumed to require a foobar2000 restart (unverified); no
  Navidrome/Subsonic ESLyric source exists anywhere yet (greenfield).

## Requirements

### Cover Art (C — implemented in C++)

- C1. Register a Windows `album_art_extractor` whose `is_our_path` matches
  `navidrome://` URIs and legacy `/rest/stream.view` HTTP URLs already present
  in old playlists; remove the `album_art_fallback` registration it replaces.
- C2. Fetch `cover_front` bytes from `getCoverArt.view` built by
  `coverArtURL()` with token authentication and the configured custom request
  headers, using one immutable request context per query.
- C3. Resolve the art id in priority order `coverArt=` param → `id=` param →
  `navidrome://track/<id>` path segment, percent-decoding **exactly once**;
  UTF-8 and URL-reserved characters in ids must survive round-trip.
- C4. Apply explicit connect/send/receive timeouts, honor foobar's
  `abort_callback` (checked at least before send and between read chunks), and
  enforce a maximum accepted response size; a cancelled or timed-out fetch
  must not leave a blocked worker beyond the configured timeouts.
- C5. Cache successful cover bytes in memory, keyed by normalized server
  identity (normalized server URL + username, same normalization as the
  library-import state) + cover id, with bounded entry count and total bytes
  (LRU eviction). No disk persistence; no credential material in keys or
  values.
- C6. Classify outcomes distinguishably: success / not-found (normal, silent) /
  auth failure / server error / transport error / invalid content (non-image,
  oversized, or Subsonic JSON error body). Non-not-found failures emit one
  console diagnostic (no secrets); all failures surface to foobar as
  art-not-found.

### Lyrics via ESLyric (L — implemented in the searcher script unless noted)

- L1. Lyrics are fetched only when ESLyric searches (playback start / manual
  search). Library import and playlist operations must not trigger any lyric
  request (the script is simply not invoked outside ESLyric searches).
- L2. The script prefers `getLyricsBySongId.view?id=<songId>` (no
  `enhanced=true`), extracting the song id from the `navidrome://track/<id>`
  URI in `meta.rawPath`. Synchronized entries are converted to standard LRC
  `[mm:ss.xx]` lines preserving each line's effective timing (offset baked in
  per D5); unsynced entries become plain text with line structure preserved.
  Every `structuredLyrics` entry is offered as its own candidate
  (synced-main first).
- L3. Fall back to legacy `getLyrics.view?artist=&title=` **only** when the
  structured endpoint is explicitly unsupported (HTTP 404, per OpenSubsonic).
  Auth failures, HTTP 5xx, transport errors (`err != 0`), and malformed bodies
  must not trigger the fallback. The unsupported verdict is remembered per
  server URL for the foobar2000 session (module-level state, not svcData).
- L4. A 200 response with absent/empty `structuredLyrics` (or missing legacy
  `value`) is a normal no-lyrics result: the script returns quickly without
  adding candidates and without error noise.
- L5. All response parsing uses the engine's native `JSON.parse`; newlines,
  quotes, backslashes, Unicode escapes, Chinese text, and emoji in lyric text
  must survive to display. No hand-rolled JSON parsing anywhere in the lyrics
  path.
- L6. Candidates are delivered via `man.createLyric()` / `man.addLyric()` with
  `title`/`artist` echoed from `meta` (match-threshold requirement) and
  `lyricText` set. **`location` must not be set to the authenticated API URL**
  (it would leak token/salt into ESLyric's UI and saved data); leave it unset.
  `man.checkAbort()` is honored between network calls. No tag or file
  write-back toward Navidrome.
- L7. Each `request()` carries a bounded timeout (5000 ms) and the configured
  custom header lines (Cloudflare Access support), mirroring the C++ client's
  header behavior.
- L8 (C++). foo_navidrome generates the bridge: a static original
  `navidrome.js` searcher and a regenerated
  `scripts\lib\foo_navidrome\config.js` (normalized server URL, username,
  token = md5(password+salt), salt, custom header lines, `debug:false`).
  Written on component init and on every settings apply / custom-headers save;
  only when `<profile>\eslyric-data\` exists (creating the `scripts\searcher`
  / `scripts\lib\foo_navidrome` subdirectories as needed); atomically
  (temp file + rename); values JS-string-escaped; never containing the raw
  password. When `eslyric-data` is absent, skip silently except one console
  info line.

### Compatibility and Safety (S)

- S1. Existing title/artist/album/duration metadata, cover-art id embedding,
  streaming, single-item add/play, `添加全部`, and `完整核对` behavior must
  remain unchanged (contract:
  [windows-library-import.md](../../spec/frontend/windows-library-import.md)).
- S2. Credentials, salts, tokens, custom header values, cover bytes, and lyric
  text must not be written to diagnostic logs — by the C++ side or by the
  script (`console.log`).
- S3. The feature is Windows-only. macOS sources and behavior are out of
  scope; shared files may only change in ways that keep macOS builds
  identical.
- S4. The deliverable remains a Windows x64 `.fb2k-component` package and raw
  DLL, built against the existing foobar2000 SDK sibling layout. New
  user-visible strings follow
  [windows-localization.md](../../spec/frontend/windows-localization.md).

## Acceptance Criteria

- [ ] AC1: A current `navidrome://` playlist item with a valid cover id shows
  its front cover in a foobar2000 album-art viewer.
- [ ] AC2: A legacy `/rest/stream.view` playlist item still resolves its cover
  from its `coverArt`/`id` query params.
- [ ] AC3: Unit tests prove the cover request URL contains the auth query and
  a cover id encoded exactly once, for ids containing UTF-8 and URL-reserved
  characters; a captured live request confirms custom headers ride along.
- [ ] AC4: Repeated queries for the same server+cover id hit the in-memory
  cache (verified by unit test on the cache and by at-most-one live fetch per
  id in smoke); abort during fetch returns promptly.
- [ ] AC5: Unit tests classify non-image bodies, oversized bodies, 401/403,
  404, 5xx, and Subsonic JSON error bodies (code 70 → not-found; 40/41 → auth)
  into the correct result classes.
- [ ] AC6: A track whose Navidrome entry has synced lyrics shows scrolling
  timestamped lyrics in ESLyric; timestamps reflect `start` (and `offset` when
  present) baked into LRC.
- [ ] AC7: A track with only unsynced/legacy lyrics shows plain text with
  paragraphs, Chinese characters, escaped characters, and emoji intact.
- [ ] AC8: Against a server without `songLyrics` (HTTP 404), the script issues
  exactly one legacy request per search; simulated auth/5xx/transport/parse
  failures produce no fallback request (verified by script code review + smoke
  against Navidrome, plus server-log inspection where available).
- [ ] AC9: A supported server returning empty `structuredLyrics` yields a
  clean no-lyrics state in ESLyric with no error and no retry storm.
- [ ] AC10: `添加全部` of the full library issues zero `getCoverArt`/lyrics
  requests (covers/lyrics stay lazy).
- [ ] AC11: Existing `LibraryImportTests` still pass 0-failed; new
  deterministic tests cover art-id resolution/decoding, cover URL
  construction, response classification, image sniffing, size caps, cache
  identity normalization + LRU eviction, and bridge `config.js` generation
  (escaping, token derivation, no-password invariant) — 0 failed.
- [ ] AC12: `Release|x64` builds clean; the package contains exactly
  `x64/foo_navidrome.dll`; inner and standalone DLL SHA-256 match.
- [ ] AC13: Real-host smoke with ESLyric installed: bridge files appear under
  `eslyric-data\scripts\`; `meta.rawPath` is confirmed to carry the
  `navidrome://` URI (debug log, once); synced + plain + no-lyrics tracks
  behave per AC6/AC7/AC9; cover display per AC1/AC2; credential rotation +
  foobar restart picks up the new config.

## Known Limitations (accepted)

- Credential/server changes take effect for lyrics after a foobar2000 restart
  (ESLyric module-import caching is unverified; restart is the safe
  assumption).
- Users without ESLyric installed get cover art only; no bridge files are
  written for them.
- Self-signed-TLS behavior of ESLyric's `request()` is untested; plain-HTTP or
  valid-cert servers are the supported paths (matches the component's own
  WinHTTP posture).
- ESLyric versions predating the 1.0.6.x `request()` HTTP/2 support may fail
  against HTTP/2-only reverse proxies; document ESLyric ≥ 1.0.6.x.

## Out of Scope

- A native lyrics panel inside foo_navidrome; automatic ESLyric installation.
- Editing lyrics or writing lyrics/tags back to Navidrome or media files.
- OpenSubsonic enhanced lyrics v2 (word-level karaoke, translation layers,
  agents).
- Automatic online lyric-provider search outside Navidrome (ESLyric's other
  sources handle that independently).
- Changes to Navidrome server indexing or source media files; macOS feature
  changes.
