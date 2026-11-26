#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Writes the index of a release (docs/specs/update-channel.md, section 3) from the
# packages in one folder. It fails, listing each problem, without writing the index, when:
#
# 1. a manifest cannot be read, or its format is not 1;
# 2. the version of a manifest is not the version asked for;
# 3. a file of a manifest is not beside it, or its size or sha256 differs;
# 4. a model of models/models.toml has no manifest, or has several;
# 5. a manifest is for a target that is not in models/models.toml.
#
# Usage: release-index.sh --dir DIR --version X.Y.Z | --self-test
#   --dir DIR        the folder with the packages, flat; the index is written there
#   --version X.Y.Z  the version of the release
#   --self-test      prove each rule on small files made on the fly; needs no build
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

dir=""
version=""
self_test=false

die() { echo "release-index.sh: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --dir)       dir="${2:?--dir needs a folder}"; shift 2 ;;
        --version)   version="${2:?--version needs X.Y.Z}"; shift 2 ;;
        --self-test) self_test=true; shift ;;
        -h|--help)   sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

# The index itself. It reads its inputs from the environment, so that the self-test runs
# the very same code on its own small files and its own model list.
read -r -d '' index_py <<'PY' || true
import datetime, glob, hashlib, json, os, sys, tomllib

env = os.environ
folder, version = env['INDEX_DIR'], env['INDEX_VERSION']
index_name = 'tacetos-index.json'

with open(env['INDEX_MODELS'], 'rb') as f:
    targets = [m['bootloader_target'] for m in tomllib.load(f)['model']]

def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()

problems = []
found = {}
for path in sorted(glob.glob(os.path.join(glob.escape(folder), 'tacetos-*.json'))):
    name = os.path.basename(path)
    if name == index_name:
        continue
    try:
        with open(path, encoding='utf-8') as f:
            manifest = json.load(f)
        if not isinstance(manifest, dict):
            raise ValueError('not an object')
    except (OSError, ValueError) as e:
        problems.append('%s: cannot be read (%s)' % (name, e))
        continue
    # Rule 1.
    if manifest.get('format') != 1:
        problems.append('%s: format is %r, not 1' % (name, manifest.get('format')))
        continue
    # Rule 2.
    if manifest.get('version') != version:
        problems.append('%s: version %s, not %s' % (name, manifest.get('version'), version))
    # Rule 3.
    for entry in manifest.get('files', []):
        file = os.path.join(folder, os.path.basename(str(entry.get('name', ''))))
        if not os.path.isfile(file):
            problems.append('%s: %s is missing' % (name, entry.get('name')))
        elif os.path.getsize(file) != entry.get('size') or sha256(file) != entry.get('sha256'):
            problems.append('%s: %s has another size or sha256' % (name, entry.get('name')))
    found.setdefault(manifest.get('target'), []).append(name)

# Rules 4 and 5.
for target in targets:
    if target not in found:
        problems.append('no package for %s' % target)
    elif len(found[target]) > 1:
        problems.append('several packages for %s: %s' % (target, ', '.join(found[target])))
for target in sorted(set(found) - set(targets), key=str):
    problems.append('%s: not a model of models.toml' % ', '.join(found[target]))

if problems:
    for p in problems:
        print(p)
    sys.exit('release-index.sh: %d problem(s), no index written' % len(problems))

built = env.get('SOURCE_DATE_EPOCH')
moment = datetime.datetime.fromtimestamp(int(built), datetime.timezone.utc) if built else datetime.datetime.now(datetime.timezone.utc)
index = {
    'format': 1,
    'version': version,
    'built': moment.strftime('%Y-%m-%dT%H:%M:%SZ'),
    'packages': [],
}
for target in targets:
    path = os.path.join(folder, found[target][0])
    index['packages'].append({'target': target, 'manifest': found[target][0],
                              'size': os.path.getsize(path), 'sha256': sha256(path)})
with open(os.path.join(folder, index_name), 'w', encoding='utf-8') as f:
    json.dump(index, f, indent=2)
    f.write('\n')
print('release-index.sh: %s written, %d packages' % (index_name, len(targets)))
PY
export index_py

if [ "$self_test" = true ]; then
    exec python3 - <<'PY'
import hashlib, json, os, subprocess, sys, tempfile

passed = failed = 0

