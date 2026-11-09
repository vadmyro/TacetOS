# models

`models.toml` is the list of iPods that TacetOS knows: how to recognise each one on USB, what its screen and storage are, and how the bootloader goes on it. The app reads it to detect a plugged iPod and to choose the right install path. The docs read it too: each model has a page in `docs/models/`, written from `docs/models/_template.md`.

Only **facts** go in this file, in our own format, each with the sources it was checked against. Nothing is copied from another project's files.

Status: the format is defined and all ten models are filled: the iPod 1G/2G, 3G, 4G, Color and Video, the Mini 1G and 2G, the Nano 1G and 2G, and the Classic 6G (the example). Each model also lists its variants (capacity, release period, Apple model numbers), which the app uses to name the exact iPod. The facts still weak are listed in each model's `to_verify`, and the facts on which sources disagree are in "Contradicting facts" below. `tests/models/tst_models.cpp` reads `models.toml` and checks that the ten models and all their fields are there.

## File layout

The file has three parts, in this order:

1. `format_version`: an integer, currently `1`. It changes only when a field is renamed or removed.
2. `[sources.<id>]`: one table per source that facts point to.
3. `[[model]]`: one table per iPod model, with sub-tables.

## Sources

Every fact of a model lists the ids of the sources it was read in. A source is declared once and reused.

| Field | Type | Meaning |
|---|---|---|
| `kind` | string | `database` (public id list), `firmware-sources`, `installer-sources`, `wiki`, `datasheet` or `measured` (read on a real iPod) |
| `title` | string | What the source is, in a few words |
| `location` | string | A URL, or a path inside the firmware base described in `firmware/base.lock` |
| `checked` | date | The day it was read, `YYYY-MM-DD` |

## Model fields

| Field | Type | Meaning |
|---|---|---|
| `id` | string | Stable short name, lowercase with dashes, for example `classic-6g`. Used in file names (`docs/models/<id>.md`) and never changed once published |
| `family` | string | One of `ipod` (the full-size line: 1G to 4G, Color, Video), `mini`, `nano`, `classic` |
| `name` | string | The name shown to the user |
| `bootloader_target` | string | The name of the build target, as passed to `firmware/build.sh --target`. It also names the bootloader built for this model |
| `to_verify` | array of strings | Facts that still need a second source or a check on a real iPod. Empty when everything is confirmed. Entries are fact names (see below) |

### `[model.usb]`

USB ids are written `vendor:product`, four lowercase hexadecimal digits each, for example `05ac:1261`. Each key is a list, because some models show more than one id for the same mode.

| Key | Meaning |
|---|---|
| `normal` | The id when the iPod is plugged in and idle or syncing |
| `disk_mode` | The id in Disk Mode (the iPod shows up as a plain drive) |
| `dfu` | The id in DFU mode, the low-level mode used to install the bootloader on models that need it |

An empty list means the mode does not exist on this model. A key that is missing means nobody has checked it yet, which is different. The app treats both as "cannot detect this mode by id".

### `[model.screen]`

| Key | Type | Meaning |
|---|---|---|
| `width` | integer | Pixels |
| `height` | integer | Pixels |
| `depth_bits` | integer | Bits per pixel (2 for four-level grayscale, 16 for the Classic) |
| `colour` | boolean | `true` for a colour screen |
| `diagonal_inches` | float | Diagonal of the visible area, used with the pixels to compute density. Optional |

Themes are filtered by `width` and `height`.

### `[model.storage]`

| Key | Type | Meaning |
|---|---|---|
| `kind` | string | `hdd` (spinning disk) or `flash` |
| `form_factor` | string | `1.8in`, `1in` (the Mini microdrive) or `chip` |
| `interface` | string | `ata` for the disk models and the Nano 1G (flash seen as an ATA drive), `nand` for raw flash (Nano 2G) |
| `capacities_gb` | array of integers | Capacities Apple sold, in GB, ascending. A user may have swapped the storage, so this is a hint only |

### `[model.install]`

| Key | Type | Meaning |
|---|---|---|
| `method` | string | `partition` (older models: the bootloader is written into the firmware partition of the disk) or `dfu` (the Classic and the later Nano: the bootloader is sent over USB in DFU mode) |
| `dual_boot_required` | boolean | `true` when only a dual-boot install is allowed on this model. The installer refuses a single boot install when this is `true`. Always `true` for the Classic |

### `[model.variant.<key>]`

One table per variant of a model: a capacity that Apple sold, in one release period. The app uses them to show the exact iPod ("iPod Classic 120 GB (late 2008)") and never to decide what is written to it (detection.md section 8.4). The key is lowercase letters, digits and dashes, no dot, and is never changed once published.

