#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Prints the firmware folder of a build target, as `firmware_folder` of its model
# in models/models.toml (docs/specs/firmware-branding.md section 8.2). Prints
# nothing when the model has none and uses the default folder.
#
# Usage: firmware-folder.sh TARGET [MODELS_TOML]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:?usage: firmware-folder.sh TARGET [MODELS_TOML]}"
models="${2:-$here/../models/models.toml}"

exec python3 - "$models" "$target" <<'PY'
import re, sys, tomllib

with open(sys.argv[1], 'rb') as f:
    models = tomllib.load(f).get('model', [])
for model in models:
    if model.get('bootloader_target') == sys.argv[2]:
        folder = model.get('firmware_folder', '')
        if folder and not re.fullmatch(r'\.[A-Za-z0-9_-]+', folder):
            sys.exit('firmware-folder.sh: bad firmware_folder %r for %s' % (folder, sys.argv[2]))
        print(folder)
PY
