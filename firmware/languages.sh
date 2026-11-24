#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Puts the language list of the first start screen into a firmware zip, as
# langs/languages.txt of its firmware folder (docs/specs/boot-screen.md, section 7.2).
# Running it twice changes nothing more.
#
# Usage: languages.sh --zip FILE [--list FILE]
#   --zip FILE    the zip made by the firmware build, changed in place
#   --list FILE   the language list (default: firmware/languages.list)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

zip=""
list="$here/languages.list"

die() { echo "languages.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --zip)     zip="${2:?--zip needs a file}"; shift 2 ;;
        --list)    list="${2:?--list needs a file}"; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -f "$zip" ] || die "no zip: ${zip:-missing --zip}"
[ -f "$list" ] || die "no language list: $list"

exec python3 - "$zip" "$list" <<'PY'
import os, shutil, sys, tempfile, zipfile

zip_path, list_path = sys.argv[1], sys.argv[2]
data = open(list_path, 'rb').read()

with zipfile.ZipFile(zip_path) as zf:
    infos = zf.infolist()
    # The firmware folder is the one that holds the language files.
    tops = {i.filename.split('/', 1)[0] for i in infos if i.filename.split('/')[1:2] == ['langs']}
    if len(tops) != 1:
        sys.exit('languages.sh: no single firmware folder with langs/ in %s' % zip_path)
    name = tops.pop() + '/langs/languages.txt'
    out_fd, out_path = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(zip_path)), suffix='.zip')
    os.close(out_fd)
    with zipfile.ZipFile(out_path, 'w', zipfile.ZIP_DEFLATED) as out:
        for info in infos:
            if info.filename != name:
                out.writestr(info, zf.read(info.filename))
        out.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data, zipfile.ZIP_DEFLATED)
shutil.move(out_path, zip_path)
PY
