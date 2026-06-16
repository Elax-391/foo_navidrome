#!/usr/bin/env bash
# win-setup-toolchain.sh — one-time setup for the native Linux -> Windows x64
# cross-compile toolchain used by win-build-local.sh.
#
# foobar2000 runs under Wine on Linux, loading Windows .dll components. The
# component uses ATL/WTL + WinHTTP, which clang-cl compiles against a Windows
# SDK/CRT/ATL fetched by `xwin` plus WTL headers — no MSVC, no Wine needed for
# building (Wine only runs foobar). This installs everything win-build-local.sh
# checks for. Safe to re-run (idempotent-ish): it skips downloads already done.
#
# Installs / provisions:
#   - llvm clang lld          (pacman; provides clang-cl, lld-link, llvm-lib)
#   - xwin SDK/CRT/ATL  -> ~/.local/share/xwin/sdk   (--include-atl)
#   - WTL headers       -> ~/.local/share/wtl/Include
#   - foobar2000 SDK    -> ../foobar2000 + ../pfc + ../libPPUI   (siblings)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"   # repo root (scripts/ lives one level down)
PARENT="$(cd "$REPO/.." && pwd)"
XWIN_SDK="$HOME/.local/share/xwin/sdk"
WTL_DIR="$HOME/.local/share/wtl"
XWIN_VER="0.9.0"
WTL_URL="https://downloads.sourceforge.net/project/wtl/WTL%2010/WTL%2010.0.10320%20Release/WTL10_10320_Release.zip"
SDK_REPO="https://github.com/reupen/foobar2000-sdk-unmodified"

say() { echo "==> $*"; }

# 1. LLVM toolchain ---------------------------------------------------------
if ! command -v clang-cl >/dev/null || ! command -v lld-link >/dev/null; then
  say "installing llvm clang lld via pacman (needs sudo) ..."
  sudo pacman -S --needed --noconfirm llvm clang lld
else
  say "clang-cl / lld-link already present"
fi

# 2. xwin: Windows SDK + CRT + ATL -----------------------------------------
if [ ! -f "$XWIN_SDK/crt/include/atlbase.h" ]; then
  say "fetching xwin $XWIN_VER ..."
  tmp="$(mktemp -d)"
  curl -fsSL "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VER}/xwin-${XWIN_VER}-x86_64-unknown-linux-musl.tar.gz" \
    | tar xz -C "$tmp"
  xbin="$(find "$tmp" -name xwin -type f | head -1)"
  say "downloading Windows SDK/CRT/ATL (~1.5GB, with --include-atl) ..."
  "$xbin" --accept-license --include-atl --cache-dir "$HOME/.cache/xwin" \
    splat --output "$XWIN_SDK"
  rm -rf "$tmp"
else
  say "xwin SDK (with ATL) already at $XWIN_SDK"
fi

# 3. WTL headers ------------------------------------------------------------
if [ ! -f "$WTL_DIR/Include/atlapp.h" ]; then
  say "downloading WTL 10 ..."
  tmp="$(mktemp -d)"
  curl -fsSL -o "$tmp/wtl.zip" "$WTL_URL"
  ( cd "$tmp" && unzip -oq wtl.zip )
  mkdir -p "$WTL_DIR"
  cp -r "$tmp/Include" "$WTL_DIR/"
  rm -rf "$tmp"
else
  say "WTL already at $WTL_DIR/Include"
fi

# 4. foobar2000 SDK siblings ------------------------------------------------
if [ ! -f "$PARENT/foobar2000/helpers/foobar2000+atl.h" ]; then
  say "cloning foobar2000 SDK into sibling layout ..."
  stg="$PARENT/_sdk-staging"
  rm -rf "$stg"
  git clone --depth 1 "$SDK_REPO" "$stg"
  mkdir -p "$PARENT/foobar2000"
  for d in SDK helpers shared foobar2000_component_client helpers-mac; do
    [ -d "$stg/foobar2000/$d" ] && cp -r "$stg/foobar2000/$d" "$PARENT/foobar2000/$d"
  done
  for d in pfc libPPUI columns_ui-sdk; do
    [ -d "$stg/$d" ] && cp -r "$stg/$d" "$PARENT/$d"
  done
  rm -rf "$stg"
else
  say "foobar2000 SDK siblings already present"
fi

say "toolchain ready. Build with:  ./win-build-local.sh"
