#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Turns the files build.sh copied for one model (types N and B) into a TacetOS
# firmware package: the firmware zip, the bootloader file and a manifest that
# lists both with their sha256. The format is described in README.md, "Package".
#
# Usage: package.sh --target MODEL [options]
#   --target MODEL     the model the files were built for (as in build.sh)
#   --in DIR           where build.sh copied its results (default: firmware/work/out)
#   --out DIR          where the package is written (default: firmware/work/package)
#   --version TEXT     package version (default: project version + "+g" + short commit)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"

# shellcheck source=base.lock
source "$here/base.lock"

target=""
in="$here/work/out"
out="$here/work/package"
version=""

die() { echo "package.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target)  target="${2:?--target needs a model}"; shift 2 ;;
        --in)      in="${2:?--in needs a directory}"; shift 2 ;;
        --out)     out="${2:?--out needs a directory}"; shift 2 ;;
        --version) version="${2:?--version needs a text}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -n "$target" ] || die "missing --target"
[ -d "$in" ] || die "no build results in $in (run build.sh first)"

# The files build.sh copied are named "<model>-<artifact>". Among the artifacts of a
# normal build, the firmware is the zip; the bootloader build gives one file.
find_one() {
    local kind="$1" filter="$3" found=() patterns pattern file
    # read splits the list without expanding the patterns here, in the current folder.
    read -r -a patterns <<<"$2"
    shopt -s nullglob
    for pattern in "${patterns[@]}"; do
        for file in "$in/$target-"$pattern; do
            # A pattern with no wildcard stays as it is: keep only files that exist.
            [ -f "$file" ] || continue
            case "$file" in $filter) found+=("$file") ;; esac
        done
    done
    shopt -u nullglob
    [ "${#found[@]}" = 1 ] || die "expected one $kind file for $target in $in, found ${#found[@]}"
    printf '%s\n' "${found[0]}"
}

firmware_in="$(find_one firmware "$ARTIFACTS_N" '*.zip')"
bootloader_in="$(find_one bootloader "$ARTIFACTS_B" '*')"

if [ -z "$version" ]; then
    project_version="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$repo/CMakeLists.txt" | head -n 1)"
    [ -n "$project_version" ] || die "no VERSION in $repo/CMakeLists.txt"
    commit="$(git -C "$repo" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
    version="$project_version+g$commit"
fi
case "$version" in *[!A-Za-z0-9.+-]*|"") die "version must use letters, digits, '.', '+' and '-' only: $version" ;; esac

# One hash for the patch set: the name and the content of each patch, in the order
# apply.sh applies them. The backports are listed in the manifest one by one.
patches_hash="$(
    shopt -s nullglob
    for patch in "$here"/patches/*.patch; do
        printf '%s\n' "$(basename "$patch")"
        cat "$patch"
    done | sha256sum | cut -d ' ' -f 1
)"

mkdir -p "$out"
firmware_name="tacetos-$target-$version.zip"
bootloader_ext="${bootloader_in##*.}"
bootloader_name="tacetos-bootloader-$target-$version.$bootloader_ext"
manifest_name="tacetos-$target-$version.json"

cp "$firmware_in" "$out/$firmware_name"
cp "$bootloader_in" "$out/$bootloader_name"

# A fixed time gives the same manifest for the same build (SOURCE_DATE_EPOCH).
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
    built="$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%dT%H:%M:%SZ)"
else
    built="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
fi

python3 - "$out" "$manifest_name" "$target" "$version" "$BASE_COMMIT" "$patches_hash" \
    "$built" "$firmware_name" "$bootloader_name" $BACKPORTS <<'EOF'
import hashlib, json, os, sys

out, manifest, target, version, base, patches, built, firmware, bootloader = sys.argv[1:10]
backports = sys.argv[10:]

def entry(name, kind):
    path = os.path.join(out, name)
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return {"name": name, "kind": kind, "size": os.path.getsize(path), "sha256": digest.hexdigest()}

data = {
    "format": 1,
    "target": target,
    "version": version,
    "baseCommit": base,
    "backports": backports,
    "patches": patches,
    "built": built,
    "files": [entry(firmware, "firmware"), entry(bootloader, "bootloader")],
}
with open(os.path.join(out, manifest), "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
EOF

echo "$out/$firmware_name"
echo "$out/$bootloader_name"
echo "$out/$manifest_name"
