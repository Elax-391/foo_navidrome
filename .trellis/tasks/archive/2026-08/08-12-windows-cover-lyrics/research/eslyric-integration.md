# Research: ESLyric integration surface for foo_navidrome (server-provided lyrics)

- **Query**: External integration surface of the ESLyric foobar2000 lyrics component, so foo_navidrome can deliver Navidrome/Subsonic server lyrics (synced LRC + plain) to ESLyric. Windows-only.
- **Scope**: external (web research; all claims verified against fetched primary pages)
- **Date**: 2026-08-12
- **Evidence cache**: fetched pages/scripts saved under `C:\tmp\smart-search-evidence\20260812-eslyric\` (wiki HTML/text, official searcher scripts, hydrogenaudio thread pages, locale file, issue pages)

## Executive summary

- ESLyric is a closed-source freeware foobar2000 lyrics component by **ohyeah**, Windows-only, current release **1.0.6.7** (2026-03-22), distributed via GitHub org `ESLyric` (`release` = binaries/wiki, `scripts` = official lyric-source scripts, `feedback` = issues). Runs on foobar2000 v2 x64 (community-verified through fb2k v2.26 x64 + ESLyric 1.0.6.x).
- It supports **user-provided JavaScript lyric sources** (ES modules run by the QuickJS-ng engine, `std`/`os` modules NOT exported — no filesystem access). A source exports `getConfig(cfg)` and `getLyrics(meta, man)`.
- **`meta` includes the playing track's path**: documented fields `rawPath` ("full path, e.g. `file://c:\path\to\track.mp3`") and `path` ("display path"). For our tracks this should surface the `navidrome://track/<id>?...` URI, letting the script extract the song id (custom-scheme value needs one runtime confirmation).
- Synced vs plain lyrics are both delivered as text via `lyricMeta.lyricText` (LRC timestamps auto-detected; duplicate-timestamp lines = translations). Network access is via a global `request(options, callback)` (headers/POST/timeout supported; HTTP/2 added in 1.0.6.x, 2026).
- Scripts **cannot read arbitrary local files**; per-source persistence exists (`man.getSvcData`/`setSvcData`) but there is **no per-source settings UI**. Community norm for auth/config is editing constants inside the script. => Recommended path: foo_navidrome generates/install an original searcher script plus a generated config `.js` (server URL + user + token) under `<profile>\eslyric-data\scripts\lib\`, which the searcher imports.
- Install dir for sources: `<fb2k profile>\eslyric-data\scripts\searcher` (`%APPDATA%\foobar2000-v2\...` for standard installs; `<install>\profile\...` for portable). Dropping a `.js` there works; ESLyric also has a "Get More → Import local script" UI. Automatic pickup timing (restart vs rescan) is unverified — plan for a restart requirement.
- No existing ESLyric source script for Navidrome/Subsonic/Airsonic/Jellyfin/Plex was found (GitHub repo search + web searches). Closest precedents: official `lrclib.js` (REST JSON API returning `syncedLyrics`/`plainLyrics`) and `musixmatch.js` (token caching via `setSvcData`).
- Fallback/alternative: ESLyric's internal **Tags** source reads embedded-lyrics tags (field names configurable by double-clicking the Tags source; default name unverified) and works off fb2k metadata, and internal **Local File** source searches configured directories. Save-to-tag will fail for unrecognized (custom-URI) paths ("Unrecognize path, cannot save to tag").

---

## Q1. Identity and baseline

| Item | Finding | Confidence |
|---|---|---|
| Component | ESLyric (`foo_uie_eslyric`), lyrics download + display for foobar2000, DUI + Columns UI | High |
| Author | **ohyeah** (hydrogenaudio user; GitHub org `ESLyric`). Script co-author "TT" appears in official scripts (`ohyeah & TT`) | High |
| Current version | **1.0.6.7**, released 2026-03-22; asset `foo_uie_eslyric_1.0.6.7.fb2k-component` | High |
| Distribution | GitHub releases https://github.com/ESLyric/release/releases (linked from author's hydrogenaudio thread + HA knowledgebase). Presence on foobar2000.org components listing NOT verified | High (channel) |
| Platform | Windows-only: "为Windows平台音乐播放器foobar2000提供歌词下载与显示功能"; requires Windows 10 1607+, foobar2000 v1.5+ | High |
| fb2k v2 x64 | Works: "working fine on Foobar v2 beta 4 x64" (2022-09-07); "Thanks again for porting ESLyric to 64-bit"; ESLyric 1.0.6.1 in use with "Foobar v2.26 x64 2025-12-17 preview" (2025-12-25) | High |
| License | **No license file** in `ESLyric/release` (repo holds only README/layout/locale; code is not published => closed-source freeware) nor in `ESLyric/scripts` (probed LICENSE/LICENSE.md/LICENSE.txt => HTTP 404). Default all-rights-reserved applies to their scripts | High (absence verified) |
| Implication for us | We may ship our **own original** source script targeting the documented API and install it into the user's profile; we must NOT bundle/redistribute ESLyric itself nor copy their script code verbatim | Medium (legal reasoning, not verified with author) |

Citations:
- [ESLyric release README, fetched 2026-08-12, https://github.com/ESLyric/release] — intro, features, system requirements (Chinese + English).
- [GitHub releases/latest redirect, 2026-08-12, https://github.com/ESLyric/release/releases/latest] → tag `1.0.6.7`, release datetime attribute 2026-03-22T13:28:53Z.
- [Hydrogenaudio Knowledgebase, page oldid=37497, https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Components/ESlyric_(foo_uie_eslyric)] — "Developer(s): ohyeah", initial release June 10 2022, min fb2k 1.6.12, DUI/Columns UI, "Scriptable lyric sources (powered by quickjs)". NOTE: stable-release field (0.5.4.1022, Oct 28 2023) is stale vs GitHub.
- [Hydrogenaudio thread "NEW ESLyric v0.5 - an alternative lyric show component", posts 2022-06-10 … 2026-04, https://hydrogenaud.io/index.php/topic,122571.0.html] — first post by ohyeah; x64/v2 posts as quoted above.

---

## Q2. Custom lyric-source scripting API (core)

Primary source: official wiki page **"歌词源及解析器脚本"** (Lyric sources & parser scripts) — https://github.com/ESLyric/release/wiki/%E6%AD%8C%E8%AF%8D%E6%BA%90%E5%8F%8A%E8%A7%A3%E6%9E%90%E5%99%A8%E8%84%9A%E6%9C%AC (fetched 2026-08-12). Cross-checked against official scripts in https://github.com/ESLyric/scripts. Confidence: **High** unless noted.

### 2.1 Engine and file format

- Wiki (verbatim): "ESLyric 使用了QuickJS-ng JavaScipt引擎提供自定义外部歌词源, 歌词解析器等功能，引擎中std及os模块未导出。" — QuickJS-ng engine; **`std` and `os` modules are NOT exported** => no filesystem/process access from scripts.
- Scripts are **ES modules**: they use `export function ...` and top-level `import` (e.g. `lrclib.js` line 1: `import { parse } from 'himalaya/src/index.js';`, resolved against `scripts\lib\`). Additionally a global `evalLib("querystring/querystring.min.js")` loads libraries relative to `scripts\lib\` (used by `musixmatch.js`).

### 2.2 Required exports (exact signatures, wiki verbatim)

```js
// 获取歌词源信息时调用
// cfg为IESLyricSearchServiceConfig对象
export function getConfig(cfg)
{
}

