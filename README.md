# foo_navidrome

A [foobar2000](https://www.foobar2000.org/) component that lets you browse and stream music from a [Navidrome](https://www.navidrome.org/) server (or any [Subsonic](http://www.subsonic.org/)-compatible server) directly inside foobar2000.

## Installation (end users)

1. Download `foo_navidrome.fb2k-component` from the [Releases](../../releases) page
2. Drag the file onto foobar2000, or double-click it — foobar2000 installs it automatically
3. Restart foobar2000
4. Go to **Preferences › Tools › Navidrome** and enter your server URL and credentials

## Features

- Browse your entire music library: Artists → Albums → Songs
- Add albums or artists to playlist in one click (loads all songs automatically)
- Double-click a song to play immediately
- Search across artists, albums and songs
- Album artwork displayed in Now Playing and playlists (fetched from Navidrome)
- Credentials saved in foobar2000's config (persistent across restarts)
- Test Connection button to verify server connectivity

## Platform Support

| Platform | Status |
|----------|--------|
| macOS    | ✅ Supported (Xcode, foobar2000 v2 for Mac) |
| Windows  | 🚧 In development (Visual Studio 2022) |
| Linux    | ❌ foobar2000 is not available on Linux |

---

## Building on macOS

### Prerequisites

- [foobar2000 v2 for Mac](https://www.foobar2000.org/mac)
- Xcode 14+
- foobar2000 SDK — clone these repos as siblings of this repo:

```
foobar2000/
  SDK/          ← https://github.com/marc2k3/foobar2000-sdk (or official SDK)
  helpers/
  shared/
  foobar2000_component_client/
  foo_navidrome/   ← this repo
pfc/              ← https://github.com/marc2k3/pfc  (sibling of foobar2000/)
```

The expected layout relative to `foo_navidrome/`:

```
../SDK/
../helpers/
../shared/
../foobar2000_component_client/
../../pfc/
```

> If `pfc` is in a different location, create a symlink:
> ```bash
> ln -s /path/to/pfc /path/to/personal/pfc
> ```

### Build steps

1. Open `foo_navidrome.xcworkspace` in Xcode
2. Select the **`foo_navidrome`** scheme (top-left scheme selector)
3. Build: **Product › Build** or `Cmd+B`
4. Run the install script:
   ```bash
   ./install.sh
   ```
5. Restart foobar2000

### Configuration

1. Open **Preferences › Tools › Navidrome**
2. Enter your server URL (e.g. `http://navidrome.local:4533/`)
3. Enter your username and password
4. Click **Test Connection** — you should see "Connected!"

### Usage

- **File › Open Navidrome Browser** — opens the browser window
- Expand an artist to see albums, expand an album to see songs
- Select one or more items and click **Add to Playlist** or **Play Now**
- Double-click a song to play it immediately
- Use the search field to search across your library

---

## Building on Windows

### Prerequisites

- [foobar2000 v2 for Windows](https://www.foobar2000.org/)
- Visual Studio 2022 (with Desktop C++ workload)
- foobar2000 Windows SDK — same directory layout as above

### Build steps

1. Open `Windows/foo_navidrome.vcxproj` in Visual Studio
2. Update the `<ProjectReference>` GUIDs in the `.vcxproj` to match your local SDK project GUIDs
3. Build in **Release | x64** configuration
4. Copy `foo_navidrome.dll` to your foobar2000 components folder:
   ```
   %APPDATA%\foobar2000\user-components\foo_navidrome\
   ```
5. Restart foobar2000

---

## Project Structure

```
foo_navidrome/
├── main.cpp                        # Shared: component version + filename
├── stdafx.h                        # Shared precompiled header
├── SubsonicTypes.h                 # Shared pure-C++ data types
├── SubsonicClient.h/.mm            # macOS: ObjC Subsonic HTTP client
├── NavidromePlugin.mm              # macOS: plugin registration, cfg vars, prefs, menu
├── NavidromeArtExtractor.mm        # macOS: album art fallback (album_art_fallback)
├── Mac/
│   ├── NavidromeBrowserController.h/.mm    # macOS: NSWindowController browser UI
│   └── NavidromePreferencesController.h/.mm # macOS: NSViewController preferences
├── Windows/
│   ├── stdafx.h/.cpp               # Windows precompiled header
│   ├── SubsonicClientWin.h/.cpp    # Windows: WinHTTP Subsonic client
│   ├── NavidromePluginWin.cpp      # Windows: plugin registration, cfg vars, prefs, menu, art
│   ├── BrowserWindow.h/.cpp        # Windows: ATL browser window
│   └── foo_navidrome.vcxproj       # Visual Studio project
├── install.sh                      # macOS install helper
├── foo_navidrome.xcodeproj/        # Xcode project
└── foo_navidrome.xcworkspace/      # Xcode workspace (includes SDK projects)
```

## Contributing

Pull requests are welcome! Areas where help is especially appreciated:

- Windows UI polish and testing
- HTTPS certificate handling on Windows
- Playlist management improvements (e.g. create named playlist per artist)
- Offline/caching support

## License

MIT — see [LICENSE](LICENSE) file.
