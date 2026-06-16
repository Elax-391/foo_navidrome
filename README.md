# foo_navidrome

A [foobar2000](https://www.foobar2000.org/) component that lets you browse and stream music from a [Navidrome](https://www.navidrome.org/) server (or any [Subsonic](http://www.subsonic.org/)-compatible server) directly inside foobar2000.

## Installation (end users)

1. Download the `.fb2k-component` for your platform from the [Releases](../../releases) page:
   - **macOS:** `foo_navidrome_<version>.fb2k-component`
   - **Windows (and Linux via Wine):** `foo_navidrome_<version>_win-x64.fb2k-component`
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
- **Native `navidrome://` URI scheme**: tracks added to playlists store a stable URI, not a transient HTTP URL — playlists survive credential rotation or server URL changes
- Appears under **Preferences › Media Library › Library viewers** alongside Album List / Artist View

## Platform Support

| Platform | Status |
|----------|--------|
| macOS    | ✅ Supported — shipped (Xcode, foobar2000 v2 for Mac) |
| Windows  | ✅ Builds & installs — runtime testing in progress (Visual Studio 2022, or cross-compiled) |
| Linux    | ⚙️ Runs under Wine — loads the Windows x64 build |

> The component now compiles, links, and installs as an x64 foobar2000 component —
> natively with Visual Studio, on CI, or **cross-compiled on Linux** with clang-cl
> (see [Building on Linux](#building-on-linux-wine)). The Windows UI and the
> `navidrome://` URI-scheme port are newer than the macOS path and are still being
> validated in-app, so a few rough edges are expected.

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

The fastest dev loop is `./scripts/dev-build.sh` — bumps the patch version in
`version.txt`, runs `xcodebuild`, and installs to your local foobar2000:

```bash
./scripts/dev-build.sh   # bump patch + build + install (see Scripts for --minor/--no-bump/… flags)
```

Restart foobar2000 after the script finishes to pick up the new version.

If you prefer Xcode directly:

1. Open `foo_navidrome.xcworkspace` in Xcode
2. Select the **`foo_navidrome`** scheme (top-left scheme selector)
3. Build: **Product › Build** or `Cmd+B`
4. Run the install script:
   ```bash
   ./scripts/install-macos.sh
   ```
5. Restart foobar2000

### Configuration

1. Open **Preferences › Tools › Navidrome**
2. Enter your server URL (e.g. `http://navidrome.local:4533/`)
3. Enter your username and password
4. Click **Test Connection** — you should see "Connected!"

### Usage

Two ways to open the browser:
- **File › Open Navidrome Browser**
- **Preferences › Media Library › Library viewers › Navidrome › Activate**

Then:
- Expand an artist to see albums, expand an album to see songs
- Select one or more items and click **Add to Playlist** or **Play Now**
- Double-click a song to play it immediately
- Use the search field to search across your library

Tracks land in the playlist as `navidrome://track/<id>?...` URIs. The component resolves these to the current HTTP stream when playback starts, so your playlists keep working if you change credentials or move the server.

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

> No Windows machine? You can **cross-compile the same x64 DLL on Linux** (see
> below), or run `./scripts/win-test.sh` to build it on a GitHub Actions Windows
> runner (real MSVC/MSBuild) and download the artifact. Each GitHub release also
> ships a `foo_navidrome_<version>_win-x64.fb2k-component` built by CI.

---

## Building on Linux (Wine)

foobar2000 has no native Linux build, but it runs well under **Wine** (it *is* the
Windows build). This repo can **cross-compile the Windows x64 component natively on
Linux** with `clang-cl` + `lld-link` — no MSVC and no Wine needed for the build
itself; Wine only runs foobar2000.

### Prerequisites

- foobar2000 for Windows installed under Wine (e.g. the AUR `foobar2000` package)
- `llvm`, `clang`, `lld` (provide `clang-cl` / `lld-link`), plus `git`, `curl`, `unzip`, `zip`

### One-time toolchain setup

```bash
./scripts/win-setup-toolchain.sh
```

Installs the LLVM toolchain, fetches the Windows SDK/CRT/**ATL** via
[`xwin`](https://github.com/Jake-Shadle/xwin), downloads [WTL](https://sourceforge.net/projects/wtl/),
and clones the foobar2000 SDK into the sibling layout (`../foobar2000`, `../pfc`, `../libPPUI`).

### Build + install loop

```bash
./scripts/win-build-local.sh   # cross-compile + install into the Wine foobar2000 (see Scripts for --launch/--clean)
```

The DLL is installed to `~/.foobar2000/profile/user-components-x64/foo_navidrome/`
and a distributable `foo_navidrome_<version>_win-x64.fb2k-component` is written to the
repo root. `scripts/install-windows.sh` handles the install + packaging and can be run
standalone after a build.

---

## Scripts

All helper scripts live in [`scripts/`](scripts/), grouped below by the OS you run
them on. Note the cross-platform twist: this project is currently developed on
**Linux**, and the **Windows component is built from Linux** (foobar2000 runs under
Wine; the DLL is cross-compiled with clang-cl) — so the "Windows" scripts run on a
Linux host. A *native* Windows build uses Visual Studio directly and needs no scripts.

### Linux — build & test the Windows component (current dev environment)

| Command | What it does |
|---------|--------------|
| `./scripts/win-setup-toolchain.sh` | One-time setup: install `clang-cl`/`lld-link`, fetch the Windows SDK/CRT/ATL (via xwin) + WTL, clone the SDK siblings |
| `./scripts/win-build-local.sh` | Cross-compile the x64 DLL and install it into the Wine foobar2000 |
| `./scripts/win-build-local.sh --launch` | …same, then relaunch foobar2000 |
| `./scripts/win-build-local.sh --clean` | Wipe the object cache and rebuild from scratch |
| `./scripts/install-windows.sh` | Install an already-built DLL + package the `.fb2k-component` (called by `win-build-local.sh`; `--new-release` to publish) |
| `./scripts/win-test.sh` | Build the DLL on a GitHub Actions Windows runner (real MSVC/MSBuild), download the artifact, install it locally |

### macOS — build & install the native Mac component

| Command | What it does |
|---------|--------------|
| `./scripts/dev-build.sh` | Bump patch version, `xcodebuild` Release, install locally |
| `./scripts/dev-build.sh --minor` · `--major` | Bump minor / major instead of patch |
| `./scripts/dev-build.sh --no-bump` | Rebuild + install at the current version |
| `./scripts/dev-build.sh --no-install` | Build only (skip install) |
| `./scripts/dev-build.sh --new-release` | Build, install, package, then create a GitHub release |
| `./scripts/install-macos.sh` | Install an already-built component + package the `.fb2k-component` (called by `dev-build.sh`) |

### CI (GitHub Actions — not run by hand)

| Script | What it does |
|--------|--------------|
| `scripts/ci-build.sh <version>` | macOS Release build + packaging, invoked by semantic-release during a release |

The Windows CI build is driven by `.github/workflows/build-windows.yml` (MSBuild on a
`windows-latest` runner) — no shell script involved.

---

## Project Structure

```
foo_navidrome/
├── main.cpp                        # Shared: component version + filename
├── stdafx.h                        # Shared precompiled header
├── SubsonicTypes.h                 # Shared pure-C++ data types
├── SubsonicClient.h/.mm            # macOS: ObjC Subsonic HTTP client
├── NavidromeInput.h/.mm            # macOS: input_singletrack handler for navidrome:// URIs
├── NavidromePlugin.mm              # macOS: plugin registration, cfg vars, prefs, menu, library_viewer
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
├── scripts/                        # build / install / toolchain helpers
│   ├── dev-build.sh                #   macOS dev loop (bump + xcodebuild + install)
│   ├── install-macos.sh            #   macOS install + package helper
│   ├── ci-build.sh                 #   CI build (called by semantic-release)
│   ├── win-setup-toolchain.sh      #   Linux: provision clang-cl + xwin SDK/ATL + WTL
│   ├── win-build-local.sh          #   Linux: cross-compile the Windows DLL + install
│   ├── install-windows.sh          #   Windows install + package helper
│   └── win-test.sh                 #   build the Windows DLL on CI + install locally
├── foo_navidrome.xcodeproj/        # Xcode project
└── foo_navidrome.xcworkspace/      # Xcode workspace (includes SDK projects)
```

## Contributing

Pull requests are welcome! Areas where help is especially appreciated:

- Windows UI polish and testing
- HTTPS certificate handling on Windows
- Playlist management improvements (e.g. create named playlist per artist)
- Offline/caching support

## Releasing

Releases are automated. Every push to `main` triggers `.github/workflows/release.yml`, which:

1. Lays out the sibling-directory tree the project expects (`pfc/`, `foobar2000/{SDK,helpers,helpers-mac,shared,foobar2000_component_client}`) by cloning [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) — an unmodified mirror of the official SDK that includes `helpers-mac/`.
2. Runs [`semantic-release`](https://semantic-release.gitbook.io/) per `.releaserc.json`. semantic-release inspects commits since the last tag and decides whether a release is needed.
3. If a release is needed:
   - `scripts/ci-build.sh <next-version>` pins `version.txt`, runs `xcodebuild -configuration Release`, ad-hoc signs, and packages the macOS `foo_navidrome_<version>.fb2k-component`.
   - `CHANGELOG.md` is updated.
   - A `chore(release): <version> [skip ci]` commit lands on `main` with `version.txt` + `CHANGELOG.md`.
   - A GitHub release is created with the macOS `.fb2k-component` attached.
   - The reusable `build-windows.yml` workflow then builds the Windows x64 DLL (MSVC/MSBuild on a `windows-latest` runner) and attaches `foo_navidrome_<version>_win-x64.fb2k-component` to the same release.

### Commit message convention

The pipeline reads [Conventional Commits](https://www.conventionalcommits.org/). The commit type determines whether (and how) the version is bumped:

| Type                  | Effect          |
| --------------------- | --------------- |
| `feat: …`             | minor bump      |
| `fix: …` / `perf: …`  | patch bump      |
| `refactor: …`         | patch bump      |
| `chore: …` / `docs: …` / `style: …` / `test: …` / `ci: …` | no release |
| `feat!: …` or footer `BREAKING CHANGE:` | major bump |

Versions are tracked in `version.txt` (consumed by the Xcode "Generate Version Header" build phase, which writes the gitignored `version_generated.h`).

### Manual release (fallback)

```bash
# Build, install locally, package, and create a GitHub release in one shot
./scripts/dev-build.sh --new-release
```

This bypasses the workflow and uses the `--new-release` path of `scripts/install-macos.sh`.

## License

MIT — see [LICENSE](LICENSE) file.