// 获取歌词时调用
// meta为IESLyricTrackMetadb对象
// man为IESLyricMetaInfoManager对象
export function getLyrics(meta, man)
{
}
```

`cfg` fields (interface `IESLyricSearchServiceConfig`): `name`, `author`, `version` (all r/w strings). Official example and every official script also set **`cfg.useRawMeta = false;`** — undocumented in the interface block but present in the wiki example; when true, presumably meta.title/artist/album skip ESLyric's search preprocessing (Confidence: Medium for semantics, High for existence).

### 2.3 `meta` object — full field list (interface `IESLyricTrackMetadb`, wiki verbatim)

```
[r] string title;      // 音轨标题，若启用了搜索预处理，则该字段为预处理后的关键词，否则与rawTitle相同
[r] string artist;     // 音轨艺术家 (preprocessed if search-preprocessing enabled)
[r] string album;      // 音轨专辑 (preprocessed if enabled)
[r] string rawTitle;   // 未经过预处理的音轨标题
[r] string rawArtist;  // 未经过预处理的音轨艺术家
[r] string rawAlbum;   // 未经过预处理的音轨专辑
[r] string rawPath;    // 完整路径，如file://c:\path\to\track.mp3
[r] string path;       // 显示路径，如c:\path\to\track.mp3
[r] int subSong;       // 子音轨索引
[r] double duration;   // 音轨长度 (seconds; lrclib.js uses Math.ceil(meta.duration))
```

- **CRITICAL for us: `rawPath`/`path` exist and carry the track's full path.** fb2k represents local files as `file://C:\...`; for a custom input the fb2k path IS the URI, so `rawPath` should be `navidrome://track/<id>?...` for our tracks (and `path` the display form). The documented example only shows `file://`; the custom-scheme value is an inference from fb2k path semantics — **verify once at runtime** (log `meta.rawPath` from a test script). Confidence: High that the field exists / Medium for exact value with `navidrome://` scheme.
- There is NO documented genre/filesize/samplerate/playback-state field. (An earlier LLM search answer listing such fields was hallucinated; the list above is the authoritative one.)
- Evidence that scripts do run for non-local tracks: internet-radio streams are searched by sources using stream title/artist (HA thread: metallum.js mod "the script uses only artist name and track title to search, so radio streams are searched also", 2026; radio-stream discussion 2022-10).

