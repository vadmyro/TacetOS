#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Record the reference result of the library rules.
#
# Usage: record_reference.sh REFERENCE_SCRIPT OUTPUT_DIR
#
# Builds the fake library in a private HOME under $TMPDIR (never the real
# music folder), drops it in the inbox, runs the reference script on it with a
# plain folder standing in for the device, and writes two files to OUTPUT_DIR:
#
#   reference-device-tree.txt     every path on the fake device, sorted, folders end with "/"
#   reference-left-in-inbox.txt   every file the reference left in the inbox
#
# Paths are compared by name only, never by size or hash (the audio bytes
# depend on the encoder version). Needs ffmpeg, ffprobe, rsync and python3.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 REFERENCE_SCRIPT OUTPUT_DIR" >&2
    exit 1
fi
ref="$(realpath "$1")"
out="$(realpath -m "$2")"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

work="$(mktemp -d "${TMPDIR:-/tmp}/reference.XXXXXX")"
trap 'rm -rf "$work"' EXIT

home="$work/home"
lib="$home/Music/iPod"
inbox="$lib/To Add"
pod="$work/pod"
mkdir -p "$inbox" "$pod" "$work/bin" "$out"

python3 "$here/make_fake_library.py" "$work/gen" >/dev/null
mv "$work/gen/manifest.tsv" "$work/manifest.tsv"
cp -a "$work/gen/." "$inbox/"
cp "$here/artists.tsv" "$lib/Artists.tsv"

# The device is a folder: answer the "where is it mounted" question with it,
# and make sure nothing tries to mount a real disk.
cat > "$work/bin/findmnt" <<SH
#!/bin/sh
case "\$*" in
    *LABEL=Ipod*) echo "$pod" ;;
esac
SH
printf '#!/bin/sh\nexit 1\n' > "$work/bin/blkid"
printf '#!/bin/sh\nexit 1\n' > "$work/bin/udisksctl"
chmod +x "$work/bin/findmnt" "$work/bin/blkid" "$work/bin/udisksctl"

HOME="$home" PATH="$work/bin:$PATH" LC_ALL=C.UTF-8 bash "$ref" >"$work/run.log" 2>&1 || {
    cat "$work/run.log" >&2
    exit 1
}

( cd "$pod" && find . -mindepth 1 -printf '%P%y\n' | sed -E 's/d$/\//; s/f$//' | LC_ALL=C sort ) \
    > "$out/reference-device-tree.txt"
( cd "$inbox" && find . -type f -printf '%P\n' | LC_ALL=C sort ) \
    > "$out/reference-left-in-inbox.txt"

echo "Recorded $(wc -l < "$out/reference-device-tree.txt") device paths and $(wc -l < "$out/reference-left-in-inbox.txt") inbox leftovers in $out"