def package(folder, target, version, change=None):
    """Write the three files of one package, as package.sh makes them."""
    files = {'tacetos-%s-%s.zip' % (target, version): b'zip of ' + target.encode(),
             'tacetos-bootloader-%s-%s.ipod' % (target, version): b'boot of ' + target.encode()}
    for name, data in files.items():
        with open(os.path.join(folder, name), 'wb') as f:
            f.write(data)
    manifest = {'format': 1, 'target': target, 'version': version,
                'files': [{'name': n, 'kind': 'firmware' if n.endswith('.zip') else 'bootloader',
                           'size': len(d), 'sha256': hashlib.sha256(d).hexdigest()}
                          for n, d in files.items()]}
    if change:
        change(manifest, folder)
    with open(os.path.join(folder, 'tacetos-%s-%s.json' % (target, version)), 'w') as f:
        json.dump(manifest, f)

def run(name, setup, expect):
    """Make two good packages, apply one change, run the index, compare with what is expected."""
    global passed, failed
    with tempfile.TemporaryDirectory() as tmp:
        folder = os.path.join(tmp, 'release')
        os.mkdir(folder)
        models = os.path.join(tmp, 'models.toml')
        with open(models, 'w') as f:
            f.write('[[model]]\nbootloader_target = "one"\n[[model]]\nbootloader_target = "two"\n')
        setup(folder)
        env = dict(os.environ, INDEX_DIR=folder, INDEX_VERSION='1.2.3', INDEX_MODELS=models,
                   SOURCE_DATE_EPOCH='0')
        result = subprocess.run([sys.executable, '-c', os.environ['index_py']], env=env,
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        index = os.path.join(folder, 'tacetos-index.json')
        if expect is None:
            ok = result.returncode == 0 and os.path.isfile(index)
            if ok:
                data = json.load(open(index))
                manifest = os.path.join(folder, 'tacetos-one-1.2.3.json')
                ok = (data['format'] == 1 and data['version'] == '1.2.3'
                      and data['built'] == '1970-01-01T00:00:00Z'
                      and [p['target'] for p in data['packages']] == ['one', 'two']
                      and data['packages'][0]['manifest'] == 'tacetos-one-1.2.3.json'
                      and data['packages'][0]['size'] == os.path.getsize(manifest)
                      and data['packages'][0]['sha256'] == hashlib.sha256(open(manifest, 'rb').read()).hexdigest())
        else:
            ok = result.returncode != 0 and expect in output and not os.path.exists(index)
    if ok:
        passed += 1
        print('ok    %s' % name)
    else:
        failed += 1
        print('FAIL  %s (expected %s)\n%s' % (name, expect or 'an index', output))

def good(folder):
    package(folder, 'one', '1.2.3')
    package(folder, 'two', '1.2.3')

def with_one(change):
    def setup(folder):
        package(folder, 'one', '1.2.3', change)
        package(folder, 'two', '1.2.3')
    return setup

def key(k, v):
    def change(manifest, folder):
        manifest[k] = v
    return change

def damage(manifest, folder):
    with open(os.path.join(folder, manifest['files'][0]['name']), 'ab') as f:
        f.write(b'!')

def remove(manifest, folder):
    os.remove(os.path.join(folder, manifest['files'][1]['name']))

def unreadable(folder):
    good(folder)
    with open(os.path.join(folder, 'tacetos-one-1.2.3.json'), 'w') as f:
        f.write('{ not json')

def twice(folder):
    good(folder)
    package(folder, 'two', '1.2.4', key('version', '1.2.3'))

def stranger(folder):
    good(folder)
    package(folder, 'three', '1.2.3')

run('good packages give the index', good, None)
run('rule 1: a manifest that cannot be read fails', unreadable, 'tacetos-one-1.2.3.json: cannot be read')
run('rule 1: a manifest of another format fails', with_one(key('format', 2)), 'format is 2, not 1')
run('rule 2: a manifest of another version fails', with_one(key('version', '1.2.4')), 'version 1.2.4, not 1.2.3')
run('rule 3: a damaged file fails', with_one(damage), 'tacetos-one-1.2.3.zip has another size or sha256')
run('rule 3: a missing file fails', with_one(remove), 'tacetos-bootloader-one-1.2.3.ipod is missing')
run('rule 4: a model without a package fails', lambda folder: package(folder, 'one', '1.2.3'), 'no package for two')
run('rule 4: a model with two packages fails', twice, 'several packages for two')
run('rule 5: a package of an unknown model fails', stranger, 'tacetos-three-1.2.3.json: not a model of models.toml')

print('release-index.sh --self-test: %d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
PY
fi

[ -n "$dir" ] || die "missing --dir"
[ -n "$version" ] || die "missing --version"
[ -d "$dir" ] || die "no folder $dir"

INDEX_DIR="$dir" INDEX_VERSION="$version" INDEX_MODELS="$here/../models/models.toml" exec python3 -c "$index_py"
