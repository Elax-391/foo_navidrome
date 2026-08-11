# Windows localization research findings

## Architecture

- The project is a native foobar2000 component with a shared C++ core and platform-specific UI/HTTP layers (`research/upstream/README.md:238-288`).
- Windows builds `foo_navidrome.dll` from `Windows/foo_navidrome.vcxproj` using C++17, Win32/ATL/WTL, WinHTTP and Unicode APIs (`research/upstream/Windows/foo_navidrome.vcxproj:80-117`).
- Windows UI is created programmatically. There are no project `.rc` files, STRINGTABLE entries, locale catalogs or `LoadString` calls (`research/upstream/Windows/NavidromePluginWin.cpp:177`; `research/upstream/Windows/foo_navidrome.vcxproj:146-163`).

## User-visible Windows strings

- Preferences and custom headers: `research/upstream/Windows/NavidromePluginWin.cpp:61-129,227-288`.
- File menu command and description: `research/upstream/Windows/NavidromePluginWin.cpp:372-400`.
- Browser title, search cue, buttons, context menu and statuses: `research/upstream/Windows/BrowserWindow.cpp:40,71,81-90,135,173-178,265,348-350,399-417,490,543-548`.
- Local network errors and missing metadata fallbacks: `research/upstream/Windows/SubsonicClientWin.cpp:176-182,250-291,318-339`.
- Component About text is shared in `research/upstream/main.cpp:8-18`; a Windows-only conditional is required so macOS behavior remains English.
- There is no Windows notification-area/tray implementation in the upstream sources.

## Keep untranslated

- `Navidrome`, `Subsonic`, `HTTP`, `WinHTTP`, `Cloudflare`, request-header names, URLs, paths, configuration keys and metadata keys.
- Song, artist, album and remote error-message content returned by the server.
- Developer-facing terminal output in `scripts/*.sh`, CI workflow text and release notes.

## Encoding and layout risks

- `_UNICODE` and Win32 wide APIs cover runtime controls, but the project does not explicitly compile source files as UTF-8. Add MSVC `/utf-8` before storing Chinese literals in source.
- Narrow strings used by foobar2000 APIs and `setStatus()` must remain UTF-8; wide strings are used directly by Win32 controls.
- Fixed widths need review for the browser action row and the custom-header dialog. Relevant layout code is `research/upstream/Windows/BrowserWindow.cpp:102-130` and `research/upstream/Windows/NavidromePluginWin.cpp:118-129,227-242`.

## Windows build evidence

- Native prerequisites and manual build are documented at `research/upstream/README.md:121-145`.
- GitHub Actions uses `windows-2022`, MSBuild v143, and builds Win32, x64 and ARM64EC before producing a `.fb2k-component` package (`research/upstream/.github/workflows/build-windows.yml:46-52,147-223`).
- The local workspace may not contain the external foobar2000 SDK sibling projects required by `ProjectReference`; validation must distinguish source/static checks from a real Windows build.
