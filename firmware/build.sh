#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Builds the firmware, the bootloader or the simulator for one iPod model, from
# sources prepared by apply.sh.
#
# Usage: build.sh --target MODEL --type N|B|S [options]
#   --target MODEL     one of the supported models, see MODELS below
#   --type N|B|S       N firmware, B bootloader, S simulator
#   --src DIR          prepared sources (default: firmware/work/src)
#   --out DIR          where the results are copied (default: firmware/work/out)
#   --toolchain DIR    cross compiler install prefix (default: ~/.cache/tacetos/toolchain)
#   --jobs N           parallel jobs (default: number of CPUs)
#
# The cross compiler is built once into the toolchain prefix if it is missing.
# The simulator uses the host compiler and needs the SDL development files.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=base.lock
source "$here/base.lock"

MODELS="ipod1g2g ipod3g ipod4g ipodcolor ipodmini1g ipodmini2g ipodnano1g ipodnano2g ipodvideo ipod6g"

target=""
type=""
src="$here/work/src"
out="$here/work/out"
toolchain="${XDG_CACHE_HOME:-$HOME/.cache}/tacetos/toolchain"
jobs="$(nproc 2>/dev/null || echo 2)"

die() { echo "build.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target)    target="${2:?--target needs a model}"; shift 2 ;;
        --type)      type="${2:?--type needs N, B or S}"; shift 2 ;;
        --src)       src="${2:?--src needs a directory}"; shift 2 ;;
        --out)       out="${2:?--out needs a directory}"; shift 2 ;;
        --toolchain) toolchain="${2:?--toolchain needs a directory}"; shift 2 ;;
        --jobs)      jobs="${2:?--jobs needs a number}"; shift 2 ;;
        -h|--help)   sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -n "$target" ] || die "missing --target (one of: $MODELS)"
[ -n "$type" ] || die "missing --type (N, B or S)"
case " $MODELS " in *" $target "*) ;; *) die "unknown model: $target (one of: $MODELS)" ;; esac

case "$type" in
    N) artifacts="$ARTIFACTS_N" ;;
    B) artifacts="$ARTIFACTS_B" ;;
    S) artifacts="$ARTIFACTS_S" ;;
    *) die "unknown type: $type (N, B or S)" ;;
esac

[ -x "$src/$CONFIGURE_SCRIPT" ] || die "no prepared sources in $src (run apply.sh first)"

if [ "$type" != "S" ]; then
    if ! compgen -G "$toolchain/$TOOLCHAIN_BIN/*-gcc" >/dev/null; then
        echo "== toolchain: building into $toolchain (once)"
        mkdir -p "$toolchain"
        env "${TOOLCHAIN_ENV}_PREFIX=$toolchain" \
            "${TOOLCHAIN_ENV}_BUILD=$toolchain/build" \
            "${TOOLCHAIN_ENV}_DOWNLOAD=$toolchain/download" \
            "$src/$TOOLCHAIN_SCRIPT" --target="$TOOLCHAIN_ARCH" --makeflags="-j$jobs"
    fi
    export PATH="$toolchain/$TOOLCHAIN_BIN:$PATH"
fi

builddir="$(dirname "$src")/build-$target-$type"
rm -rf "$builddir"
mkdir -p "$builddir" "$out"

echo "== configure: $target, type $type"
cd "$builddir"
"$src/$CONFIGURE_SCRIPT" --target="$target" --type="$type"

echo "== make"
make -j"$jobs"
if [ "$type" = "N" ]; then
    make zip
fi

echo "== artifacts"
shopt -s nullglob
found=0
for pattern in $artifacts; do
    for file in $pattern; do
        # Keep the model in the name so ten builds can share one output directory.
        cp "$file" "$out/$target-$(basename "$file")"
        echo "$out/$target-$(basename "$file")"
        found=1
    done
done
[ "$found" = 1 ] || { echo "build.sh: build finished but no artifact matches: $artifacts" >&2; exit 1; }
