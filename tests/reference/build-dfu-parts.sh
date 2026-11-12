#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Builds the generated parts of the Classic DFU image and the reference tool that
# the comparison test runs beside our own code. Nothing of it is stored in this
# repository: the sources are read at the commit pinned in firmware/base.lock, and
# the result goes outside the tree.
#
# Usage: build-dfu-parts.sh [--sources DIR] [--out DIR] [--toolchain DIR]
#   --sources DIR    a checkout of the base sources (local use). Without it the
#                    folders that are needed are fetched at the pinned commit (CI),
#                    as a sparse checkout.
#   --out DIR        where the parts go (default: $TACETOS_DFU_PARTS_DIR, else
#                    ~/.cache/tacetos/dfu-parts).
#   --toolchain DIR  the prefix of an ARM toolchain, whose bin/ holds the compiler
#                    (local use: the one of an existing build of the base sources).
#                    Without a compiler in DIR, the toolchain is built there from
#                    the pinned sources, which takes a while (default: ~/.cache/tacetos/toolchain,
#                    a folder that CI keeps in its cache).
#
# Written to DIR: header-signature.bin, certificate.bin, installer-payload.bin,
# uninstaller-payload.bin (the four inputs of src/installer/dfuimage.cpp), dfu-tool
# (the reference tool, built from the same two programs) and manifest.txt (name,
# size and sha256 of each of the first four, tab separated).
#
# Then configure CMake with -DTACETOS_DFU_PARTS_DIR=DIR. The tool is only used for
# images, never to talk to a device, and never with its single boot option.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lock="$here/../../firmware/base.lock"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/tacetos"
sources=""
toolchain="$cache/toolchain"
out="${TACETOS_DFU_PARTS_DIR:-$cache/dfu-parts}"

while [ $# -gt 0 ]; do
    case "$1" in
        --sources) sources="${2:?--sources needs a directory}"; shift 2 ;;
        --out) out="${2:?--out needs a directory}"; shift 2 ;;
        --toolchain) toolchain="${2:?--toolchain needs a directory}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-dfu-parts.sh: unknown option: $1" >&2; exit 2 ;;
    esac
done

# shellcheck source=../../firmware/base.lock
source "$lock"

mkdir -p "$out" "$toolchain"
out="$(cd "$out" && pwd)"
toolchain="$(cd "$toolchain" && pwd)"

