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
