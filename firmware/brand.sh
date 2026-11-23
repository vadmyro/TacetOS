#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Puts the TacetOS name and logo in place of the base's, in the sources made by
# apply.sh (docs/specs/firmware-branding.md, sections 4.1 and 4.2). Every name and
# file it works on is read from base.lock. Running it twice gives the same files.
#
# Usage: brand.sh [--src DIR] | --check DIR
#   --src DIR    the sources to change (default: firmware/work/src)
#   --check DIR  change nothing; exit 1 and list what still shows the base in DIR
#
# Needs Python 3 with Pillow.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/work/src"
mode=apply

while [ $# -gt 0 ]; do
    case "$1" in
        --src) src="${2:?--src needs a directory}"; shift 2 ;;
        --check) src="${2:?--check needs a directory}"; mode=check; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "brand.sh: unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -d "$src/apps/lang" ] || { echo "brand.sh: $src does not hold the base sources" >&2; exit 2; }
python3 -c 'import PIL' 2>/dev/null || { echo "brand.sh: needs Python 3 with Pillow" >&2; exit 2; }

# shellcheck source=base.lock
source "$here/base.lock"
export BRAND_NAMES BRAND_NEW BRAND_LOGO_IMAGES BRAND_ICON_SETS BRAND_ORIGINAL_SHA256

exec python3 - "$mode" "$src" "$here/../docs/assets/logo" <<'PY'
import hashlib, os, re, struct, sys
from PIL import Image

mode, src, logo_dir = sys.argv[1:4]

def lines(key):
    return [l.split('#')[0].strip() for l in os.environ.get(key, '').splitlines() if l.split('#')[0].strip()]

names = re.compile('|'.join('(?:%s)' % n for n in lines('BRAND_NAMES')), re.IGNORECASE)
new_name = os.environ['BRAND_NEW']
logos = lines('BRAND_LOGO_IMAGES')
icon_sets = lines('BRAND_ICON_SETS')
originals = {path: digest for digest, path in (l.split(None, 1) for l in lines('BRAND_ORIGINAL_SHA256'))}
# The base's mark is the 32nd icon of a set; a set holds 32 icons stacked from the top.
MARK_SLOT, SET_ICONS = 31, 32

problems = []

# Language files: only the displayed text (<dest>) and the voice text (<voice>).
def lang_files():
    folder = os.path.join(src, 'apps', 'lang')
    return sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.endswith('.lang'))

def brand_lang(path, fix):
    with open(path, encoding='utf-8', newline='') as f:
        text = f.readlines()
    inside, changed = False, False
    for i, line in enumerate(text):
        tag = line.strip()
        if tag in ('<dest>', '<voice>'):
            inside = True
        elif tag in ('</dest>', '</voice>'):
            inside = False
        elif inside and names.search(line):
            if fix:
                text[i] = names.sub(new_name, line)
                changed = True
            else:
                problems.append('%s:%d: %s' % (os.path.relpath(path, src), i + 1, tag))
    if changed:
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.writelines(text)

# Logos: a new BMP at the size and depth written in the file name, built from our banner.
def brand_logo(rel):
    m = re.search(r'\.(\d+)x(\d+)x(\d+)\.bmp$', rel)
    if not m:
        sys.exit('brand.sh: no size in the logo name %s' % rel)
    width, height, depth = map(int, m.groups())
    colour = depth >= 16
    banner = Image.open(os.path.join(logo_dir, 'banner-dark.png' if colour else 'banner-light.png')).convert('RGB')
    scale = min(width / banner.width, height / banner.height)
    fitted = banner.resize((max(1, round(banner.width * scale)), max(1, round(banner.height * scale))), Image.Resampling.LANCZOS)
    canvas = Image.new('RGB', (width, height), banner.getpixel((0, 0)))
    canvas.paste(fitted, ((width - fitted.width) // 2, (height - fitted.height) // 2))
    if colour:
        canvas.save(os.path.join(src, rel), 'BMP')
    else:
        # Four grey levels, as the screen shows them.
        canvas.convert('L').point(lambda v: round(v / 85) * 85).save(os.path.join(src, rel), 'BMP')

# Icon sets: only the pixels of the mark's slot change, in the file's own format,
# so the header, the palette and every other icon stay as they were.
def read_bmp(path):
    data = bytearray(open(path, 'rb').read())
    offset, = struct.unpack_from('<I', data, 10)
    header, width, height, _, bpp, compression = struct.unpack_from('<IiiHHI', data, 14)
    if bpp == 32:
        masks = struct.unpack_from('<4I', data, 54) if compression == 3 else None
        if masks != (0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000):
            sys.exit('brand.sh: %s: 32-bit layout not handled' % path)
    elif compression != 0 or bpp not in (4, 24):
        sys.exit('brand.sh: %s: %d-bit BMP not handled' % (path, bpp))
    palette = []
    if bpp == 4:
        start = 14 + header
        palette = [tuple(data[start + 4 * i + 2 - c] for c in range(3)) for i in range(16)]
    return data, offset, width, height, bpp, palette

def brand_icon_set(rel):
    path = os.path.join(src, rel)
    data, offset, width, height, bpp, palette = read_bmp(path)
    rows = abs(height)
    if rows % SET_ICONS:
        sys.exit('brand.sh: %s: %d rows do not hold %d icons' % (rel, rows, SET_ICONS))
    size = (width, rows // SET_ICONS)
    icon = Image.open(os.path.join(logo_dir, 'icon-512.png')).convert('RGBA').resize(size, Image.Resampling.LANCZOS)
    stride = (width * bpp + 31) // 32 * 4
    top = MARK_SLOT * size[1]
    for y in range(size[1]):
        row = top + y
        base = offset + stride * (rows - 1 - row if height > 0 else row)
        for x in range(width):
            r, g, b, a = icon.getpixel((x, y))
            if bpp == 32:
                data[base + 4 * x: base + 4 * x + 4] = bytes((b, g, r, a))
                continue
            # No alpha in these sets: the icon is laid on white, then the nearest colour is used.
            r, g, b = (round((c * a + 255 * (255 - a)) / 255) for c in (r, g, b))
            if bpp == 24:
                grey = 255 if (r + g + b) / 3 >= 128 else 0
                data[base + 3 * x: base + 3 * x + 3] = bytes((grey, grey, grey))
            else:
                index = min(range(len(palette)), key=lambda i: sum((p - q) ** 2 for p, q in zip(palette[i], (r, g, b))))
                pos = base + x // 2
                data[pos] = (data[pos] & 0x0f) | (index << 4) if x % 2 == 0 else (data[pos] & 0xf0) | index
    open(path, 'wb').write(bytes(data))

def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()

for path in lang_files():
    brand_lang(path, mode == 'apply')

for rel in logos + icon_sets:
    path = os.path.join(src, rel)
    if not os.path.isfile(path):
        problems.append('%s: missing' % rel)
    elif rel not in originals:
        problems.append('%s: no original sha256 in base.lock' % rel)
    elif mode == 'apply':
        (brand_logo if rel in logos else brand_icon_set)(rel)
    elif sha256(path) == originals[rel]:
        problems.append("%s: still the base's image" % rel)

for p in problems:
    print(p)
if problems:
    sys.exit('brand.sh: %d problem(s) in %s' % (len(problems), src))
print('brand.sh: %s done in %s' % ('check' if mode == 'check' else 'branding', src))
PY
