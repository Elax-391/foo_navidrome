#!/bin/bash
# Developer build script: bump version, build, install.
#
# Usage:
#   ./dev-build.sh                  — bump patch, build, install locally
#   ./dev-build.sh --minor          — bump minor (resets patch to 0)
#   ./dev-build.sh --major          — bump major (resets minor + patch to 0)
#   ./dev-build.sh --no-bump        — skip the version bump (just build + install)
#   ./dev-build.sh --no-install     — build only (skip install.sh)
#   ./dev-build.sh --new-release    — bump, build, install, then create a GitHub release

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_FILE="${SCRIPT_DIR}/version.txt"

BUMP="patch"
DO_INSTALL=true
DO_RELEASE=false

for arg in "$@"; do
    case "$arg" in
        --major)        BUMP="major"      ;;
        --minor)        BUMP="minor"      ;;
        --patch)        BUMP="patch"      ;;
        --no-bump)      BUMP="none"       ;;
        --no-install)   DO_INSTALL=false  ;;
        --new-release)  DO_RELEASE=true   ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. Bump version.txt
# ---------------------------------------------------------------------------
if [ ! -f "$VERSION_FILE" ]; then
    echo "1.0.0" > "$VERSION_FILE"
fi

CURRENT=$(tr -d '[:space:]' < "$VERSION_FILE")
IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"
MAJOR="${MAJOR:-1}"; MINOR="${MINOR:-0}"; PATCH="${PATCH:-0}"

case "$BUMP" in
    major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
    minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
    patch) PATCH=$((PATCH + 1)) ;;
    none)  ;;
esac

NEW="${MAJOR}.${MINOR}.${PATCH}"
if [ "$BUMP" != "none" ]; then
    echo "$NEW" > "$VERSION_FILE"
    echo "Version: ${CURRENT} -> ${NEW}"
else
    echo "Version: ${CURRENT} (no bump)"
fi

# ---------------------------------------------------------------------------
# 2. Build
# ---------------------------------------------------------------------------
cd "$SCRIPT_DIR"
echo "Building (Release)..."
xcodebuild \
    -workspace foo_navidrome.xcworkspace \
    -scheme foo_navidrome \
    -configuration Release \
    build \
    | tail -n 20

# Check build success (xcodebuild returns 0 on success thanks to set -e)
echo "Build OK."

# ---------------------------------------------------------------------------
# 3. Install (delegates to install.sh, which also packages the .fb2k-component)
# ---------------------------------------------------------------------------
if [ "$DO_INSTALL" = true ]; then
    if [ "$DO_RELEASE" = true ]; then
        ./install.sh --new-release
    else
        ./install.sh
    fi
fi

echo ""
echo "Done. Restart foobar2000 to load v${NEW}."
