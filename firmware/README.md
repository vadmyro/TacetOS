# firmware

The TacetOS firmware is **not a diverging fork**: it is a short stack of patches on top of a pinned base.

| File | Role |
|---|---|
| `base.lock` | The base sources and the exact commit we build on |
| `patches/` | Our patches, applied in order |
| `backports.md` | Newer features brought in one at a time |
| `version.sh` | Prints the TacetOS version, the same for every model, its packages and the TacetOS line of its info file |
| `release-index.sh` | Checks the packages of a release and writes its index (`docs/specs/update-channel.md`, section 3) |
| `follow.sh` | Tries our backports, patches and branding on the newest commit of the base's development branch and writes a report of the conflicts |
| `apply.sh` | Downloads the base at the locked commit, applies the patches, then runs `brand.sh` |
| `brand.sh` | Puts the TacetOS name and logo in place of the base's, in the language files and the images, and writes the built-in boot logo (needs Python 3 with Pillow) |
| `build.sh` | Builds the firmware, bootloader or simulator for one model, or only the ARM toolchain |
| `firmware-folder.sh` | Prints the firmware folder of a model from `models/models.toml`, empty for the default one |
| `plugins.list` | The plugins the firmware keeps, and their TacetOS names |
| `plugins.sh` | Applies `plugins.list` to the zip of a firmware build and notes the listed plugins the base does not build for that model; `build.sh` runs it |
| `check-build.sh` | Checks that the outputs of a build never show the base's name or logo; `--self-test` proves each rule |
| `package.sh` | Turns the firmware and bootloader of one model into a package the app installs |

Firmware builds run in CI.

## Following the base

`follow.sh` answers one question: would our work still apply on the newest base? It fetches the newest commit of the development branch (`BASE_DEV_BRANCH` in `base.lock`), tries the backports, then each patch, then the branding, and writes `firmware/work/follow-report.md`: one line per backport and patch (applies, conflict, or already in the newest commit for a backport), the first lines of each failure, and the result of the branding check. A patch that conflicts is skipped and the next ones are still tried. It changes nothing in the repository and exits with 1 when something conflicts. A weekly CI job (`follow-base.yml`) runs it and shows the report in the summary of the run. The pinned base only moves by hand, after reading the report.

## Package

A package is what the app installs on an iPod. CI makes one for every model of `models/models.toml` on every run (job `firmware`, one artifact per model named `tacetos-<target>`, such as `tacetos-ipod6g`, kept 30 days). The ARM toolchain is built once, cached, and shared by the ten builds (`build.sh --toolchain-only`). To make one locally:

```sh
bash firmware/apply.sh
bash firmware/build.sh --target ipod6g --type N
bash firmware/build.sh --target ipod6g --type B
bash firmware/check-build.sh --target ipod6g --types N,B --plugins
bash firmware/package.sh --target ipod6g
```

The build and the package take their version from `version.sh` (or both from `--version`). The build writes it in the info file of the firmware folder as the line `TacetOS: <version>` (patch 0015), which the app reads to tell a TacetOS firmware from another build of the base (`docs/specs/detection.md`, section 7.3).

CI also builds the simulator of every model (job `simulator`, one artifact per model named `tacetos-sim-<target>`, kept 30 days). It is a test tool, so it is not part of a package: a later step runs it to compare the theme previews with the real screens. Locally, `build.sh --target ipod6g --type S` needs the SDL 2 development files, and `check-build.sh --target ipod6g --types S` checks it.

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
| `version` | text | Given by `version.sh` (`docs/specs/update-channel.md`, section 5): `X.Y.Z` on a tag `vX.Y.Z`, else `X.Y.Z-dev+g` and the short commit of this repository (`0.3.0-dev+g5783155a1b2c`). `X.Y.Z` is the version of `project()` in `CMakeLists.txt`. Letters, digits, `.`, `+` and `-` only |
| `baseCommit` | text | The base commit the firmware was built on (`base.lock`) |
| `backports` | list of text | The commits brought in on top of it, in order (`base.lock`) |
| `patches` | text | sha256 of our patch set: for each file of `patches/` in the order they are applied, its name, a line feed, then its content |
| `built` | text | When the package was made, UTC, ISO 8601 (`2026-11-15T18:30:00Z`). Taken from `SOURCE_DATE_EPOCH` when it is set |
| `files` | list | One object per file of the package: `name` (file name, no folder), `kind` (`firmware` or `bootloader`), `size` (bytes), `sha256` (lowercase hexadecimal) |

The app checks `format`, then that `target` is the model of the iPod, then the size and sha256 of every file, before it touches the iPod. A package with any other file, or a missing one, is refused.
