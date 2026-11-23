#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Checks that the firmware build outputs follow the branding rules
# (docs/specs/firmware-branding.md, section 6). It fails, listing each problem, when:
#
# 1. an output of a requested type is missing, or older than base.lock or than any file of patches/;
# 2. a string of the firmware file, the bootloader file, any plugin or codec in the zip, or any
#    compiled language file, contains a spelling of BRAND_NAMES that is not inside a
#    BRAND_TOLERATED entry, or that reads as text (two words or more with letters) (strings of
#    4 printable characters or more, matched without regard to case);
# 3. a path in the zip contains a spelling of the name outside a BRAND_TOLERATED entry, or a
#    plugin file name (without its extension) contains "rock";
# 4. an image of BRAND_LOGO_IMAGES or BRAND_ICON_SETS in the sources still has the SHA-256 of
#    the base's file (BRAND_ORIGINAL_SHA256);
# 5. with --plugins: the zip holds a plugin that is not in the plugin list, or lacks one that is;
# 6. with a model whose firmware folder is set (firmware_folder in models/models.toml):
#    a path of the zip is not inside that folder.
#
# Usage: check-build.sh --target MODEL --types N,B,S [options] | --self-test
#   --target MODEL   the iPod model, as given to build.sh
#   --types N,B,S    the build types to check (N firmware, B bootloader, S simulator)
#   --out DIR        the build outputs (default: firmware/work/out)
#   --src DIR        the sources used by the build (default: firmware/work/src)
#   --plugins        also check the plugins against firmware/plugins.list (rule 5)
#   --self-test      prove each rule on small files made on the fly; needs no build
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

target=""
types=""
out="$here/work/out"
src="$here/work/src"
plugins=false
self_test=false

die() { echo "check-build.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --target)    target="${2:?--target needs a model}"; shift 2 ;;
        --types)     types="${2:?--types needs N, B or S, comma separated}"; shift 2 ;;
        --out)       out="${2:?--out needs a directory}"; shift 2 ;;
        --src)       src="${2:?--src needs a directory}"; shift 2 ;;
        --plugins)   plugins=true; shift ;;
        --self-test) self_test=true; shift ;;
        -h|--help)   sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

# The check itself. It reads everything from the environment, so that the self-test runs
# the very same code on its own small files and its own lists.
read -r -d '' check_py <<'PY' || true
import fnmatch, glob, hashlib, os, re, sys, zipfile

env = os.environ

def lines(text):
    return [l.split('#')[0].strip() for l in text.splitlines() if l.split('#')[0].strip()]

names = re.compile('|'.join('(?:%s)' % n for n in lines(env['BRAND_NAMES'])), re.IGNORECASE)

# An entry is a piece of text, optionally preceded by a file pattern and a colon (*.ipod:text).
tolerated_pieces = []
for entry in lines(env.get('BRAND_TOLERATED', '')):
    pattern, colon, piece = entry.partition(':')
    if colon and '*' in pattern and ' ' not in pattern:
        tolerated_pieces.append((pattern, piece))
    else:
        tolerated_pieces.append(('', entry))

def tolerated(text, where):
    """True when no spelling of the name is left once the pieces allowed in this file are taken out.
    A string that reads as text (two words or more with letters) is never tolerated: a technical
    name is never part of a message."""
    if sum(1 for word in text.split() if re.search('[^\\W\\d_]', word)) >= 2:
        return False
    for pattern, piece in tolerated_pieces:
        if not pattern or fnmatch.fnmatch(where, pattern) or fnmatch.fnmatch(os.path.basename(where), pattern):
            text = text.replace(piece, '')
    return not names.search(text)

problems = []

def scan(data, where):
    for m in re.finditer(rb'[\x20-\x7e]{4,}', data):
        string = m.group(0).decode('ascii')
        if names.search(string) and not tolerated(string, where):
            problems.append('%s: string shows the name: %r' % (where, string))

target, out_dir = env['CHECK_TARGET'], env['CHECK_OUT']