# A build directory inside the repository would end up in a commit.
repo="$(cd "$here/../.." && pwd)"
case "$out/" in
    "$repo"/*) echo "build-dfu-parts.sh: $out is inside the repository" >&2; exit 2 ;;
esac

if [ -z "$sources" ]; then
    sources="$out/sources"
    echo "== fetching the needed folders at $BASE_COMMIT"
    rm -rf "$sources"
    mkdir -p "$sources"
    git -C "$sources" init -q
    git -C "$sources" remote add origin "$BASE_URL"
    paths=("/$REF_DFU_TOOL_DIR/")
    for d in $REF_DFU_FETCH_DIRS; do
        paths+=("/$d/")
    done
    git -C "$sources" sparse-checkout set --no-cone "${paths[@]}"
    git -C "$sources" fetch -q --depth 1 --filter=blob:none origin "$BASE_COMMIT"
    git -C "$sources" checkout -q --detach FETCH_HEAD
    actual="$(git -C "$sources" rev-parse HEAD)"
    if [ "$actual" != "$BASE_COMMIT" ]; then
        echo "build-dfu-parts.sh: expected $BASE_COMMIT, got $actual" >&2
        exit 1
    fi
fi
sources="$(cd "$sources" && pwd)"

tool_src="$sources/$REF_DFU_TOOL_DIR"
if [ ! -d "$tool_src" ]; then
    echo "build-dfu-parts.sh: no $REF_DFU_TOOL_DIR in $sources" >&2
    exit 1
fi

# 1. The ARM compiler.
if ! compgen -G "$toolchain/$TOOLCHAIN_BIN/${REF_DFU_CROSS}gcc" >/dev/null; then
    echo "== toolchain: building into $toolchain (once)"
    env "${TOOLCHAIN_ENV}_PREFIX=$toolchain" \
        "${TOOLCHAIN_ENV}_BUILD=$toolchain/build" \
        "${TOOLCHAIN_ENV}_DOWNLOAD=$toolchain/download" \
        "$sources/$TOOLCHAIN_SCRIPT" --target="$TOOLCHAIN_ARCH" --makeflags="-j$(nproc 2>/dev/null || echo 2)"
fi
export PATH="$toolchain/$TOOLCHAIN_BIN:$PATH"

# 2. The two programs for the iPod. They are built in a copy of the tool's folder,
# so that the sources given with --sources are never written to. The make of the
# tool also turns them into the C file that the reference tool is built from, so the
# tool and our parts carry the very same programs.
work="$out/work"
rm -rf "$work"
mkdir -p "$work/$REF_DFU_TOOL_DIR"
cp -R "$tool_src/." "$work/$REF_DFU_TOOL_DIR/"
payload_dir="$work/$REF_DFU_PAYLOAD_DIR"
mkdir -p "$payload_dir/build"
echo "== building the programs for the iPod"
make -C "$payload_dir" ROOTDIR="$sources" CROSS="$REF_DFU_CROSS" $REF_DFU_PAYLOAD_TARGETS >"$work/payloads.log" 2>&1 || {
    tail -n 30 "$work/payloads.log" >&2
    exit 1
}

# Our name for each program is the first and the second target, install then uninstall.
read -r install_target uninstall_target <<<"$REF_DFU_PAYLOAD_TARGETS"

# The files are padded to a multiple of 4 with zeros, like the tool does when it
# turns them into C (our loader refuses any other size).
pad4() {
    local from="$1" to="$2" size
    cp "$from" "$to"
    size="$(wc -c <"$to")"
    if [ $((size % 4)) -ne 0 ]; then
        head -c $((4 - size % 4)) /dev/zero >>"$to"
    fi
}
pad4 "$payload_dir/$install_target" "$out/installer-payload.bin"
pad4 "$payload_dir/$uninstall_target" "$out/uninstaller-payload.bin"

# 3. The reference tool, from the same two programs (the make of the tool does it
# with its own C converter, which is built and run here in the copy).
echo "== building the reference tool"
cc="${CC:-cc}"
"$cc" -O1 -w -o "$payload_dir/bin2c" "$payload_dir/bin2c.c"
(cd "$payload_dir" && ./bin2c "../$REF_DFU_PAYLOAD_OUTPUT" $REF_DFU_PAYLOAD_TARGETS)
tool_files=()
for f in $REF_DFU_TOOL_SOURCES; do
    tool_files+=("$work/$REF_DFU_TOOL_DIR/$f")
done
# The tool prints a version string that its own build passes in. Its USB code is left
# out of this build: only the image commands are used.
"$cc" -O1 -w -DVERSION=\"reference\" -I"$work/$REF_DFU_TOOL_DIR" -o "$out/dfu-tool" "${tool_files[@]}"

# 4. The two fixed blocks, read out of the tool's code.
echo "== extracting the fixed blocks"
"$cc" -O1 -w -I"$work/$REF_DFU_TOOL_DIR" \
    -DREF_SOURCE="\"$REF_DFU_BLOCKS_SOURCE\"" -DREF_HEADER="$REF_DFU_HEADER_VAR" \
    -DREF_SIGNATURE="$REF_DFU_SIGNATURE_FIELD" -DREF_CERTIFICATE="$REF_DFU_CERTIFICATE_VAR" \
    -o "$work/extract-dfu-blocks" "$here/extract-dfu-blocks.c" "$work/$REF_DFU_TOOL_DIR/$REF_DFU_PAYLOAD_OUTPUT.c"
"$work/extract-dfu-blocks" "$out/header-signature.bin" "$out/certificate.bin"

# 5. The manifest.
sha256() {
    if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
: >"$out/manifest.txt"
for name in header-signature.bin certificate.bin installer-payload.bin uninstaller-payload.bin; do
    printf '%s\t%s\t%s\n' "$name" "$(wc -c <"$out/$name" | tr -d ' ')" "$(sha256 "$out/$name")" >>"$out/manifest.txt"
done

rm -rf "$work"
if [ -d "$out/sources" ]; then
    rm -rf "$out/sources"
fi
echo "== ready: $out"
cat "$out/manifest.txt"
