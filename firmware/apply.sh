#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 YOUR_PSEUDO
#
# Downloads the base sources at the commit pinned in base.lock, brings in the
# listed backports, then applies our patches in order.
#
# Usage: apply.sh [--dest DIR]
#   --dest DIR   where to put the sources (default: firmware/work/src)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dest="$here/work/src"

while [ $# -gt 0 ]; do
    case "$1" in
        --dest) dest="${2:?--dest needs a directory}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "apply.sh: unknown option: $1" >&2; exit 2 ;;
    esac
done

# shellcheck source=base.lock
source "$here/base.lock"

# Commits made here only exist to carry the patches; they never leave the work tree.
git_local() {
    git -C "$dest" -c user.name="TacetOS build" -c user.email="build@invalid" \
        -c commit.gpgsign=false "$@"
}

# Always start from a fresh repository, but only replace one this script made.
marker="$dest/.git/tacetos-apply"
mkdir -p "$dest"
if [ -d "$dest/.git" ]; then
    [ -f "$marker" ] || { echo "apply.sh: $dest is a git repository not made by this script" >&2; exit 2; }
    rm -rf "$dest/.git"
fi
git -C "$dest" init -q
touch "$marker"
git -C "$dest" remote add origin "$BASE_URL"

echo "== base: $BASE_TAG ($BASE_COMMIT)"
git_local fetch -q --depth 1 origin "$BASE_COMMIT"
git_local checkout -q --force --detach FETCH_HEAD
git_local clean -q -fdx

actual="$(git_local rev-parse HEAD)"
if [ "$actual" != "$BASE_COMMIT" ]; then
    echo "apply.sh: expected $BASE_COMMIT, got $actual" >&2
    exit 1
fi

for commit in $BACKPORTS; do
    echo "== backport: $commit"
    # Depth 2 brings the parent, which the cherry-pick needs as its merge base.
    git_local fetch -q --depth 2 origin "$commit"
    if ! git_local cherry-pick --allow-empty --keep-redundant-commits "$commit" >/dev/null; then
        echo "apply.sh: backport $commit does not apply on $BASE_TAG" >&2
        git_local cherry-pick --abort || true
        exit 1
    fi
done

shopt -s nullglob
for patch in "$here"/patches/*.patch; do
    echo "== patch: $(basename "$patch")"
    if ! git_local apply --index "$patch"; then
        echo "apply.sh: patch $(basename "$patch") does not apply" >&2
        exit 1
    fi
    git_local commit -q -m "$(basename "$patch" .patch)"
done

echo "== ready: $dest"
