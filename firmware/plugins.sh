#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Applies the plugin list to a firmware zip (docs/specs/firmware-branding.md, section 7.6):
# plugins not in the list leave with their files, renamed plugins take their TacetOS name,
# and the file associations of viewers.config follow. Running it twice changes nothing more.
#
# Usage: plugins.sh --zip FILE [--list FILE] [--check]
#   --zip FILE    the zip made by the firmware build, changed in place
#   --list FILE   the plugin list (default: firmware/plugins.list)
#   --check       change nothing: list each plugin of the zip that is not in the list, each
#                 listed plugin the zip lacks, and each association to a missing plugin;
#                 exit 1 when there is one
#
# A plugin is a .rock, .ovl or .lua file of the plugins folder; its key is its path there
# without extension. The data files of a plugin go with it: files with its key and another
# extension or its key followed by "_", and the folders named after it (with or without a
# leading dot). Data files keep their names when the plugin is renamed: the plugin looks
# for them by those names.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

zip=""
list="$here/plugins.list"
check=false

die() { echo "plugins.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --zip)     zip="${2:?--zip needs a file}"; shift 2 ;;
        --list)    list="${2:?--list needs a file}"; shift 2 ;;
        --check)   check=true; shift ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

[ -f "$zip" ] || die "no zip: ${zip:-missing --zip}"
[ -f "$list" ] || die "no plugin list: $list"

exec python3 - "$zip" "$list" "$check" <<'PY'
import os, posixpath, shutil, sys, tempfile, zipfile

zip_path, list_path, check = sys.argv[1], sys.argv[2], sys.argv[3] == 'true'
PLUGIN_EXTENSIONS = ('.rock', '.ovl', '.lua')

renames = {}
for number, line in enumerate(open(list_path, encoding='utf-8'), 1):
    words = line.split('#')[0].split()
    if not words:
        continue
    if len(words) > 2:
        sys.exit('plugins.sh: %s:%d: a path and at most one new name' % (list_path, number))
    renames[words[0]] = words[1] if len(words) == 2 else None

def final(key):
    new = renames[key]
    return posixpath.join(posixpath.dirname(key), new) if new else key

wanted = {final(key) for key in renames}
owners = set(renames) | wanted

def split(name):
    """(top folder, path in the plugins folder) or None for a file outside the plugins folder."""
    parts = name.split('/', 2)
    if len(parts) == 3 and parts[1] == 'rocks':
        return parts[0], parts[2]
    return None

def data_of(rel, keys):
    """The key of the plugin that owns this data file, or None."""
    key, ext = posixpath.splitext(rel)
    for owner in keys:
        folder, base = posixpath.split(owner)
        if rel.startswith(owner + '/') or rel.startswith(posixpath.join(folder, '.' + base) + '/'):
            return owner
        if ext not in PLUGIN_EXTENSIONS and (key == owner or key.startswith(owner + '_')):
            return owner
    return None

def plugin_key(rel):
    key, ext = posixpath.splitext(rel)
    return key if ext in PLUGIN_EXTENSIONS else None

with zipfile.ZipFile(zip_path) as zf:
    infos = zf.infolist()
    names = [i.filename for i in infos]
    top = next((split(n)[0] for n in names if split(n)), None)
    viewers_name = top + '/viewers.config' if top else None
    viewers = zf.read(viewers_name).decode('utf-8') if viewers_name in names else None

    # Plugins of the zip that are not data of a listed plugin.
    present = set()
    for n in names:
        place = split(n)
        if place and not n.endswith('/') and not data_of(place[1], owners):
            key = plugin_key(place[1])
            if key:
                present.add(key)

    def associations(text):
        """The viewers.config lines kept, with renamed plugins, and the plugins they name."""
        kept, named = [], []
        for line in text.splitlines(True):
            fields = line.rstrip('\r\n').split(',')
            if len(fields) >= 2 and not line.startswith('#'):
                plugin = final(fields[1]) if fields[1] in renames else fields[1]
                named.append(plugin)
                if plugin not in wanted:
                    continue
                fields[1] = plugin
                line = ','.join(fields) + line[len(line.rstrip('\r\n')):]
            kept.append(line)
        return ''.join(kept), named

    if check:
        problems = ['%s: plugin not in the plugin list: %s' % (os.path.basename(zip_path), key)
                    for key in sorted(present - wanted)]
        problems += ['%s: listed plugin missing: %s' % (os.path.basename(zip_path), key)
                     for key in sorted(wanted - present)]
        if viewers is not None:
            problems += ['%s: viewers.config names a missing plugin: %s' % (os.path.basename(zip_path), p)
                         for p in sorted(set(associations(viewers)[1]) - present)]
        for p in problems:
            print(p)
        sys.exit(1 if problems else 0)

    gone = present - wanted - set(renames)
    out_fd, out_path = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(zip_path)), suffix='.zip')
    os.close(out_fd)
    with zipfile.ZipFile(out_path, 'w') as out:
        kept_names = []
        for info in infos:
            name, place = info.filename, split(info.filename)
            if place and not name.endswith('/'):
                rel = place[1]
                key = plugin_key(rel)
                owner = data_of(rel, owners)
                if not owner and data_of(rel, gone):
                    continue
                if not owner and key:
                    if key not in renames and key not in wanted:
                        continue
                    if key in renames and renames[key]:
                        name = '%s/rocks/%s%s' % (place[0], final(key), posixpath.splitext(rel)[1])
            kept_names.append((info, name))
        for info, name in kept_names:
            # A folder of the plugins folder stays only while something is left in it.
            if name.endswith('/') and split(name) and not any(n.startswith(name) and n != name for _, n in kept_names):
                continue
            data = zf.read(info.filename)
            if info.filename == viewers_name:
                data = associations(data.decode('utf-8'))[0].encode('utf-8')
            new = zipfile.ZipInfo(name, info.date_time)
            new.external_attr, new.compress_type, new.create_system = info.external_attr, info.compress_type, info.create_system
            out.writestr(new, data)
shutil.move(out_path, zip_path)
PY
