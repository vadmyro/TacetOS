#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Outputs the TacetOS version. If the current commit is a tag matching v<X.Y.Z>,
# returns X.Y.Z. Otherwise, returns X.Y.Z-dev+g<commit short 12>, where X.Y.Z is
# read from CMakeLists.txt and <commit short 12> is the short commit hash.
#
# Used by package.sh (its default version) and by the CI, which gives the same version
# to every firmware job. On a tag that is not the project version, it fails.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"

# Try to get the version from a tag matching v<X.Y.Z>
# On the CI the tag comes from the event: a shallow checkout may not hold the tags.
tag_version() {
    local tag
    if [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
        tag="${GITHUB_REF_NAME:-}"
    else
        tag=$(git -C "$repo" describe --tags --exact-match HEAD 2>/dev/null || echo "")
    fi
    if [ -n "$tag" ] && [[ "$tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
        echo "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

# Get project version from CMakeLists.txt
project_version() {
    sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$repo/CMakeLists.txt" | head -n 1
}

# Get short commit hash
short_commit() {
    git -C "$repo" rev-parse --short=12 HEAD 2>/dev/null || echo "unknown"
}

version="$(project_version)"
[ -n "$version" ] || { echo "version.sh: no VERSION in $repo/CMakeLists.txt" >&2; exit 2; }
if tag="$(tag_version)"; then
    # A release: the tag must be the project version (update-channel.md, section 2).
    [ "$tag" = "$version" ] || { echo "version.sh: tag v$tag but the project version is $version" >&2; exit 1; }
    echo "$version"
else
    echo "$version-dev+g$(short_commit)"
fi
