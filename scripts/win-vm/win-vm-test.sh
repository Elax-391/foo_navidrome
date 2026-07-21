#!/usr/bin/env bash
# win-vm-test.sh — the local Windows-on-ARM test loop:
#   1. cross-build the x64 foo_navidrome.dll on this Mac (build-mac.sh)
#   2. scp it into the running Win11 ARM QEMU guest
#   3. relaunch foobar2000 there
#
# The guest is created by win-vm.sh + autounattend.xml (run once). foobar2000 on
# ARM is ARM64EC and loads the x64 component via emulation, so the x64 build is
# all we need to exercise the UI. Requires the guest booted (win-vm.sh run) with
# SSH up (tester/tester on localhost:2222).
#
# Usage: ./win-vm-test.sh [--launch]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SSH_PORT="${SSH_PORT:-2222}"
VMDIR="${VMDIR:-$HOME/.local/share/foo_navidrome-winvm}"
# Common opts (no port flag — ssh uses -p, scp uses -P).
COMMON=(-i "$VMDIR/id_vm" -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)
SSH_OPTS=(-p "$SSH_PORT" "${COMMON[@]}")
SCP_OPTS=(-P "$SSH_PORT" "${COMMON[@]}")
# foobar-on-ARM (ARM64EC) scans user-components-arm64ec — it loads our x64 DLL
# there via emulation. Hot-swapping the DLL in-place requires the component to
# already be installed once from a .fb2k-component (see README); a loose drop
# into a fresh folder is NOT picked up by the ARM build.
GUEST_DIR='C:\Users\tester\AppData\Roaming\foobar2000-v2\user-components-arm64ec\foo_navidrome'
LAUNCH=0; [ "${1:-}" = "--launch" ] && LAUNCH=1

echo "==> building x64 DLL ..."
"$HERE/build-mac.sh"
DLL="$REPO/build-win-mac/foo_navidrome.dll"
[ -f "$DLL" ] || { echo "build produced no DLL"; exit 1; }

echo "==> waiting for guest SSH on localhost:$SSH_PORT ..."
for i in $(seq 1 60); do
  ssh "${SSH_OPTS[@]}" tester@localhost "echo ok" >/dev/null 2>&1 && break
  sleep 5
  [ "$i" = 60 ] && { echo "guest SSH not reachable — is win-vm.sh run booted + provisioned?"; exit 1; }
done

echo "==> deploying component ..."
ssh "${SSH_OPTS[@]}" tester@localhost "cmd /c \"mkdir \"$GUEST_DIR\" 2>nul & taskkill /IM foobar2000.exe /F 2>nul & exit /b 0\""
scp "${SCP_OPTS[@]}" "$DLL" "tester@localhost:AppData/Roaming/foobar2000-v2/user-components-arm64ec/foo_navidrome/foo_navidrome.dll"

if [ "$LAUNCH" = 1 ]; then
  echo "==> relaunching foobar2000 ..."
  ssh "${SSH_OPTS[@]}" tester@localhost \
    "cmd /c start \"\" \"%ProgramFiles%\\foobar2000\\foobar2000.exe\"" || true
fi
echo "==> done. In the guest: open the Navidrome browser and right-click a row."
