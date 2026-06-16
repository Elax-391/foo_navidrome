#!/usr/bin/env bash
# Installs the Windows foo_navidrome.dll into the local (Wine) foobar2000 and
# produces a distributable .fb2k-component package. The Linux counterpart of
# install-macos.sh — run after building with ./win-build-local.sh.
#
# Usage:
#   ./install-windows.sh                  — install locally, package
#   ./install-windows.sh --new-release    — same, then create a GitHub release and upload the package
#
# foobar2000 on Linux runs under Wine (the Windows build), loading x64 .dll
# components from the profile's user-components-x64 dir. No code signing — unlike
# macOS, foobar2000 on Windows does not reject unsigned components.

set -euo pipefail

NEW_RELEASE=false
for arg in "$@"; do
    case "$arg" in
        --new-release) NEW_RELEASE=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

COMPONENT_NAME="foo_navidrome"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # scripts/
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"          # repo root (build-win/, version_generated.h, package output)
BUILT_DLL="${BUILT_DLL:-${ROOT}/build-win/${COMPONENT_NAME}.dll}"
FB2K_COMPONENTS="${FB2K_COMPONENTS:-${HOME}/.foobar2000/profile/user-components-x64}"

# ---------------------------------------------------------------------------
# 1. Locate the built DLL
# ---------------------------------------------------------------------------
if [ ! -f "$BUILT_DLL" ]; then
    echo "ERROR: ${BUILT_DLL} not found."
    echo "Build it first:  ./win-build-local.sh"
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Install into the local (Wine) foobar2000 user-components-x64 dir
# ---------------------------------------------------------------------------
DEST="${FB2K_COMPONENTS}/${COMPONENT_NAME}"
echo "Installing: $BUILT_DLL"
echo "       To:  $DEST"
mkdir -p "$DEST"
cp -f "$BUILT_DLL" "$DEST/${COMPONENT_NAME}.dll"

# ---------------------------------------------------------------------------
# 3. Package into .fb2k-component (a ZIP foobar2000 can install directly)
# ---------------------------------------------------------------------------
# Read version from the generated header (written by win-build-local.sh / the
# Xcode build phase); keep the same file naming as install-macos.sh.
VERSION=""
VERSION_HEADER="${ROOT}/version_generated.h"
if [ -f "$VERSION_HEADER" ]; then
    VERSION=$(grep 'COMPONENT_VERSION' "$VERSION_HEADER" | sed 's/.*"\(.*\)".*/\1/')
fi
VERSION_SUFFIX="${VERSION:+_${VERSION}}"

OUTPUT="${ROOT}/${COMPONENT_NAME}${VERSION_SUFFIX}_win-x64.fb2k-component"
echo "Packaging:  $OUTPUT"

TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

# foobar2000 v2 reads 64-bit Windows components from an x64/ subdirectory inside
# the ZIP (32-bit at the root, mac/ for the macOS bundle), so the same
# .fb2k-component layout can carry every platform.
mkdir -p "$TMPDIR_PKG/x64"
cp -f "$BUILT_DLL" "$TMPDIR_PKG/x64/${COMPONENT_NAME}.dll"
rm -f "$OUTPUT"
( cd "$TMPDIR_PKG" && zip -rq "$OUTPUT" x64 )

echo ""
echo "Local install:  ${DEST}/${COMPONENT_NAME}.dll"
echo "Distributable:  $OUTPUT"
echo ""
echo "Restart foobar2000 to load the component."
echo "Preferences > Tools > Navidrome — enter your server URL and credentials."

# ---------------------------------------------------------------------------
# 4. (Optional) Create a GitHub release and upload the package
# ---------------------------------------------------------------------------
if [ "$NEW_RELEASE" = true ]; then
    if [ -z "$VERSION" ]; then
        echo "ERROR: Cannot create release — version_generated.h not found or empty."
        exit 1
    fi
    TAG="v${VERSION}"
    REPO="santiagorod92/${COMPONENT_NAME}"
    echo ""
    echo "Creating GitHub release ${TAG} on ${REPO}…"
    gh release create "$TAG" "$OUTPUT" \
        --repo "$REPO" \
        --title "$TAG" \
        --notes "See [README](https://github.com/${REPO}#readme) for installation instructions." \
        || gh release upload "$TAG" "$OUTPUT" --repo "$REPO" --clobber
    echo "Release published: https://github.com/${REPO}/releases/tag/${TAG}"
fi
