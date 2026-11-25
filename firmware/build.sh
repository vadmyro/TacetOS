#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Builds the firmware, the bootloader or the simulator for one iPod model, from
# sources prepared by apply.sh.
#
# Usage: build.sh --target MODEL --type N|B|S [options]
#        build.sh --toolchain-only [--src DIR] [--toolchain DIR] [--jobs N]
#   --target MODEL     one of the supported models, see MODELS below
#   --type N|B|S       N firmware, B bootloader, S simulator
#   --src DIR          prepared sources (default: firmware/work/src)
#   --out DIR          where the results are copied (default: firmware/work/out)
#   --toolchain DIR    cross compiler install prefix (default: ~/.cache/tacetos/toolchain)
#   --jobs N           parallel jobs (default: number of CPUs)
#   --toolchain-only   only build the cross compiler if it is missing, then stop
#
# The cross compiler is built once into the toolchain prefix if it is missing.
# The simulator uses the host compiler and needs the SDL development files. It is stripped.
# A model with `firmware_folder` in models/models.toml gets its firmware and its
# bootloader built for that folder.
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
toolchain_only=0

die() { echo "build.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target)    target="${2:?--target needs a model}"; shift 2 ;;
        --type)      type="${2:?--type needs N, B or S}"; shift 2 ;;
        --src)       src="${2:?--src needs a directory}"; shift 2 ;;
        --out)       out="${2:?--out needs a directory}"; shift 2 ;;
        --toolchain) toolchain="${2:?--toolchain needs a directory}"; shift 2 ;;
        --jobs)      jobs="${2:?--jobs needs a number}"; shift 2 ;;
        --toolchain-only) toolchain_only=1; shift ;;
        -h|--help)   sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

if [ "$toolchain_only" = 1 ]; then
    # The toolchain is the same for every model: the CI builds it once, before the models.
    target=ipod6g
    type=B
fi
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

# The firmware folder of the model, empty for the default one (branding section 8.2).
folder="$(bash "$here/firmware-folder.sh" "$target")"
configure_folder=()
if [ -n "$folder" ] && [ "$type" != "S" ]; then
    configure_folder=("--rbdir=$folder")
fi

if [ "$type" != "S" ]; then
    if ! compgen -G "$toolchain/$TOOLCHAIN_BIN/*-gcc" >/dev/null; then
        echo "== toolchain: building into $toolchain (once)"
        mkdir -p "$toolchain"
        env "${TOOLCHAIN_ENV}_PREFIX=$toolchain" \
            "${TOOLCHAIN_ENV}_BUILD=$toolchain/build" \
            "${TOOLCHAIN_ENV}_DOWNLOAD=$toolchain/download" \
            "$TOOLCHAIN_MIRROR_VAR=$TOOLCHAIN_MIRROR" \
            "$src/$TOOLCHAIN_SCRIPT" --target="$TOOLCHAIN_ARCH" --makeflags="-j$jobs"
        # The toolchain script exits with 0 even when a download fails: check its result.
        if ! compgen -G "$toolchain/$TOOLCHAIN_BIN/*-gcc" >/dev/null; then
            echo "build.sh: no compiler in $toolchain/$TOOLCHAIN_BIN after the toolchain build (see above)" >&2
            exit 1
        fi
    fi
    export PATH="$toolchain/$TOOLCHAIN_BIN:$PATH"
fi
if [ "$toolchain_only" = 1 ]; then
    echo "== toolchain: ready in $toolchain"
    exit 0
fi

# The base's make passes every source path in a single shell command, which
# overflows the system limit when the sources sit under a long path: the build
# goes through a short link to them, and its build folder sits next to the link.
src="$(cd "$src" && pwd)"
out="$(mkdir -p "$out" && cd "$out" && pwd)"
short="${TMPDIR:-/tmp}/tacetos-$target-$type"
mkdir -p "$short"
ln -sfn "$src" "$short/src"
src="$short/src"
builddir="$short/build"
rm -rf "$builddir"
mkdir -p "$builddir"

echo "== configure: $target, type $type${configure_folder:+, folder $folder}"
cd "$builddir"
"$src/$CONFIGURE_SCRIPT" --target="$target" --type="$type" "${configure_folder[@]}"

echo "== make"
make -j"$jobs"
if [ "$type" = "N" ]; then
    make zip
    # Only the plugins of plugins.list reach the zip, renamed as it says. The listed ones
    # the base does not build for this model are noted for check-build.sh.
    for zip in *.zip; do
        bash "$here/plugins.sh" --zip "$zip" --absent "$out/$target-plugins-absent.txt"
    done
    # The language list of the first start screen goes into the firmware folder.
    for zip in *.zip; do
        bash "$here/languages.sh" --zip "$zip"
    done
fi

shopt -s nullglob
if [ "$type" = "S" ]; then
    # The simulator is a test tool kept as an artifact: strip it, so that the symbol names
    # and the debug information of the base's code do not travel with it.
    for pattern in $artifacts; do
        for file in $pattern; do
            strip --strip-all "$file"
        done
    done
fi

echo "== artifacts"
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
