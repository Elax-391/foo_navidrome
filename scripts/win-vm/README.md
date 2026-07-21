# Local Windows-on-ARM testing (macOS, no Windows machine)

Build the Windows `foo_navidrome.dll` on your Mac and runtime-test it in a headless
Windows 11 ARM64 VM — all from the terminal, no CI round-trip, no separate PC.

## How it works

- **Build**: `clang-cl` + `lld-link` cross-compile the component against the
  Microsoft CRT/SDK/ATL fetched by [`xwin`](https://github.com/Jake-Shadle/xwin)
  plus WTL headers. No MSVC, no Wine. Only the **x64** target is built locally —
  clang can't cross-compile ARM64EC (it needs MSVC-only intrinsics). That's fine:
  foobar2000 on Windows-ARM is ARM64EC and loads the x64 component via emulation.
  (CI's `build-windows.yml` builds the real ARM64EC binary for releases.)
- **VM**: a scriptable `qemu-system-aarch64` guest with HVF (native-speed ARM
  virtualization) — the same engine UTM wraps, minus the un-scriptable GUI. NVMe
  disk (inbox driver), virtio-net (driver auto-loaded from `$WinPEDriver$`), and
  an `autounattend.xml` that does a hands-off install, creates a local admin,
  auto-logs-in, and enables OpenSSH.
- **Deploy**: `scp` the freshly built DLL into the guest over SSH and relaunch
  foobar2000.

## One-time setup

```bash
scripts/win-vm/setup-mac-toolchain.sh   # brew deps + xwin + WTL + foobar SDK
scripts/win-vm/fetch-win11-arm.sh       # build Win11 ARM ISO (~4GB) + virtio ISO
scripts/win-vm/win-vm.sh install        # unattended install (~20-30 min, headless)
```

`win-vm.sh install` returns immediately after launching QEMU; the install runs in
the background. Watch `~/.local/share/foo_navidrome-winvm/serial.log` (UEFI only —
Windows itself is silent on serial) or just wait for SSH on `localhost:2222` to
answer (`tester` / `tester`). Once it does, the guest is provisioned. Then install
foobar2000 ARM once inside the guest (download the ARM installer from
foobar2000.org and run it, or `winget install foobar2000`).

## The test loop

```bash
scripts/win-vm/win-vm.sh run            # boot the installed VM (if not running)
scripts/win-vm/win-vm-test.sh --launch  # build x64 DLL -> scp in -> relaunch foobar
```

Then in the guest: **File ▸ Open Navidrome Browser**, right-click a row → the
**Play Now / Add to Playlist** context menu.

`win-vm.sh ssh` drops you into a shell in the guest; `win-vm.sh stop` powers it off.

## Files

| file | role |
|------|------|
| `setup-mac-toolchain.sh` | installs clang-cl/lld/xwin/qemu/WTL + foobar SDK |
| `build-mac.sh`, `cc1.sh` | cross-compile the x64 DLL |
| `fetch-win11-arm.sh`     | build the Win11 ARM ISO (uupdump) + virtio ISO |
| `autounattend.xml`       | hands-off Windows install answer file |
| `win-vm.sh`              | create / boot / ssh / stop the QEMU guest |
| `win-vm-test.sh`         | build → deploy → relaunch loop |

## Notes / gotchas

- **`clang-cl` eats `/Users/...` as the `/U` flag** — sources are passed as
  `/Tp<path>` in `cc1.sh`. Don't "simplify" that away.
- **Credentials are throwaway** (`tester`/`tester`, host-only NAT). Never reuse
  the answer file for anything real.
- The install boots the ARM64 CD (`bootaa64.efi`). The autounattend media is a
  **CD**, not a raw disk image, so it doesn't win the UEFI boot election.
- **Static CRT (`/MT`) is mandatory** for the local x64 build — foobar-on-ARM
  only bundles the ARM64EC flavour of `VCRUNTIME140`/`MSVCP140`, so an emulated
  x64 `/MD` DLL fails to load silently. `build-mac.sh` already uses `/MT`.
- **Install once from the `.fb2k-component`, then hot-swap.** A loose DLL dropped
  into a component folder is NOT picked up by the ARM build. Install the packaged
  component through foobar's UI first (it lands in `user-components-arm64ec\`);
  after that `win-vm-test.sh` hot-swaps the DLL in place and relaunches.
