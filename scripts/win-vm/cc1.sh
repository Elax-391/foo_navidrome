#!/usr/bin/env bash
# cc1.sh — compile one Windows TU with clang-cl. Invoked by build-mac.sh via
# xargs (kept external so each xargs command line stays short). Config comes
# through CC1_* env vars; compile flags are read from $CC1_BUILD/.clflags.
set -uo pipefail
if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ]; then
  exec "$(brew --prefix)/bin/bash" "$0" "$@"
fi
src="$1"
key="$(echo "$src" | sed "s#^$CC1_R/##; s#^$CC1_SDK/##; s#^$CC1_PFC/#pfc/#; s#^$CC1_PPUI/#libPPUI/#; s#/#__#g")"
obj="$CC1_OBJ/${key%.cpp}.obj"
if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then exit 0; fi
mapfile -d "" flags < "$CC1_BUILD/.clflags"
extra=(); case "$(basename "$src")" in audio_math.cpp) extra=(-mavx2 -mfma);; esac
# Source passed as /Tp<path>: clang-cl otherwise parses a leading /Users as /U.
if "$CC1_LLVM/clang-cl" "${flags[@]}" "${extra[@]}" /Fo"$obj" /Tp"$src" 2>"$obj.log"; then
  exit 0
else
  cat "$obj.log" >> "$CC1_BUILD/errors.log"; exit 1
fi