# Rule 1: the outputs of each requested type are present and newer than base.lock and patches/.
newest = max(os.path.getmtime(f) for f in env['CHECK_REFERENCES'].splitlines() if f)
outputs = []
for kind in env['CHECK_TYPES'].split(','):
    patterns = env.get('ARTIFACTS_' + kind, '').split()
    if not patterns:
        problems.append('unknown build type: %s' % kind)
    for pattern in patterns:
        found = sorted(glob.glob(os.path.join(glob.escape(out_dir), target + '-' + pattern)))
        if not found:
            problems.append('output missing: %s-%s' % (target, pattern))
        for f in found:
            outputs.append(f)
            if os.path.getmtime(f) < newest:
                problems.append('%s: older than base.lock or a patch, build again' % os.path.basename(f))

# Rules 2 and 3: strings of the binaries, plugins, codecs and languages; paths of the zip.
for f in outputs:
    base = os.path.basename(f)
    if not f.endswith('.zip'):
        with open(f, 'rb') as fh:
            scan(fh.read(), base)
        continue
    with zipfile.ZipFile(f) as zf:
        for path in zf.namelist():
            if names.search(path) and not tolerated(path, path):
                problems.append('%s: path shows the name: %s' % (base, path))
            stem, ext = os.path.splitext(os.path.basename(path))
            if ext in ('.rock', '.ovl') and 'rock' in stem.lower():
                problems.append('%s: plugin name has "rock": %s' % (base, path))
            if ext in ('.rock', '.ovl', '.codec', '.lng'):
                scan(zf.read(path), base + ':' + path)

# Rule 4: the logos and icon sets of the sources are not the base's files.
for entry in lines(env.get('BRAND_ORIGINAL_SHA256', '')):
    digest, path = entry.split(None, 1)
    f = os.path.join(env['CHECK_SRC'], path)
    if os.path.isfile(f):
        with open(f, 'rb') as fh:
            if hashlib.sha256(fh.read()).hexdigest() == digest:
                problems.append('%s: still the base file' % path)

# Rule 5: the plugins of the zip are those of the plugin list, checked by plugins.sh.
if env.get('CHECK_PLUGINS') == 'true':
    import subprocess
    for f in outputs:
        if f.endswith('.zip'):
            result = subprocess.run(['bash', env['CHECK_PLUGINS_SH'], '--zip', f, '--list', env['CHECK_PLUGIN_LIST'], '--check'],
                                    capture_output=True, text=True)
            problems += [l for l in (result.stdout + result.stderr).splitlines() if l]
            if result.returncode not in (0, 1):
                problems.append('%s: plugins.sh could not read it' % os.path.basename(f))

# Rule 6: with a model whose firmware folder is set, the zip has only that folder at the top.
folder = env.get('CHECK_FOLDER', '').strip('/')
if folder:
    for f in outputs:
        if f.endswith('.zip'):
            with zipfile.ZipFile(f) as zf:
                tops = sorted({p.lstrip('/').split('/')[0] for p in zf.namelist()} - {folder})
            for top in tops:
                problems.append('%s: top folder is %s, not %s' % (os.path.basename(f), top, folder))

for p in problems:
    print(p)
if problems:
    sys.exit('check-build.sh: %d problem(s) found' % len(problems))
print('check-build.sh: build check passed')
PY
plugins_sh="$here/plugins.sh"
export check_py plugins_sh

if [ "$self_test" = true ]; then
    exec python3 - <<'PY'
import hashlib, os, subprocess, sys, tempfile, time, zipfile

passed = failed = 0

def files(root, content):
    for path, data in content.items():
        f = os.path.join(root, path)
        os.makedirs(os.path.dirname(f), exist_ok=True)
        with open(f, 'wb') as fh:
            fh.write(data)

def zipped(content):
    with tempfile.TemporaryFile() as tmp:
        with zipfile.ZipFile(tmp, 'w') as zf:
            for path, data in content.items():
                zf.writestr(path, data)
        tmp.seek(0)
        return tmp.read()

logo = b'BM the new logo'
old_logo = b'BM the old logo'
good_zip = {
    '.base/rocks/games/blocks.rock': b'\0\0Blocks\0TacetOS\0/.base/blocks.cfg\0',
    '.base/codecs/mpa.codec': b'\0\0MPEG audio\0',
    '.base/langs/english.lng': b'\0\0About TacetOS\0',
    '.base/themes/default.cfg': b'Basebox here, but no binary is read',
}

