# Verification

## Passed

- `git diff --check`
- `Windows/foo_navidrome.vcxproj` XML parsing
- Strict UTF-8 decoding for all changed C++/header/project files
- Known Windows UI English-literal audit: only comments and vendored WTL documentation matched
- `Windows/Localization.h` syntax check with MSVC C++17 and `/utf-8`
- Manual diff review confirmed centralized button/context-menu/status strings, Windows-only About branching, non-negative compact layout calculations, and preserved remote/technical content

## Successful Windows build

SDK source:

`https://github.com/reupen/foobar2000-sdk-unmodified`

The SDK was staged in an isolated task build tree with the same layout used by upstream CI:

- `workspace/SDK`
- `workspace/helpers`
- `workspace/shared`
- `workspace/foobar2000_component_client`
- `pfc`
- `libPPUI`

Visual Studio Build Tools 2026 provides `v145`; the project and SDK projects were globally retargeted at the command line without modifying their checked-in toolset declarations. The minimal current ATL component was added because `helpers/foobar2000+atl.h` requires it. Vendored WTL was injected with an isolated `Directory.Build.targets` file.

Build command:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' `
  'Windows\foo_navidrome.vcxproj' `
  /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 `
  /p:SolutionDir='<isolated-product-root>\' /p:OutDir='<isolated-build-root>\out\x64\' `
  /m /nologo /v:minimal
```

Result:

- MSBuild: 0 warnings, 0 errors.
- DLL: `dist/zh-CN-win-x64/foo_navidrome.dll` (236,032 bytes).
- Package: `dist/zh-CN-win-x64/foo_navidrome_1.3.0_zh-CN_win-x64.fb2k-component`.
- Package size: 104,634 bytes; SHA-256: `72912863FDBE15EB99EB8E90BBC837CA3A0B384E383ACF86065E7CABE6F8E50B`.
- PE machine: x64 (`0x8664`).
- Chinese UTF-16 and UTF-8 marker strings are both present in the compiled DLL.
- DLL SHA-256: `23D3F8816E48E4D64505533DCA015D3119C727A000BE1B9BE57CB5FBA37F3A3C`.

The package contains exactly one entry, `x64/foo_navidrome.dll`; its size and SHA-256 match the standalone DLL, and ZIP integrity/CRC validation passed.

## Runtime checks still recommended

- Install the x64 component into foobar2000 and inspect the standalone and Media Library embedded browsers.
- Exercise settings connection success/failure, custom headers, File menu, right-click menu, search/loading/empty-selection/add-complete states.
- Inspect 100%, 125%, and 150% DPI plus the minimum supported window widths.
