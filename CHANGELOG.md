## [1.1.0](https://github.com/santiagorod92/foo_navidrome/compare/v1.0.1...v1.1.0) (2026-05-23)


### Features

* general project improvements ([a6c2d89](https://github.com/santiagorod92/foo_navidrome/commit/a6c2d8918f54531f388a27b65453d1c68854f9db))


### Bug Fixes

* buid step ([7572610](https://github.com/santiagorod92/foo_navidrome/commit/75726100ed43f7d339b60b69f49c0d2b32c74dfb))
* build step again ([cd4bd8d](https://github.com/santiagorod92/foo_navidrome/commit/cd4bd8dfb939abfdc2770e0fd8b8dc6bf5f7ebd3))
* ci build ([8243e9b](https://github.com/santiagorod92/foo_navidrome/commit/8243e9b4fa15539f23972177e1aac9b72e4f6496))
* release action ([38a870c](https://github.com/santiagorod92/foo_navidrome/commit/38a870c090ab902b98b6df269f328b876aeaec8d))
* release action issues ([e6fe420](https://github.com/santiagorod92/foo_navidrome/commit/e6fe42012d10852c35f2434983448155cf5915c7))

# Changelog

All notable changes to this project are documented here. The file is regenerated
on every release by [semantic-release](https://semantic-release.gitbook.io/) from
[Conventional Commits](https://www.conventionalcommits.org/) in the git log.

Releases before automation:

## 1.0.10 — 2026-05-23

- Browser refactored from `NSWindowController` to `NSViewController`; mounted
  inline inside *Preferences › Media Library › Navidrome* (no extra window) and
  also wrapped in a standalone window from the File menu.

## 1.0.9 — 2026-05-23

- Cover art works for `navidrome://` URIs (the art extractor matched only the
  legacy `/rest/stream.view` HTTP URLs before).

## 1.0.8 — 2026-05-23

- Fixed `parse_uri` so song IDs are extracted correctly from
  `navidrome://track/<id>` URIs (RFC-3986 puts `track` in the authority, not the
  path).

## 1.0.5 — 2026-05-23

- Browser sub-page appears under *Preferences › Media Library › Navidrome*.

## 1.0.4 — 2026-05-23

- `install.sh` prefers the Release build over a stale Debug build in
  DerivedData.

## 1.0.3 — 2026-05-23

- Single-source-of-truth versioning via `version.txt` + `dev-build.sh`.

## 1.0.1 — 2026-04-10

- Initial macOS release.
