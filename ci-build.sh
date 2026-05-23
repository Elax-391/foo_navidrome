#!/bin/bash
# CI build orchestrator — called by semantic-release prepareCmd.
#
# Usage: ./ci-build.sh <new-version>
#
# 1. Writes the new version to version.txt (single source of truth read by the
#    Xcode "Generate Version Header" build phase).
# 2. Builds the Release configuration with xcodebuild.
# 3. Packages the built .component into a .fb2k-component zip in the repo root.
#
# Does NOT install to ~/Library/foobar2000-v2/ (that's dev-build.sh's job).

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version>" >&2
    exit 1
fi

VERSION="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPONENT_NAME="foo_navidrome"

cd "$SCRIPT_DIR"

# ---------------------------------------------------------------------------
# 1. Pin version
# ---------------------------------------------------------------------------
echo "$VERSION" > version.txt
echo "ci-build: version = $VERSION"

# ---------------------------------------------------------------------------
# 2. Build (Release)
# ---------------------------------------------------------------------------
echo "ci-build: xcodebuild Release ..."
# Print full xcodebuild output to /tmp and stream a filtered summary to stdout.
# On failure, dump the full log so the actual compile error is visible in the
# GitHub Actions step output (the noisy compile-command echoes mean the error
# message would otherwise scroll off-screen, hidden behind thousands of lines).
LOG=/tmp/xcodebuild.log
set +e
xcodebuild \
    -workspace foo_navidrome.xcworkspace \
    -scheme foo_navidrome \
    -configuration Release \
    -derivedDataPath build/derived \
    build > "$LOG" 2>&1
XCB_RC=$?
set -e

# Always show the summary (errors, warnings, notes, BUILD result lines).
grep -E "error:|warning:|note:|\\*\\* BUILD|ld:|fatal:|FAILED" "$LOG" || true

if [ $XCB_RC -ne 0 ]; then
    echo "ci-build: xcodebuild failed (rc=$XCB_RC). Full log:"
    echo "----- BEGIN xcodebuild log -----"
    cat "$LOG"
    echo "----- END xcodebuild log -----"
    exit $XCB_RC
fi

# ---------------------------------------------------------------------------
# 3. Locate built bundle
# ---------------------------------------------------------------------------
COMPONENT="build/derived/Build/Products/Release/${COMPONENT_NAME}.component"
if [ ! -d "$COMPONENT" ]; then
    echo "ERROR: built bundle not found at $COMPONENT" >&2
    exit 1
fi
echo "ci-build: built $COMPONENT"

# ---------------------------------------------------------------------------
# 4. Ad-hoc sign (foobar2000 rejects unsigned bundles on load)
# ---------------------------------------------------------------------------
codesign --sign - --force --deep "$COMPONENT"

# ---------------------------------------------------------------------------
# 5. Package into foo_navidrome_<VERSION>.fb2k-component
#    foobar2000 v2.6+ expects Mac bundles under "mac/" inside the zip.
# ---------------------------------------------------------------------------
OUTPUT="${SCRIPT_DIR}/${COMPONENT_NAME}_${VERSION}.fb2k-component"
TMPDIR_PKG=$(mktemp -d)
trap 'rm -rf "$TMPDIR_PKG"' EXIT

mkdir -p "$TMPDIR_PKG/mac"
cp -Rf "$COMPONENT" "$TMPDIR_PKG/mac/"

ditto --noqtn -ck --norsrc "$TMPDIR_PKG" "$OUTPUT"

echo "ci-build: packaged $OUTPUT"