### 2.4 Manager `man` (interface `IESLyricMetaInfoManager`, wiki verbatim + real usage)

```
IESLyricMetaInfo createLyric(); // 创建一个新的歌词描述信息
void addLyric(IESLyricMetaInfo newLyric); // 添加歌词至ESLyric
bool checkAbort();              // 用户是否取消搜索，是则返回true
string getSvcData(string key);  // 获取歌词源相关配置
void setSvcData(string key);    // 设置歌词源相关配置，key在同一歌词源内唯一即可
```

- The `setSvcData` doc signature is a **typo**; real usage is two-arg: `man.setSvcData('token', token); man.setSvcData('lastTokenUpdated', new Date().toUTCString());` (`musixmatch.js` lines 183-184) and `let token = man.getSvcData('token');` (line 143). This is a per-source persistent key/value store, script-writable only (no UI). [https://github.com/ESLyric/scripts/blob/main/searcher/musixmatch.js]
- `man.checkAbort()` used in `musixmatch.js` line 76 to bail out of result loops.

### 2.5 Lyric object (interface `IESLyricMetaInfo`, wiki verbatim, translated)

```
[r,w] string title;        // required; participates in match scoring
[r,w] string artist;       // recommended; participates in match scoring
[r,w] string album;        // optional, shown in search UI
[r,w] string lyricText;    // lyric TEXT — one of lyricText/lyricData is required
[r,w] ArrayBuffer lyricData; // binary lyric data (for formats needing an external parser)
[r,w] string source;       // optional; auto-filled with the source name
[r,w] string fileType;     // default "lrc"; set for other formats (requires a matching parser)
[r,w] string location;     // optional (e.g. source URL; lrclib.js sets it to the API URL)
[r,w] bool isLocal;        // optional, default false
[r,w] int confidence;      // [0,100] optional; by default ESLyric fuzzy-matches title+artist
                           // and hides results below the user's minimum-match threshold
```

- **Synced vs plain**: both go through `lyricText`. `lrclib.js` picks `json.syncedLyrics` if non-empty else `json.plainLyrics` and assigns the string to `lyricMeta.lyricText` — LRC timestamps are auto-detected; plain text displays unsynced. No separate "synced" flag exists on the lyric object.
- **Match-filter gotcha**: results are filtered by title/artist similarity vs the user's minimum-match setting (also noted by Robotxm README: match-degree logic applies even with "show all lyrics" checked — https://github.com/Robotxm/ESLyric-LyricsSource). Official scripts therefore echo the track's own metadata back: `lyricMeta.title = meta.title; lyricMeta.artist = meta.artist;` — our script should do the same so server-returned lyrics always score 100%.

### 2.6 Network API (wiki verbatim, comments)

```
// 全局函数 — 发起http请求
// options: string URL, or settings object:
// { string url; string method /*"get"|"post"*/; string body; object headers; bool raw; }
// callback: void callback(int err, IHttpResponse rsp, string body)
void request(object options, func callback);

interface IHttpResponse {
   [r] int statusCode; [r] string statusMessage; [r] object headers;
}
```

- `timeout` (ms) is supported in the settings object though undocumented: `const settings = { url, timeout: 5000 };` (official `lrclib.js` line 23, `genius.js` line 23). Errors surface as nonzero `err` / non-200 `statusCode`; official scripts just `return` on failure.
- Callbacks complete before the surrounding code continues in official usage (`musixmatch.js` `queryToken()` reads the token right after `request(...)` returns) => effectively synchronous/blocking semantics inside `getLyrics`. Confidence: Medium (inferred from official script control flow).
- **HTTP/2 was added in the 1.0.6.x line (2026)** after metal-archives began rejecting HTTP/1.1 ("The metallum.js searcher script now works fine thanks to http/2.0 implementation!", HA thread 2026). Older ESLyric versions can't reach HTTP/2-only servers.
- Other helpers available to scripts: `console.log` (to fb2k console), `mxml` (minixml XML parser — useful for Subsonic XML, though JSON via `f=json` is easier), `zlib.compress/uncompress` (needs fb2k's `zlib1.dll`), `atob`/`btoa`, `arrayBufferToString`/`stringToArrayBuffer(utf8)`, `utils.createProfiler()`, `evalLib(relPath)`.

### 2.7 Local files / per-source settings / how our auth reaches the script

- **No filesystem read API** in current scripts (std/os not exported). Contrast: the legacy 0.3.x API was ActiveX-based (`new ActiveXObject("Scripting.FileSystemObject")`, `utils.ReadTextFile` — see https://github.com/Aeroblast/Load_Local_LRC_for_offVocals_in_ESLyrics, old API) — none of that exists in the QuickJS engine. Confidence: High.
- **No per-source settings UI.** Config surface = script code itself. Community norm is editing constants in the .js (e.g. Gemini source: "turns on with the addition of a line of code in ESLyric with a free API key" — HA thread 2026-01; https://github.com/shirafukayayoi/fb2k-ESLyric-LyricGeminiStrip). `get/setSvcData` persists values but only the script can write them.
- **Practical delivery of server URL + token from foo_navidrome**: our C++ component can WRITE a generated JS config module (e.g. `<profile>\eslyric-data\scripts\lib\foo_navidrome\config.js` exporting `{serverUrl, username, token, salt}`) and an original searcher script in `...\scripts\searcher\` that `import`s it (exactly how `lrclib.js` imports from lib). Regenerate the config file whenever credentials change. Whether ESLyric re-reads module imports without a restart is UNKNOWN — treat "restart foobar2000 after credential change" as the safe assumption, or re-resolve config per call if import caching proves stale.

### 2.8 Install directory + pickup

- Official wiki (verbatim): "歌词源脚本位置：**fb2k配置目录\eslyric-data\scripts\searcher**" (lyric-source scripts); "解析器脚本位置：eslyric安装文件目录\scripts\parser" (parsers).
- Community confirmations: "请将`searcher`文件夹和`lib`文件夹内的文件，分别放入`Foobar2000\profile\eslyric-data\scripts\`路径下对应文件夹中" [shimo-saki README, https://github.com/shimo-saki/ESLyric-LyricScript]; "..\profile\eslyric-data\scripts\lib\utils.js" and "You can also just dump the files into your searcher folder and they will work" [HA thread, sveakul, 2026-01]. For a standard (non-portable) fb2k v2 install the profile dir is `%APPDATA%\foobar2000-v2`, so: `%APPDATA%\foobar2000-v2\eslyric-data\scripts\searcher\`. Confidence: High.
- Alternative install: ESLyric Preferences → Lyric Sources → "Get More.." → **"Import local script"** (locale string 364) — validates the file ("Invalid or incompatible script file" on bad content; HA thread 2025-12). Enable advanced setting `pref.debug.log` to see script load errors in the fb2k console (ohyeah, HA thread 2025-12-28).
- **Pickup timing after dropping a file (restart vs automatic rescan): NOT verified** by any fetched source. Plan for "restart foobar2000 (or use Import local script)" until runtime testing says otherwise.

### 2.9 Verbatim example skeleton (official wiki)

From https://github.com/ESLyric/release/wiki/%E6%AD%8C%E8%AF%8D%E6%BA%90%E5%8F%8A%E8%A7%A3%E6%9E%90%E5%99%A8%E8%84%9A%E6%9C%AC ("歌词源示例"):

```js
export function getConfig(cfg)
{
    cfg.name = "My custom source";
    cfg.version = "0.1";
    cfg.author = "My name";
    cfg.useRawMeta = false;
}

export function getLyrics(meta, man)
{
    let lyricMeta = man.createLyric();
    // 从网络获取歌词
    request("https://example.com/", (err, res, body) => {
        if (!err && res.statusCode == 200) {
            // title与artist都参与搜索结果匹配
            // 根据用户设置的最小匹配度过滤
            // 搜索界面勾选显示"显示全部歌词时"将忽略过滤，显示全部正常解析的歌词
            lyricMeta.title = "title";
            lyricMeta.artist = "artist";
            lyricMeta.album = "album"; // 可选
            lyricMeta.lyricText = body; // 设置歌词文本
            man.addLyric(lyricMeta); // 添加至ESLyric
        }
    });
}
```

### 2.10 Verbatim real production source (official `lrclib.js`, closest analog to our REST case)

From https://github.com/ESLyric/scripts/blob/main/searcher/lrclib.js (raw: https://raw.githubusercontent.com/ESLyric/scripts/main/searcher/lrclib.js), core excerpt (Clean() body elided):

```js
import { parse } from 'himalaya/src/index.js';

export function getConfig(cfg) {
	cfg.name = 'LRCLIB (Mixed)';
	cfg.version = '0.1';
	cfg.author = 'TT';
	cfg.useRawMeta = false;
}

export function getLyrics(meta, man) {
	const artist = Clean(meta.artist);
	const album = Clean(meta.album);
	const title = Clean(meta.title);
	const duration = Math.ceil(meta.duration);
	const url = `https://lrclib.net/api/get?artist_name=${artist}&track_name=${title}&album_name=${album}&duration=${duration}`;
	const settings = { url,	timeout: 5000 };

	if (artist === '' || album === '' || title === '') return;

	request(settings, (err, res, body) => {
		if (err || res.statusCode !== 200) return;

		let lyricText = findLyrics(body);
		lyricText = parseLyrics(lyricText);

		const lyricMeta = man.createLyric();
		lyricMeta.title = meta.title;
		lyricMeta.album = meta.album;
		lyricMeta.artist = meta.artist;
		lyricMeta.lyricText = lyricText;
		lyricMeta.location = url;
		man.addLyric(lyricMeta);
	});
}

function findLyrics(content) {
	const json = JSON.parse(content);
	const lyrics = (json.syncedLyrics && json.syncedLyrics.trim()) || json.plainLyrics;
	// Check if syncedLyrics is a non-empty string, otherwise fall back to plainLyrics
	return lyrics.trim();
}
```

---

## Q3. Existing precedent for self-hosted music servers

**Result: none found.** No ESLyric source script exists for Navidrome, Subsonic, Airsonic, Jellyfin, Plex, or Music Assistant as of 2026-08-12. Confidence: Medium (negative claim; two independent search methods, but niche scripts on gists/forums could exist unindexed).

Methods:
- GitHub repository search for "ESLyric" (full result list via GitHub search API, 2026-08-12): ~20 repos, all target public lyric services (NetEase, QQ Music, Kugou, Baidu, ALSong, MiniLyrics proxy, Musixmatch/Gemini, VTT parsers, translations) — none server-oriented. [https://api.github.com/search/repositories?q=ESLyric]
- Web searches ("ESLyric navidrome", "ESLyric subsonic lyrics source", "ESLyric jellyfin script") via DuckDuckGo returned no ESLyric-related hits (only Navidrome's own web-UI lyrics plugin, unrelated).

Closest precedents for our two design problems:

1. **REST JSON lyric API with synced/plain fields** — official `lrclib.js` (see 2.10): one GET, `JSON.parse(body)`, prefer `syncedLyrics` else `plainLyrics`, assign to `lyricText`. Our Subsonic/OpenSubsonic call fits this shape exactly.
2. **Server address + auth configuration** — no script has a config UI. Patterns observed:
   - Hardcoded constants edited by the user in the script file (`fb2k-ESLyric-LyricGeminiStrip`: Gemini API key enabled "with the addition of a line of code"; Aeroblast legacy script: `var paths = ["E:\\Lyrics"];`).
   - Self-acquired tokens cached via `man.setSvcData/getSvcData` (`musixmatch.js` fetches its own usertoken from the Musixmatch API, then caches it). Not usable for externally-supplied credentials — nothing external can write svcData.
   - => For foo_navidrome the natural adaptation is: our C++ plugin owns the credentials and **generates** the script/config file content itself (no user editing), which no existing script does but the mechanism (import from `scripts\lib`) is exercised by official scripts.

---

## Q4. Tag/metadata-based alternative (internal sources)

ESLyric's built-in (non-script) sources and behavior — from the official en_US locale file [https://github.com/ESLyric/release/blob/main/locale/en_US.lng, fetched 2026-08-12] and the HA thread:

- **Internal sources**: "Local File" (string 123) and "Tags" (strings 119/124), alongside script sources. Settings groups: "Local File Search Settings" (25) with "Search filenames" (28), "Search directories" (292), "Search recursively" (314); "Tag Search Settings" (29) with **"Tag fields" (122)**; "Script Search Settings" (31).
- **Tags source reads embedded lyrics; tag field names are user-configurable**: "The plugin supports reading lyrics embedded in the file, which is enabled in the Preferences - Lyric Option - Lyrics Sources, and you can set the tag name by double-clicking the Tags lyrics source." [HA thread, always.beta, 2022-10-10]. Confidence: High (locale + forum agree).
- **Default tag field name(s): UNVERIFIED.** No fetched source states the defaults (commonly LYRICS/UNSYNCEDLYRICS in the fb2k ecosystem, but that is unconfirmed for ESLyric — check the UI at implementation time). Confidence: Unknown.
- **Would the Tags source work for our `navidrome://` tracks?** The Tags source reads fb2k metadata, and our input's `get_info()` populates fields parsed from the URI. If foo_navidrome added a `LYRICS` field to `get_info()` (fetched from the server), ESLyric could in principle pick it up — BUT (a) our metadata comes from URI query params (size-limited, lyrics don't fit in a URI), (b) fetching lyrics synchronously inside `get_info()` would block playlist rendering, and (c) whether the Tags source consults dynamic/late metadata is undocumented. => Tag route is a poor fit; script source is the right vehicle. Confidence: Medium (architecture reasoning on verified facts).
- **Search order / auto-search timing**:
  - "Automatically search on playback start" (locale 6) — auto-search triggers on playback start. High.
  - Sources are checked in user-configurable order; users reorder them ("I've moved azlyrics to being the last provider to be checked" [HA thread, sveakul, 2022-10]). High.
  - Early-exit options: "Skip external sources if local lyrics found during auto-search" (7), "Skip remaining sources if lyrics found during auto-search" (267), "Choose the best matched lyrics currently". High.
  - Search filters can exclude tracks/conditions: "Search filters exclude unwanted search attempts and only activate during automated lyric searches" (68), "Disable auto-search if any of the following conditions are met" (436). High.
  - **Re-search when metadata appears later**: internet-radio behavior shows ESLyric re-searches when the stream's title/artist changes (sources get the current dynamic title/artist), but the lyric TIMESTAMP base is not reset on stream track change (feedback issue #106, "Lyric text synchronization for radio stream tracks": "The time for timestamps is determined by the start time of the playback of the radio stream... not reset after changing the name"; dev reply: "It seems there is no way to sync progress, it may be more reasonable to display unsync lyrics." [https://github.com/ESLyric/feedback/issues/106]). For foo_navidrome tracks this is moot: our metadata is embedded in the URI and complete at playback start, and each track is a separate finite-duration playback. Confidence: Medium-High.
- **Saving lyrics**: "Manual save"/"Auto-save on load" (117/118), "Save when played 60s or 1/3" (300), "Auto save synced only"/"Auto save unsynced only" (311/312); save target file or tag. Error strings show tag-save limits: "Subindex exists, cannot save to tag." (331), **"Unrecognize path, cannot save to tag." (332)**, "File is read-only, cannot save to tag." (333). A `navidrome://` URI is almost certainly an "unrecognized path" for tag-writing (fb2k can't retag a remote stream), so users should save to file (default: profile `lyrics` folder as .lrc — HA thread 2022-11) or rely on re-fetching from our server each play. Confidence: Medium-High.

---

## Q5. Synced lyrics format ESLyric consumes

From the official wiki (parser section, "当前ESLyric支持的歌词格式" — formats ESLyric natively recognizes). Confidence: High (official doc).

1. **Standard LRC**:
   ```
   [00:00.00]line 1
   [00:01.00]line 2
   ```
   Timestamp shape `[mm:ss.xx]`. Navidrome/OpenSubsonic structured lyrics (`getLyricsBySongId` start-ms values) must be converted by our script to this shape (`[mm:ss.xx]`, 2-digit centiseconds is the documented example).
2. **Enhanced (word-level) LRC**: `[mm:ss.xx] <mm:ss.xx> word <mm:ss.xx> word ...` (karaoke). Not needed for Subsonic lyrics but harmless.
3. **Multi-line same-timestamp lyrics = translation/reference lines**: lines sharing one timestamp are grouped; "其中第一行将作为原始歌词" (first line is the original). Groups need not be contiguous. Useful if the server ever returns bilingual LRC.
4. **Plain text** (no timestamps) displays as unsynced lyrics; internal/auto-save logic distinguishes synced vs unsynced ("Auto save synced only"/"Auto save unsynced only" locale strings).
5. **`[offset:...]` tag**: not mentioned in the wiki format section, but two indirect evidences say ESLyric understands an offset header: locale action **"Apply Offset to Tag" (444)** and feedback issue #21 (user: ESLyric persists timing adjustment "前面加了offset改的时间，而不是改每一个时间点" — by prepending an offset rather than rewriting every timestamp; complaint is that OTHER players ignore it) [https://github.com/ESLyric/feedback/issues/21]. => ESLyric writes and honors an offset header itself. Confidence: Medium (indirect, 2 sources; verify with a test LRC).
6. **UTF-8**: script-delivered lyrics are JS strings (QuickJS strings; `stringToArrayBuffer` is explicitly "(utf8)"), so UTF-8 round-trips; official CJK sources (NetEase/QQ/Kugou) deliver CJK text through `lyricText` in production. No BOM/encoding caveats surfaced for the script path. Confidence: Medium-High. (File-based local .lrc encoding behavior was not researched.)

## Q6. Known pitfalls (ESLyric + fb2k v2 + custom sources)

1. **Match-threshold filtering can hide valid results** — results whose title/artist differ from the track's are filtered below the user's minimum match; even "show all lyrics" doesn't fully bypass it (Robotxm README). Mitigation: echo `meta.title/artist` into the lyric object (official scripts do this).
2. **Engine is QuickJS-ng, not Node/browser** — no `fetch`, no `XMLHttpRequest`, no `std`/`os`, no filesystem, no `ActiveXObject` (that was the legacy 0.3.x API). Only the documented globals (`request`, `mxml`, `zlib`, `evalLib`, `atob/btoa`, buffer<->string helpers, `console.log`). Scripts written for old ESLyric (`start_search(info, callback)`, `info.Title`) are incompatible ("Not guaranteed to be compatible with old versions" — scripts repo README; v0.5 "written from scratch, and not compatible with the old version(0.3.x)").
3. **HTTP/2-only endpoints fail on ESLyric < 1.0.6.x** (request() gained HTTP/2 in 2026 — metal-archives 403 saga, HA thread). Self-hosted Navidrome behind default reverse proxies still speaks HTTP/1.1, so low risk; document a minimum ESLyric version anyway.
4. **TLS with self-signed certificates**: behavior of `request()` against self-signed HTTPS (common for LAN Navidrome) is UNDOCUMENTED/untested — flag for runtime testing; users with plain-HTTP LAN servers are unaffected.
5. **Script install via browser "save link as" corrupts files** (users get HTML instead of JS => "Invalid or incompatible script file"; HA thread 2025-12). Not a risk for us (we write the file programmatically) but relevant for support docs.
6. **Save-to-tag cannot work for custom-URI tracks** ("Unrecognize path, cannot save to tag") — users should not enable tag-save expectations for navidrome:// tracks.
7. **Debugging**: advanced setting `pref.debug.log` prints script loading/errors to the fb2k console (author's own instruction, HA thread 2025-12-28); `console.log` works from inside sources.
8. **Wine/Linux**: ESLyric 1.x does not display lyrics under Wine (HA thread 2025-12) — irrelevant for our Windows-only feature but explains why testing must be on real Windows/VM.
9. **ARM64/ARM64EC**: no information found on ESLyric shipping ARM64EC binaries; on Windows-on-ARM foobar2000 it presumably runs via the x64 fallback. Our QEMU ARM64 test VM may therefore exercise ESLyric under x64 emulation only. Confidence: Unknown.
10. **Old wiki page staleness**: the Hydrogenaudio knowledgebase page lists 0.5.4.1022 as stable — always check GitHub releases for the real current version.

---

## Recommended integration sketch (synthesis)

1. foo_navidrome (C++, Windows) writes two files under the fb2k profile dir when credentials are set/changed:
   - `eslyric-data\scripts\lib\foo_navidrome\config.js` — generated ES module: `export const config = { serverUrl: "...", user: "...", token: "...", salt: "..." };` (md5 token auth params, same as SubsonicClientWin uses).
   - `eslyric-data\scripts\searcher\navidrome.js` — our original static searcher script: `getConfig` sets name "Navidrome (foo_navidrome)"; `getLyrics(meta, man)` checks `meta.rawPath` starts with `navidrome://track/`, extracts `<songId>` (strip query), calls OpenSubsonic `rest/getLyricsBySongId.view?id=<songId>&f=json&...auth` via `request({url, timeout})`, converts structured lines to `[mm:ss.xx]` LRC (or passes plain text), sets `lyricMeta.title/artist = meta.title/artist`, `lyricMeta.lyricText`, `man.addLyric`. Fallback to legacy `getLyrics.view?artist&title` for non-OpenSubsonic servers.
2. First install: create files + tell the user to restart foobar2000 (or import via ESLyric's "Import local script"). Credential rotation: regenerate `config.js`; verify at runtime whether ESLyric caches module imports (if yes, restart note).
3. Runtime verifications needed before coding against assumptions: (a) `meta.rawPath` value for `navidrome://` tracks; (b) script pickup timing; (c) module-import caching across searches; (d) TLS behavior; (e) default Tags field names if the tag route is ever revisited.

## Caveats / not found

- `meta.rawPath` exact string for custom-URI tracks — inference, needs runtime log (top risk).
- Script pickup semantics after file drop (restart vs rescan) — not documented anywhere fetched.
- ES-module import caching/reload behavior across track changes — unknown.
- Default tag field name(s) of the internal Tags source — unverified.
- `[offset:]` honored on load — indirect evidence only.
- ESLyric on foobar2000.org official components list — not checked.
- Zhipu search quota was exhausted (HTTP 429) and Exa unconfigured; grep.app blocked — GitHub code-wide search for `rawPath` usage in third-party scripts could not be completed (negative-precedent claim rests on repo search + DDG).
- LLM synthesized search answers were DISCARDED as unreliable (one invented a fake ESLyric API — e.g. `getLyrics(meta, config)` returning a Promise and a huge meta field list); every claim above traces to a fetched page.

## Sources index

| # | Source | Date | URL |
|---|---|---|---|
| S1 | Official wiki: 歌词源及解析器脚本 (full API + formats + example) | fetched 2026-08-12 | https://github.com/ESLyric/release/wiki/%E6%AD%8C%E8%AF%8D%E6%BA%90%E5%8F%8A%E8%A7%A3%E6%9E%90%E5%99%A8%E8%84%9A%E6%9C%AC |
| S2 | ESLyric/release (README, releases, locale) | fetched 2026-08-12; release 2026-03-22 | https://github.com/ESLyric/release ; /releases/tag/1.0.6.7 ; /blob/main/locale/en_US.lng |
| S3 | ESLyric/scripts (official sources: lrclib.js, musixmatch.js, genius.js, ...) | fetched 2026-08-12 | https://github.com/ESLyric/scripts |
| S4 | Hydrogenaudio thread "NEW ESLyric v0.5" (posts 2022-06-10 … 2026-04) | fetched 2026-08-12 | https://hydrogenaud.io/index.php/topic,122571.0.html |
| S5 | Hydrogenaudio Knowledgebase component page | oldid 37497 | https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Components/ESlyric_(foo_uie_eslyric) |
| S6 | ESLyric/feedback issues #21 (offset/tag), #106 (radio sync) | fetched 2026-08-12 | https://github.com/ESLyric/feedback/issues/21 ; /issues/106 |
| S7 | shimo-saki/ESLyric-LyricScript README (install paths) | fetched 2026-08-12 | https://github.com/shimo-saki/ESLyric-LyricScript |
| S8 | Robotxm/ESLyric-LyricsSource README (match filtering, legacy/current split) | fetched 2026-08-12 | https://github.com/Robotxm/ESLyric-LyricsSource |
| S9 | shirafukayayoi/fb2k-ESLyric-LyricGeminiStrip (auth-by-editing-script norm) | fetched 2026-08-12 | https://github.com/shirafukayayoi/fb2k-ESLyric-LyricGeminiStrip |
| S10 | Aeroblast legacy script (old 0.3.x ActiveX API contrast) | fetched 2026-08-12 | https://github.com/Aeroblast/Load_Local_LRC_for_offVocals_in_ESLyrics |
| S11 | GitHub repo search "ESLyric" (precedent sweep) | 2026-08-12 | https://api.github.com/search/repositories?q=ESLyric |