def run(name, change, expect):
    """Make good outputs, apply one change, run the check, compare with what is expected."""
    global passed, failed
    with tempfile.TemporaryDirectory() as tmp:
        reference = os.path.join(tmp, 'base.lock')
        files(tmp, {'base.lock': b'#'})
        old = time.time() - 60
        os.utime(reference, (old, old))
        out, src = os.path.join(tmp, 'out'), os.path.join(tmp, 'src')
        content = {
            'model-firmware.ipod': b'\0TacetOS 1.0\0/.base/boot.ipod\0Basebox_ok\0',
            'model-bootloader-model.ipod': b'\0Loading TacetOS\0',
            'model-package.zip': None,
        }
        tree = {'zip': dict(good_zip), 'out': content, 'src': {'bitmaps/logo.bmp': logo}}
        change(tree)
        tree['out']['model-package.zip'] = zipped(tree['zip'])
        files(out, {k: v for k, v in tree['out'].items() if v is not None})
        files(src, tree['src'])
        plugin_list = os.path.join(tmp, 'plugins.list')
        files(tmp, {'plugins.list': tree.get('list', b'# test list\ngames/blocks\n')})
        for f in tree.get('old', []):
            os.utime(os.path.join(out, f), (old - 60, old - 60))
        env = dict(os.environ,
                   BRAND_NAMES='basebo(x|ks)\n',
                   BRAND_TOLERATED='.base # folder\n*.ipod:Basebox_ok # only in the binaries\n',
                   BRAND_ORIGINAL_SHA256=hashlib.sha256(old_logo).hexdigest() + ' bitmaps/logo.bmp\n',
                   ARTIFACTS_N='package.zip firmware.ipod', ARTIFACTS_B='bootloader-*.ipod',
                   CHECK_TARGET='model', CHECK_TYPES=tree.get('types', 'N,B'), CHECK_OUT=out,
                   CHECK_SRC=src, CHECK_REFERENCES=reference, CHECK_PLUGINS=tree.get('plugins', 'false'),
                   CHECK_FOLDER=tree.get('folder', ''),
                   CHECK_PLUGINS_SH=os.environ['plugins_sh'], CHECK_PLUGIN_LIST=plugin_list)
        result = subprocess.run([sys.executable, '-c', os.environ['check_py']], env=env,
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        ok = result.returncode == 0 and expect is None or result.returncode != 0 and expect is not None and expect in output
    if ok:
        passed += 1
        print('ok    %s' % name)
    else:
        failed += 1
        print('FAIL  %s (expected %s)\n%s' % (name, expect or 'a pass', output))

def put(where, path, data):
    def change(tree):
        tree[where][path] = data
    return change

def drop(where, path):
    def change(tree):
        del tree[where][path]
    return change

def setting(key, value):
    def change(tree):
        tree[key] = value
    return change

run('good outputs pass', lambda tree: None, None)
run('rule 1: a missing output fails', drop('out', 'model-bootloader-model.ipod'), 'output missing: model-bootloader-*.ipod')
run('rule 1: an output older than base.lock fails', setting('old', ['model-firmware.ipod']), 'model-firmware.ipod: older than')
run('rule 1: an unknown type fails', setting('types', 'N,X'), 'unknown build type: X')
run('rule 2: the name in the firmware fails', put('out', 'model-firmware.ipod', b'\0Basebox 1.0\0'), "model-firmware.ipod: string shows the name: 'Basebox 1.0'")
run('rule 2: any spelling of the name fails', put('out', 'model-bootloader-model.ipod', b'\0BASEBOKS\0'), "string shows the name: 'BASEBOKS'")
run('rule 2: the name in a plugin fails', put('zip', '.base/rocks/games/blocks.rock', b'\0Basebox game\0'), "blocks.rock: string shows the name: 'Basebox game'")
run('rule 2: the name in a codec fails', put('zip', '.base/codecs/mpa.codec', b'\0by basebox\0'), "mpa.codec: string shows the name")
run('rule 2: the name in a language fails', put('zip', '.base/langs/english.lng', b'\0About Basebox\0'), "english.lng: string shows the name")
run('rule 2: a piece allowed in one kind of file only fails elsewhere', put('zip', '.base/rocks/games/blocks.rock', b'\0Basebox_ok\0'), "string shows the name: 'Basebox_ok'")
run('rule 2: a tolerated piece inside a message fails', put('out', 'model-firmware.ipod', b'\0No Basebox_ok here\0'), "string shows the name: 'No Basebox_ok here'")
run('rule 2: a tolerated piece after a symbol passes', put('out', 'model-firmware.ipod', b'\0#! Basebox_ok/2.0\0'), None)
run('rule 2: a string with the name next to a tolerated piece fails', put('out', 'model-firmware.ipod', b'\0/.base/basebox.cfg\0'), "string shows the name: '/.base/basebox.cfg'")
run('rule 3: the name in a zip path fails', put('zip', '.base/basebox_sounds/a.wav', b''), 'path shows the name: .base/basebox_sounds/a.wav')
run('rule 3: "rock" in a plugin name fails', put('zip', '.base/rocks/games/rockgame.rock', b''), 'plugin name has "rock": .base/rocks/games/rockgame.rock')
run('rule 3: "rock" in an overlay name fails', put('zip', '.base/rocks/games/spacerock.ovl', b''), 'plugin name has "rock"')
run('rule 4: a logo left as the base file fails', put('src', 'bitmaps/logo.bmp', old_logo), 'bitmaps/logo.bmp: still the base file')
def with_folder(*changes):
    def change(tree):
        tree['folder'] = '.base'
        for c in changes:
            c(tree)
    return change

def with_plugins(*changes):
    def change(tree):
        tree['plugins'] = 'true'
        for c in changes:
            c(tree)
    return change

run('rule 5: plugins as listed pass', with_plugins(), None)
run('rule 5: a plugin not in the list fails', with_plugins(put('zip', '.base/rocks/games/extra.rock', b'')), 'plugin not in the plugin list: games/extra')
run('rule 5: a listed plugin missing fails', with_plugins(setting('list', b'games/blocks\ngames/maze\n')), 'listed plugin missing: games/maze')
run('rule 5: a plugin left with its old name fails', with_plugins(setting('list', b'games/oldblocks blocks\n'), put('zip', '.base/rocks/games/oldblocks.rock', b''), drop('zip', '.base/rocks/games/blocks.rock')), 'plugin not in the plugin list: games/oldblocks')
run('rule 5: an association to a missing plugin fails', with_plugins(put('zip', '.base/viewers.config', b'txt,viewers/gone,1\n')), 'viewers.config names a missing plugin: viewers/gone')
run('rule 6: a zip in the folder of the model passes', setting('folder', '.base'), None)
run('rule 6: a zip in another folder fails', setting('folder', '.mine'), 'model-package.zip: top folder is .base, not .mine')
run('rule 6: a file beside the folder fails', with_folder(put('zip', 'loose.txt', b'')), 'model-package.zip: top folder is loose.txt, not .base')

print('check-build.sh --self-test: %d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
PY
fi

[ -n "$target" ] || die "missing --target"
[ -n "$types" ] || die "missing --types (N, B or S, comma separated)"
[ -d "$out" ] || die "no build outputs in $out (run build.sh first)"

# shellcheck source=base.lock
source "$here/base.lock"
export BRAND_NAMES BRAND_TOLERATED BRAND_ORIGINAL_SHA256 ARTIFACTS_N ARTIFACTS_B ARTIFACTS_S
export CHECK_TARGET="$target" CHECK_TYPES="$types" CHECK_OUT="$out" CHECK_SRC="$src" CHECK_PLUGINS="$plugins"
export CHECK_PLUGINS_SH="$plugins_sh" CHECK_PLUGIN_LIST="$here/plugins.list"
CHECK_FOLDER="$(bash "$here/firmware-folder.sh" "$target")"
export CHECK_FOLDER
CHECK_REFERENCES="$(printf '%s\n' "$here/base.lock" "$here"/patches/*)"
export CHECK_REFERENCES

exec python3 -c "$check_py"
