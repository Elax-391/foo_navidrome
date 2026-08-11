# SDK build evidence

## Source

- Repository: `https://github.com/reupen/foobar2000-sdk-unmodified`
- Retrieved bundle contained `foobar2000/{SDK,helpers,shared,foobar2000_component_client}`, `pfc`, and `libPPUI`.
- The product CI documents the same staging layout in `.github/workflows/build-windows.yml:58-104`.

## Local toolchain

- Visual Studio Build Tools 2026 18.7
- MSVC 14.51 / PlatformToolset `v145`
- Windows SDK 10.0.26100.0
- Current ATL component installed during this task
- WTL from `Windows/third_party/wtl/Include`

## Build result

- Target: `Release|x64`
- Global toolset override: `PlatformToolset=v145`
- Result: 0 warnings, 0 errors
- Output machine: x64 (`0x8664`)
- Output SHA-256: `23D3F8816E48E4D64505533DCA015D3119C727A000BE1B9BE57CB5FBA37F3A3C`

## Resolved failures

1. `MSB8020 v143 missing`: use the installed `v145` as a global MSBuild property.
2. ATL headers missing: install the current `Microsoft.VisualStudio.Component.VC.ATL` component.
3. `libPPUI/*.h` missing: stage SDK-bundle `libPPUI` at the build root, matching upstream CI.
