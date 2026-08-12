# Verification: Windows Cover Art and ESLyric Lyrics

## Automated Results

- Component `Release|x64`: PASS with Visual Studio Build Tools v145 and the
  reupen foobar2000 SDK sibling layout.
- `MediaEnrichmentTests`: PASS, `/W4 /WX`, output
  `All MediaEnrichment tests passed`.
- `LibraryImportTests`: PASS, output `All library import tests passed`.
- Embedded ESLyric ES-module syntax: PASS via `node --input-type=module --check`.
- Static checks: vcxproj XML parse, strict UTF-8 decode, no removed fallback or
  duplicate-script references, no lyric `location` assignment, and
  `git diff --check`: PASS.
- Independent final review findings fixed: finite DNS timeout, length-framed
  cache keys, and no remote cover id in diagnostics.

## Delivery

- DLL: `dist/cover-lyrics-win-x64/foo_navidrome.dll`
- DLL size: `402432` bytes.
- DLL SHA-256: `A96E86BF98492AEE26692E01A18391BB4EEA5728CDC86F8827FCB973C52848B5`
- Component:
  `dist/cover-lyrics-win-x64/foo_navidrome_1.3.0_cover-lyrics_win-x64.fb2k-component`
- Component SHA-256:
  `9AC6EF377229850F2CD100C9A7E6F00FA5852B8DDB3F9A66468417BEE8FF185C`
- Archive entry: exactly `x64/foo_navidrome.dll`.
- Inner/outer DLL SHA-256: identical.
- PE machine: `0x8664` (x64).

## Real-Host Evidence

- Installed final DLL into the existing foobar2000 v2 profile after backing up
  the previous DLL to the isolated build cache.
- Plugin startup generated:
  - `%APPDATA%\foobar2000-v2\eslyric-data\scripts\searcher\navidrome.js`
  - `%APPDATA%\foobar2000-v2\eslyric-data\scripts\lib\foo_navidrome\config.js`
- Structural checks confirmed `getLyrics` export, canonical `config` export,
  token field, `debug:false`, and no lyric `location` assignment. Secret values
  were not printed.
- After restart, foobar2000 v2.25.10 loaded normally. The selected
  `navidrome://track/...` item displayed its server cover in the active layout,
  confirming the path-owning extractor in the real host.

## Remaining Manual Boundary

The active layout has no visible ESLyric lyric panel, so playback was not
started or the user's layout changed automatically. The user must still verify
one synced, one plain/Chinese/emoji, and one no-lyrics Navidrome track in their
preferred ESLyric panel, including timestamp/offset behavior and the legacy
404 fallback where applicable.
