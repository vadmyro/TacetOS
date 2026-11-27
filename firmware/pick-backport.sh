# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Sourced by apply.sh and follow.sh, never run alone. Needs git_local and the
# values of base.lock.
#
# pick_backport COMMIT: cherry-picks one backport on the work tree. A conflict in a
# file listed in BACKPORT_LOOSE_FILES is not a failure: the change to that file is
# dropped and the rest of the commit is kept. Any other conflict fails (return 1) and
# leaves the cherry-pick unfinished, for the caller to abort.
pick_backport() {
    local commit="$1" file
    local -a conflicted

    git_local cherry-pick --allow-empty --keep-redundant-commits "$commit" && return 0

    mapfile -t conflicted < <(git_local diff --name-only --diff-filter=U)
    [ "${#conflicted[@]}" -gt 0 ] || return 1
    for file in "${conflicted[@]}"; do
        case " $BACKPORT_LOOSE_FILES " in
            *" $file "*) ;;
            *) return 1 ;;
        esac
    done
    for file in "${conflicted[@]}"; do
        echo "pick-backport.sh: $file: change dropped, listed in BACKPORT_LOOSE_FILES"
        git_local checkout --ours -- "$file"
        git_local add -- "$file"
    done
    git_local -c core.editor=true cherry-pick --continue
}