| Key | Type | Meaning |
|---|---|---|
| `name` | string | The name shown to the user, with the capacity. Two variants of one model may share it when only the release period tells them apart (the two Classic 160 GB) |
| `capacity_gb` | integer | The capacity Apple printed on the box, in GB. It must be one of the `capacities_gb` of the model |
| `released` | string | When it was first sold, as shown to the user: a year, or "late 2008". A hint, never evidence |
| `apple_models` | array of strings | The Apple model numbers of this variant (all colours and special editions), four characters, upper case, without the letter that starts the string in the iPod's own file (the region letter) |
| `sources` | array of strings | Source ids, as for the facts of the model |
| `to_verify` | array of strings | The keys among `capacity_gb`, `released` and `apple_models` that still lack a second source or a check on a real iPod |

A model number appears in one variant only, in the whole file. A variant is **confirmed** when it has two sources and its `to_verify` is empty. No source we could read gives release dates, so `released` is in `to_verify` for every variant until someone reads Apple's own pages.

### `[model.sources]`

One key per fact, each a list of source ids declared in the `[sources.*]` tables. The fact names are:

| Fact name | Covers |
|---|---|
| `family`, `name` | The identity of the model |
| `usb_normal`, `usb_disk_mode`, `usb_dfu` | The three keys of `[model.usb]` |
| `screen` | The whole `[model.screen]` table |
| `storage` | The whole `[model.storage]` table |
| `variants` | All the `[model.variant.*]` tables of the model |
| `install_method` | `[model.install]` |
| `bootloader_target` | The build target name |

A fact is **confirmed** when it has at least two sources and is not listed in `to_verify`. Anything else is a fact still to check. The release checklist requires `to_verify` to be empty for every model that the release calls supported.

## Rules for adding a fact

- Read it in at least two independent places, and list both. A header file and a manual page written from the same header count as one place only when they were derived from each other.
- If sources disagree, do not pick one silently: keep the fact in `to_verify` and write the disagreement in the model page.
- Write the fact in your own words and your own structure. Do not paste a file from another project.
- Prefer a measurement on a real iPod (`kind = "measured"`) over anything else, and say which iPod and which day in the model page.
- Keep ids lowercase and in the `vendor:product` shape, so a plain text compare works.

## Adding a model

1. Copy the example `[[model]]` block and change every value.
2. Add the sources it needs under `[sources.*]` if they are not there yet.
3. Copy `docs/models/_template.md` to `docs/models/<id>.md` and fill it.
4. Check that the file still parses as TOML and that every id in `[model.sources]` is declared.

## Contradicting facts

Result of the cross review of `models.toml` (2026-11-08). Each entry is a place where two sources do not say the same thing, or where one fact cannot be told apart from another. None of them is settled by guessing: the fact stays in `to_verify` until a real iPod is tested.

| Model | Fact | What disagrees | What the file says |
|---|---|---|---|
| `nano-2g` | `usb_dfu` | The id database calls `05ac:1240` "DFU mode". The DFU tool calls it "WTF mode" and lists `05ac:1220` as the DFU id | Both ids are kept in `dfu`. The install does not use DFU on this model, so nothing depends on it |
| `ipod-1g2g` | `usb_normal`, `usb_disk_mode` | The installer device list says these models have no USB (FireWire only). The id database lists an id for the 2nd generation | Empty lists, and both facts are in `to_verify` |
| `mini-1g`, `mini-2g` | `usb_normal` | Not a disagreement, but both generations show the same id `05ac:1205`, so USB cannot tell them apart | Same id in both models. The app asks the user, or reads the disk contents |
| `ipod-4g` | `bootloader_target` | One tool of the firmware base uses another internal name for this target | `ipod4g`, the build target and bootloader file name |
| `ipod-video` | `name` | The same hardware was sold as Video, then as Classic in 2007. The id is the same | One model, named Video. It must not be confused with `classic-6g`, which has another id and another install method |
| all except `ipod-1g2g` | `usb_disk_mode` | The id database lists one id per model without saying which mode it is. Disk Mode is assumed to share the normal id | Same id as `normal`, and the fact is in `to_verify` |
| `ipod-4g` | `variants` | The table of model numbers gives 25 GB for the U2 special edition (`9787`). No 4th generation was sold with that capacity, and the `capacities_gb` of the model has a 30 GB entry that no variant has | `9787` is in the 20 GB variant, and the capacity is in the `to_verify` of the variant. `capacities_gb` is left as it was until Apple's pages are read |
| all | `variants` | Only one source (the table of model numbers) gives the model numbers and the capacities, and none gives release dates. Only the Classic has a second source for its capacities (the manual of the firmware base) | `variants` is in `to_verify` of every model |
| all | `storage` | No source we could read offline gives capacities. They come from general knowledge, and `diagonal_inches` is left out | `storage` is in `to_verify` for every model |

The test checks the rule behind this table: a fact that has fewer than two sources must be listed in `to_verify`. It does not compare `models.toml` with the model pages in `docs/models/`. Those pages exist for four models only (Classic 6G, Video, Nano 1G, Nano 2G), and their values were compared with this file by hand during the review: they agree.
