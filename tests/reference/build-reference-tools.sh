#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Builds the reference partition tool that the comparison tests run beside our
# own code. Nothing of it is stored in this repository: the sources are read at
# the commit pinned in firmware/base.lock, and the result goes outside the tree.
#
# Usage: build-reference-tools.sh [--sources DIR] [--out DIR]
#   --sources DIR  a checkout of the base sources (local use). Without it the two
#                  tool directories are fetched at the pinned commit (CI),
#                  as a sparse checkout of those two directories only.
#   --out DIR      where the tool is built (default: $TACETOS_REFERENCE_TOOLS_DIR,
#                  else ~/.cache/tacetos/reference-tools). The tool is DIR/partition-tool.
#
# Then configure CMake with -DTACETOS_REFERENCE_TOOLS_DIR=DIR.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lock="$here/../../firmware/base.lock"
sources=""
out="${TACETOS_REFERENCE_TOOLS_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/tacetos/reference-tools}"

while [ $# -gt 0 ]; do
    case "$1" in
        --sources) sources="${2:?--sources needs a directory}"; shift 2 ;;
        --out) out="${2:?--out needs a directory}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-reference-tools.sh: unknown option: $1" >&2; exit 2 ;;
    esac
done

# shellcheck source=../../firmware/base.lock
source "$lock"

mkdir -p "$out"
out="$(cd "$out" && pwd)"

# A build directory inside the repository would end up in a commit.
repo="$(cd "$here/../.." && pwd)"
case "$out/" in
    "$repo"/*) echo "build-reference-tools.sh: $out is inside the repository" >&2; exit 2 ;;
esac

if [ -z "$sources" ]; then
    sources="$out/sources"
    echo "== fetching $REF_PARTITION_TOOL_DIR and $REF_DFU_TOOL_DIR at $BASE_COMMIT"
    rm -rf "$sources"
    mkdir -p "$sources"
    git -C "$sources" init -q
    git -C "$sources" remote add origin "$BASE_URL"
    git -C "$sources" sparse-checkout set --no-cone "/$REF_PARTITION_TOOL_DIR/" "/$REF_DFU_TOOL_DIR/"
    git -C "$sources" fetch -q --depth 1 --filter=blob:none origin "$BASE_COMMIT"
    git -C "$sources" checkout -q --detach FETCH_HEAD
    actual="$(git -C "$sources" rev-parse HEAD)"
    if [ "$actual" != "$BASE_COMMIT" ]; then
        echo "build-reference-tools.sh: expected $BASE_COMMIT, got $actual" >&2
        exit 1
    fi
fi

tool_src="$sources/$REF_PARTITION_TOOL_DIR"
if [ ! -d "$tool_src" ]; then
    echo "build-reference-tools.sh: no $REF_PARTITION_TOOL_DIR in $sources" >&2
    exit 1
fi

cc="${CC:-cc}"
# The source prints a version string that its own build passes in.
flags=(-O1 -w -DVERSION=\"reference\")
libs=()
if [ "$(uname -s)" = "Darwin" ]; then
    libs+=(-framework CoreFoundation -framework IOKit)
fi

files=()
for f in $REF_PARTITION_TOOL_SOURCES; do
    files+=("$tool_src/$f")
done

echo "== building the partition tool in $out"
"$cc" "${flags[@]}" -I"$tool_src" -o "$out/partition-tool" "${files[@]}" "${libs[@]}"
echo "== ready: $out/partition-tool"
