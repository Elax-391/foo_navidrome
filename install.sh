#!/bin/bash
# Installs foo_navidrome to foobar2000 and produces a distributable .fb2k-component package.
# Run after building in Xcode (Product > Build or Cmd+B).

set -euo pipefail

COMPONENT_NAME="foo_navidrome"
BUILD_DIR="${HOME}/Library/Developer/Xcode/DerivedData"
FB2K_COMPONENTS="${HOME}/Library/foobar2000-v2/user-components"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# 1. Locate the built .component bundle
# ---------------------------------------------------------------------------
COMPONENT=$(find "$BUILD_DIR" -name "${COMPONENT_NAME}.component" \
    -not -path "*/Index.noindex/*" 2>/dev/null \
    | sort -t/ -k1 | head -1)

if [ -z "$COMPONENT" ]; then
    COMPONENT=$(find "$BUILD_DIR" -name "${COMPONENT_NAME}.component" 2>/dev/null | head -1)
fi

if [ -z "$COMPONENT" ]; then
    echo "ERROR: Could not find ${COMPONENT_NAME}.component in DerivedData."
    echo "Please build the project first: open foo_navidrome.xcworkspace and press Cmd+B"
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Install to local foobar2000 user-components
# ---------------------------------------------------------------------------
DEST="${FB2K_COMPONENTS}/${COMPONENT_NAME}"
echo "Installing: $COMPONENT"
echo "       To:  $DEST"
mkdir -p "$DEST"
cp -Rf "$COMPONENT" "$DEST/"

# ---------------------------------------------------------------------------
# 3. Ad-hoc sign (required — macOS kills unsigned bundles at load time)
# ---------------------------------------------------------------------------
INSTALLED="${DEST}/${COMPONENT_NAME}.component"
echo "Signing:    $INSTALLED"
codesign --sign - --force --deep "$INSTALLED"

# ---------------------------------------------------------------------------
# 4. Package into .fb2k-component (a ZIP foobar2000 can install directly)
# ---------------------------------------------------------------------------
# Read version from the generated header (created by the Xcode build phase)
VERSION=""
VERSION_HEADER="${SCRIPT_DIR}/version_generated.h"
if [ -f "$VERSION_HEADER" ]; then
    VERSION=$(grep 'COMPONENT_VERSION' "$VERSION_HEADER" | sed 's/.*"\(.*\)".*/\1/')
fi
VERSION_SUFFIX="${VERSION:+_${VERSION}}"

OUTPUT="${SCRIPT_DIR}/${COMPONENT_NAME}${VERSION_SUFFIX}.fb2k-component"
echo "Packaging:  $OUTPUT"

TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

# foobar2000 v2.6+ expects Mac bundles under a "mac/" subdirectory inside the ZIP
# This allows the same .fb2k-component to carry both Mac and Windows builds
mkdir -p "$TMPDIR_PKG/mac"
cp -Rf "$INSTALLED" "$TMPDIR_PKG/mac/"

# Create the ZIP using ditto (no __MACOSX metadata)
ditto --noqtn -ck --norsrc \
    "$TMPDIR_PKG" "$OUTPUT"

echo ""
echo "Local install:  $INSTALLED"
echo "Distributable:  $OUTPUT"
echo ""
echo "Restart foobar2000 to load the component."
echo "Preferences > Tools > Navidrome — enter your server URL and credentials."
echo ""
echo "To distribute: share ${COMPONENT_NAME}${VERSION_SUFFIX}.fb2k-component"
echo "Users can install it by dragging it onto foobar2000 or double-clicking it."
