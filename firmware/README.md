# firmware

The TacetOS firmware is **not a diverging fork**: it is a short stack of patches on top of a pinned base.

| File | Role |
|---|---|
| `base.lock` | The base sources and the exact commit we build on |
| `patches/` | Our patches, applied in order |
| `backports.md` | Newer features brought in one at a time |
| `apply.sh` | Downloads the base at the locked commit, applies the patches, then runs `brand.sh` |
| `brand.sh` | Puts the TacetOS name and logo in place of the base's, in the language files and the images (needs Python 3 with Pillow) |
| `build.sh` | Builds the firmware, bootloader or simulator for one model |
| `firmware-folder.sh` | Prints the firmware folder of a model from `models/models.toml`, empty for the default one |
| `plugins.list` | The plugins the firmware keeps, and their TacetOS names |
| `plugins.sh` | Applies `plugins.list` to the zip of a firmware build; `build.sh` runs it |
| `check-build.sh` | Checks that the outputs of a build never show the base's name or logo; `--self-test` proves each rule |
| `package.sh` | Turns the firmware and bootloader of one model into a package the app installs |

Firmware builds run in CI.

## Package

A package is what the app installs on an iPod. CI makes one for the iPod Classic on every run (job `firmware`, artifact `tacetos-ipod6g`, kept 30 days); the other models come later. To make one locally:

```sh
bash firmware/apply.sh
bash firmware/build.sh --target ipod6g --type N
bash firmware/build.sh --target ipod6g --type B
bash firmware/check-build.sh --target ipod6g --types N,B --plugins
bash firmware/package.sh --target ipod6g
```

A package is three files side by side, all named after the model and the version:

| File | Content |
|---|---|
| `tacetos-<target>-<version>.zip` | The firmware, as the build makes it (unchanged) |
| `tacetos-bootloader-<target>-<version>.<ext>` | The bootloader file of that model |
| `tacetos-<target>-<version>.json` | The manifest |

The manifest is a JSON object:

| Key | Type | Meaning |
|---|---|---|
| `format` | number | Version of this format, `1`. The app refuses any other value |
| `target` | text | The model the files are for, as `bootloader_target` in `models/models.toml` (`ipod6g`) |
| `version` | text | The project version from `CMakeLists.txt`, then `+g` and the short commit of this repository (`0.0.1+g5783155a1b2c`). Letters, digits, `.`, `+` and `-` only |
| `baseCommit` | text | The base commit the firmware was built on (`base.lock`) |
| `backports` | list of text | The commits brought in on top of it, in order (`base.lock`) |
| `patches` | text | sha256 of our patch set: for each file of `patches/` in the order they are applied, its name, a line feed, then its content |
| `built` | text | When the package was made, UTC, ISO 8601 (`2026-11-15T18:30:00Z`). Taken from `SOURCE_DATE_EPOCH` when it is set |
| `files` | list | One object per file of the package: `name` (file name, no folder), `kind` (`firmware` or `bootloader`), `size` (bytes), `sha256` (lowercase hexadecimal) |

The app checks `format`, then that `target` is the model of the iPod, then the size and sha256 of every file, before it touches the iPod. A package with any other file, or a missing one, is refused.
