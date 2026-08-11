# Windows Native UI Localization

## Scope

Apply this contract when changing user-visible strings in the foobar2000 Windows component. It does not authorize translating macOS/Linux UI, developer scripts, CI output, server metadata, or remote error messages.

## String Contract

- Define reusable Windows UI strings in `Windows/Localization.h` under `navidrome::l10n`.
- Use `wchar_t` literals for Win32/WTL controls.
- Use C++17 `u8` `char` literals for foobar2000 APIs, `std::string` status text, and local error messages that flow through UTF-8-to-wide conversion.
- Add `/utf-8` to the common `ClCompile` options in `Windows/foo_navidrome.vcxproj`; it must apply to Debug/Release and Win32/x64/ARM64EC.
- Keep `Navidrome`, protocol/API names, HTTP status data, request-header names, configuration keys, and server-provided media/error content unchanged.
- Shared sources such as `main.cpp` must use `_WIN32` branching so non-Windows output remains unchanged.

## Dynamic Text and Errors

- Format Chinese count text in localization helpers instead of appending English nouns at call sites.
- Prefix remote error text with a localized component-owned prefix, but do not translate or reinterpret the remote message body.
- Translate only locally generated fallback/errors; retain API names and numeric codes where they help diagnosis.

## Layout Contract

- Treat Chinese text length as a layout change, not a string-only change.
- Button/status calculations must clamp widths and heights to non-negative values.
- Resizable top-level windows must set a usable minimum tracking size when fixed action rows would otherwise collapse.
- Review the standalone browser, embedded browser, preferences page, and custom-header window at normal, narrow, 125%, and 150% DPI conditions.

## Required Checks

1. Search the first-party `Windows/` sources for every replaced English UI literal; matches may remain only in comments or explicitly out-of-scope third-party files.
2. Parse `Windows/foo_navidrome.vcxproj` as XML and confirm `/utf-8` plus `Localization.h` registration.
3. Strictly decode every changed source file as UTF-8.
4. Run `git diff --check`.
5. Build `Release|x64` with the foobar2000 SDK sibling layout. The SDK bundle must stage `SDK`, `helpers`, `shared`, and `foobar2000_component_client` beside the product workspace, with `pfc` and `libPPUI` one level above it. ATL must come from Visual Studio Build Tools and WTL from `Windows/third_party/wtl/Include`.
6. A command-line `PlatformToolset` override may retarget both the component and SDK projects to the one installed toolset; do not rewrite all SDK project files merely to change `v142`/`v143` declarations.

## Wrong vs Correct

Wrong:

```cpp
menu.AppendMenu(MF_STRING, IDC_PLAY, L"立即播放");
setStatus("Added " + std::to_string(count) + " tracks");
```

Correct:

```cpp
menu.AppendMenu(MF_STRING, IDC_PLAY, navidrome::l10n::playNow);
setStatus(navidrome::l10n::addedTracks(count));
```

The correct form keeps repeated UI text and Chinese grammar in one auditable location.
