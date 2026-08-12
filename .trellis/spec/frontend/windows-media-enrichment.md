# Windows Media Enrichment

## 1. Scope / Trigger

Apply this contract when changing Windows cover-art delivery or the ESLyric
bridge. The feature is lazy: library import and playlist operations must not
fetch cover bytes or lyrics. macOS behavior and a native lyrics panel remain
out of scope.

## 2. Signatures

```cpp
std::string resolveArtId(const std::string& path);
std::string buildCoverArtUrl(const std::string& serverUrl,
    const std::string& username, const std::string& password,
    const std::string& salt, const std::string& coverId, int size = 0);

SubsonicClientWin::BinaryFetchResult SubsonicClientWin::httpGetBinary(
    const SubsonicRequestContext& context, const std::string& url,
    std::size_t maxBytes, abort_callback& abort) const;

std::string EsLyricBridge::installOrUpdate(
    const SubsonicRequestContext& context, bool debug = false);
```

The foobar service is an `album_art_extractor`; `open()` returns
`album_art_extractor_instance_ptr`, and the instance implements
`album_art_extractor_instance_v2::query()` plus `query_paths()`.

## 3. Contracts

- Art ownership matches `navidrome://` and legacy `/rest/stream.view` paths.
- Art-id priority is `coverArt` query, `id` query, then the
  `navidrome://track/<id>` segment. Percent-decode exactly once; a literal `+`
  is not a space.
- One immutable `SubsonicRequestContext` must build the cover URL, supply
  custom headers, and form the cache identity for a query.
- Resolve/connect/send/receive WinHTTP timeouts are all finite. Abort is
  checked before network work and between read chunks; synchronous calls may
  remain blocked only until their configured timeout.
- Cache only validated successful bodies. The process-only LRU is bounded by
  32 entries and 48 MiB, keyed by length-framed normalized server URL,
  username, and cover id. Never put credentials in the key.
- `Windows/EsLyricScript.h` is the single searcher source. Do not add a second
  resource or `.js` copy.
- Convert the foobar profile URI with `core_api::pathInProfile()` and
  `filesystem::g_get_native_path()` before using UTF-16 Win32 file APIs.
- Generate `config.js` with normalized URL, username, MD5 token, salt, custom
  headers, and `debug:false`. Never persist the raw password. Write unchanged
  content as a no-op; replace changed files with a same-directory temporary
  file and `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- The searcher prefers `getLyricsBySongId.view`. Only an HTTP 404 marks the
  server unsupported and triggers legacy `getLyrics.view`; that verdict is
  cached in module memory. Do not fallback on auth, transport, 5xx, malformed,
  or empty successful responses.
- Synced lyrics become LRC using `max(0, start - offset)`; unsynced lines are
  joined with newlines. Set title/artist/album/lyricText, but never `location`.

## 4. Validation & Error Matrix

| Condition | Result |
|---|---|
| HTTP 200 + image magic or image MIME | `FetchClass::Ok` |
| HTTP 200 + Subsonic code 70 | `NotFound`, silent |
| HTTP 401/403 or Subsonic 40/41/44 | `Auth` |
| HTTP 404/410 | `NotFound`, silent |
| HTTP 5xx or other Subsonic error | `ServerError` |
| WinHTTP failure | `Transport`; never classify/cache a partial body |
| Empty, oversized, or non-image body | `InvalidContent` |
| foobar abort | `Aborted` -> `exception_aborted` |
| ESLyric directory absent | Skip bridge write; one startup info message |
| Bridge write failure | Localized console error; keep old destination file |

## 5. Good / Base / Bad Cases

- Good: a `navidrome://` item resolves a percent-encoded cover id once, uses
  one request snapshot, returns cached cover bytes on the second query, and
  offers each structured lyric as a separate ESLyric candidate.
- Base: no cover or lyrics is a quiet not-found result; imports continue
  without enrichment requests.
- Bad: an authenticated HTML/JSON error body, partial read, oversized image,
  or non-404 lyrics failure must not be cached, displayed, or sent to the
  legacy endpoint.

## 6. Tests Required

- `MediaEnrichmentTests`: id priority/decode-once, UTF-8/reserved characters,
  auth URL token vector, HTTP/body/error classification, size boundary, cache
  identity/LRU/framing, config escaping/stability/no-password invariant.
- `LibraryImportTests`: existing import tests remain zero-failed to prove
  enrichment stays lazy and does not change playlist behavior.
- Parse vcxproj XML, strict-decode changed sources as UTF-8, run
  `git diff --check`, and build `Release|x64` against the SDK sibling layout.
- Package exactly `x64/foo_navidrome.dll`; inner and standalone SHA-256 must
  match.
- Host smoke: bridge files appear after startup/restart, a `navidrome://`
  selection displays cover art, and real synced/plain/no-lyrics tracks are
  checked in ESLyric.

## 7. Wrong vs Correct

Wrong:

```cpp
auto url = SubsonicClientWin::get().coverArtURL(id); // takes a new snapshot
DeleteFileW(target.c_str());
MoveFileW(temp.c_str(), target.c_str());
```

Correct:

```cpp
auto url = SubsonicClientWin::get().coverArtURL(context, id);
MoveFileExW(temp.c_str(), target.c_str(),
    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
```

The correct form keeps URL/header/cache identity coherent and leaves the old
bridge file intact if replacement fails.
