#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Tries our backports and our patches on the newest commit of the development branch of
# the base (BASE_DEV_BRANCH in base.lock), then the branding, and writes a report of what
# applies and what conflicts. It changes nothing in this repository: the sources go to a
# work folder, and the pinned base is only moved by hand, after reading the report.
#
# Usage: follow.sh [--dest DIR] [--report FILE]
#   --dest DIR      where to put the sources (default: firmware/work/follow-src)
#   --report FILE   the report, in Markdown (default: firmware/work/follow-report.md)
#
# A patch that does not apply is skipped and the next ones are tried, so that one conflict
# does not hide the others (a later patch that needed it will then be reported too).
# Exit status: 0 when everything applies, 1 when something conflicts or the branding fails,
# 2 for a wrong call or when the newest commit cannot be fetched. The report is written
# in every case where the commit was fetched.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dest="$here/work/follow-src"
report="$here/work/follow-report.md"

while [ $# -gt 0 ]; do
    case "$1" in
        --dest)   dest="${2:?--dest needs a directory}"; shift 2 ;;
        --report) report="${2:?--report needs a file}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "follow.sh: unknown option: $1" >&2; exit 2 ;;
    esac
done

# shellcheck source=base.lock
source "$here/base.lock"
[ -n "${BASE_DEV_BRANCH:-}" ] || { echo "follow.sh: BASE_DEV_BRANCH is not set in base.lock" >&2; exit 2; }

git_local() {
    git -C "$dest" -c user.name="TacetOS build" -c user.email="build@invalid" \
        -c commit.gpgsign=false "$@"
}

# shellcheck source=pick-backport.sh
source "$here/pick-backport.sh"

# Always start from a fresh repository, but only replace one made by this script or by apply.sh.
marker="$dest/.git/tacetos-apply"
mkdir -p "$dest" "$(dirname "$report")"
if [ -d "$dest/.git" ]; then
    [ -f "$marker" ] || { echo "follow.sh: $dest is a git repository not made by this script" >&2; exit 2; }
    rm -rf "$dest/.git"
fi
git -C "$dest" init -q
touch "$marker"
git -C "$dest" remote add origin "$BASE_URL"

echo "== newest commit of the branch $BASE_DEV_BRANCH"
if ! git_local fetch -q --depth 1 origin "$BASE_DEV_BRANCH"; then
    echo "follow.sh: cannot fetch the branch $BASE_DEV_BRANCH" >&2
    exit 2
fi
git_local checkout -q --force --detach FETCH_HEAD
git_local clean -q -fdx
newest="$(git_local rev-parse HEAD)"
newest_date="$(git_local log -1 --format=%cs)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
rows_backports="$work/backports.txt"
rows_patches="$work/patches.txt"
details="$work/details.txt"
: > "$rows_backports"; : > "$rows_patches"; : > "$details"
problems=0

# Adds the first lines of a failure to the details, under a title.
add_details() {
    { printf '\n### %s\n\n```\n' "$1"; head -n 20 "$2"; printf '```\n'; } >> "$details"
}

for commit in $BACKPORTS; do
    echo "== backport: $commit"
    short="${commit:0:12}"
    log="$work/backport.log"
    if git_local fetch -q --depth 2 origin "$commit" 2>"$log" \
       && pick_backport "$commit" >"$log" 2>&1; then
        if git_local diff --quiet HEAD~1 HEAD; then
            echo "| \`$short\` | already in the newest commit, can leave the list |" >> "$rows_backports"
        else
            echo "| \`$short\` | applies |" >> "$rows_backports"
        fi
    else
        git_local cherry-pick --abort 2>/dev/null || true
        echo "| \`$short\` | **conflict** |" >> "$rows_backports"
        add_details "Backport $short" "$log"
        problems=$((problems + 1))
    fi
done

shopt -s nullglob
applied=0
for patch in "$here"/patches/*.patch; do
    name="$(basename "$patch" .patch)"
    echo "== patch: $name"
    log="$work/patch.log"
    if git_local apply --index "$patch" 2>"$log"; then
        git_local commit -q -m "$name"
        echo "| \`$name\` | applies |" >> "$rows_patches"
        applied=$((applied + 1))
    else
        echo "| \`$name\` | **conflict** |" >> "$rows_patches"
        add_details "Patch $name" "$log"
        problems=$((problems + 1))
    fi
done

echo "== branding"
brand="not run"
log="$work/brand.log"
if bash "$here/brand.sh" --src "$dest" >"$log" 2>&1; then
    git_local add -A
    git_local commit -q -m "branding" --allow-empty
    if bash "$here/brand.sh" --check "$dest" >"$log" 2>&1; then
        brand="done, and the check finds nothing of the base"
    else
        brand="**done, but the check still finds the base**"
        add_details "Branding check" "$log"
        problems=$((problems + 1))
    fi
else
    brand="**failed**"
    add_details "Branding" "$log"
    problems=$((problems + 1))
fi

patch_count="$(printf '%s\n' "$here"/patches/*.patch | grep -c . || true)"
{
    echo "# Following the base"
    echo
    echo "- Branch: \`$BASE_DEV_BRANCH\`, newest commit \`${newest:0:12}\` of $newest_date"
    echo "- Pinned base: \`$BASE_TAG\` (\`${BASE_COMMIT:0:12}\`)"
    echo "- Patches applied: $applied of $patch_count"
    if [ "$problems" -eq 0 ]; then
        echo "- Result: **everything applies**"
    else
        echo "- Result: **$problems problem(s)**, see the details below"
    fi
    echo
    echo "## Backports"
    echo
    if [ -s "$rows_backports" ]; then
        echo "| Commit | Result |"; echo "|---|---|"; cat "$rows_backports"
    else
        echo "None."
    fi
    echo
    echo "## Patches"
    echo
    if [ -s "$rows_patches" ]; then
        echo "| Patch | Result |"; echo "|---|---|"; cat "$rows_patches"
    else
        echo "None."
    fi
    echo
    echo "## Branding"
    echo
    echo "$brand"
    cat "$details"
} > "$report"

echo "== report: $report ($problems problem(s))"
[ "$problems" -eq 0 ]
