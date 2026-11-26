# The update channel

How a new TacetOS firmware reaches an iPod that already runs TacetOS: what a release holds, the index the app reads, the address of the channel, how two versions compare, the download, the update itself, and where it shows on the home screen. The app updates the firmware only: the app's own updates come with the packages of the beta.

Decisions: D39 (the package), D52 (draft releases until the beta), D53 (the Classic folder), D60 (the version on the iPod), D61 (the bootloader in an update), D62 (when the app reads the channel), D63 (the version numbers).

## 1. Words

- **Package**: the three files of one model (`firmware/README.md`, "Package"): the zip of the firmware, the bootloader file and the manifest.
- **Index**: one small JSON file that lists the package of every model of a release (section 3).
- **Release**: a GitHub release of this repository, made by the CI from a tag. Until the beta it is always a **draft**: nobody but the owner of the repository sees it (D52).
- **Channel**: the place the app reads the index and the packages from. By default the latest release of this repository (section 4).
- **Local channel**: a folder on the computer, or a small HTTP server on the computer, that holds an index and its packages. For the tests and for the device test of v0.3.

## 2. The release

1. **The tag** is `vX.Y.Z`, where `X.Y.Z` is the `VERSION` of `project()` in `CMakeLists.txt`. The CI refuses a tag that differs from it, before it builds anything ("tag v0.3.1 but the project version is 0.3.0").
2. **What it builds**: the firmware and the bootloader of every model of `models/models.toml`, the same jobs as on every push (`firmware/README.md`), with the version `X.Y.Z` (section 5). Every check of a normal run must pass, the branding checks included.
3. **What it holds**: the 30 files of the 10 packages and the index, side by side, with no folder (31 files). The file names are those the packages give (`tacetos-ipod6g-0.3.0.zip`, `tacetos-bootloader-ipod6g-0.3.0.ipod`, `tacetos-ipod6g-0.3.0.json`) and `tacetos-index.json`. A release version has only letters, digits and dots, so GitHub never changes a file name. After the upload the CI reads the list of files of the release back and compares each name and size with its own files. A difference fails the run.
4. **A draft, always** (D52). The CI creates the release as a draft named "TacetOS X.Y.Z", with a body of one line: "Draft, not public before the beta." It never publishes it. Publishing is a decision of the owner, after the beta starts (P12).
5. **The same tag again**: when the tag is pushed again (moved to another commit, then pushed with force), the draft of that tag is deleted and made again, never doubled. A published release with that tag is never touched: the run fails ("v0.3.0 is already published").
6. **Rights**: only the release job has `contents: write`. Every other job keeps `contents: read`.

## 3. The index

`tacetos-index.json` is a JSON object, at most `INDEX_MAX` (64 KiB):

| Key | Type | Meaning |
|---|---|---|
| `format` | number | Version of this format, `1`. The app ignores an index with any other value |
| `version` | text | The version of every package of the index (section 5) |
| `built` | text | When the index was made, UTC, ISO 8601. Logged only |
| `packages` | list | One object per model: `target` (as `bootloader_target` in `models/models.toml`), `manifest` (file name of its manifest, no folder), `size` (bytes) and `sha256` (lowercase hexadecimal) of that manifest |

Rules:

- each `target` appears once, and every model of `models/models.toml` appears;
- the `version` of each manifest is the `version` of the index;
- the index gives the manifest by its size and sha256, and the manifest gives its two files the same way (`firmware/README.md`): a package is checked from the index down, with no file left unchecked.

`firmware/release-index.sh --dir DIR --version X.Y.Z` reads the manifests in `DIR`, checks each one (its version, its two files beside it with their size and sha256), checks that every model is there once, and writes `DIR/tacetos-index.json`. It fails, listing each problem, without writing the index. `--self-test` proves each rule on small files it makes, and needs no build. The manifest format of `firmware/README.md` does not change.

## 4. The address

**Default**: the latest release of this repository, `https://github.com/vadmyro/TacetOS/releases/latest/download/`, followed by a file name. GitHub answers it with a redirect to the file. A draft is not "latest": until the beta the default address finds nothing, and the app says nothing (section 8).

**Another address**: the environment variable `TACETOS_UPDATE_CHANNEL` replaces the default one in every build except the packaged builds of the beta (CMake option `TACETOS_PACKAGE_BUILD`, off by default, on only for those packages). Its value is one of:

- an `https://` address of a folder;
- an `http://` address on the computer itself (`127.0.0.1`, `::1` or `localhost`), for the fake server of the tests;
- the path of a folder, or a `file://` address: the local channel.

Any other value is ignored, with one log line, and the default address stays. The address in use is logged once per launch.

**Rules of the reads**:

- HTTPS only, except the cases above. A redirect is followed only to an `https://` address, `REDIRECT_MAX` (5) times at most.
- A read that gets no byte for `CHANNEL_TIMEOUT` (15 s) fails. So does a file larger than its limit: `INDEX_MAX` for the index, the `size` of the index for a manifest, the `size` of the manifest for a package file. The read stops at the limit and does not keep the rest.
- A missing file (HTTP 404, or no file in the folder) is not an error: it means "no update" (section 8).

## 5. Versions

A version is `X.Y.Z`, three numbers in decimal with no leading zero (0 alone is allowed), each at most 65535, then optionally `-dev`, then optionally `+g` and 12 lowercase hexadecimal digits (the commit). Anything else cannot be compared.

| Build | Version | Example |
|---|---|---|
| A tag `vX.Y.Z` | `X.Y.Z` | `0.3.0` |
| Any other CI run, or a local build | `X.Y.Z-dev+g<commit>` | `0.3.0-dev+g5a1b2c3d4e5f` |

`X.Y.Z` is always the `VERSION` of `project()`, which is the version of the phase in progress in `docs/ROADMAP.md` (D63): `0.3.0` during v0.3, `0.4.0` from the first change of v0.4. The firmware build and the package take the same version from one place (`firmware/README.md`), so the TacetOS line of the info file and the manifest always agree.

**Order**: compare X, then Y, then Z, as numbers. With the same three numbers, a version without `-dev` is newer than one with it. The commit never counts. So two builds outside a tag with the same three numbers are equal, and neither is an update of the other.

**Newer**: the channel has an update when the version of its index is newer than the version on the iPod. A version that cannot be compared, on either side, is never newer; the app logs it.

**On the iPod**: the version is the value of the key `TacetOS` of the info file (`detection.md` section 7.3, D60), which a firmware patch adds. The firmware line of the card shows "TacetOS 0.3.0", or "TacetOS 0.3.0 (development build)" for a `-dev` version. The commit is logged, never shown.

## 6. The download

The download is the first step of the update (section 7), `download`:

1. Read the index again (it may have changed since the check of section 8), and find the entry of the model of the iPod. No entry, or a version that is no longer newer: the step fails with `update-gone` ("This update is no longer available. Press Refresh").
2. Check the free space of the download folder: the size of the manifest plus the sizes of its two files, plus `DOWNLOAD_MARGIN` (50 MB). Else `download-space` ("Not enough space on this computer to download the update: 120 MB needed, 40 MB free").
3. Download the manifest, then its two files, into `<app data>/updates/<version>/<target>/`. Each file is written as `<name>.part`, then checked (the size and sha256 given by the index or the manifest), then renamed to its name. A file that fails its check is deleted, and the step fails with `download-damaged` ("The download is damaged. Try again"). A read that fails or times out ends in `download-failed` ("The update could not be downloaded. Check the connection, then try again").
4. A file already there with its right size and sha256 is not downloaded again. A `.part` file left by a cut is deleted and the file starts again from zero.
5. The downloaded manifest becomes the package of the flow, exactly as a manifest chosen by hand (`install-wizard.md` section 7.1): its path and sha256 go in the progress file, and the checks 1 to 5 of that section run in `diagnose` as in an install.

After an update that ends in `finish`, the folder of the older versions in `updates/` is deleted. The folder of the version just installed stays until the next update, so that a resume never downloads twice.

## 7. The update

The flow `update` of the wizard (`install-wizard.md` section 2) writes the new firmware into the firmware folder and keeps the user's files. It changes nothing else: no DFU mode, no Disk Mode, no format, no backup of the music (D61).

**Lists of steps**:

- **Update, older iPod or iPod Classic:** `download`, `diagnose`, `backup-settings`, `firmware`, `verify`, `finish`.
- **Update, iPod Classic, `bootloaderFirst`:** `download`, `diagnose`, `backup-settings`, `classic-bootloader`, `firmware`, `verify`, `finish`.

Rules:

- `firmware` is the step of `install-wizard.md` section 7, unchanged: the space, what is kept (section 7.3), unpack, copy of the kept files, swap, check after, and the resume after a cut (section 7.5). The settings, the themes and the data of the plugins stay.
- `backup-settings` is the step of the uninstall (`install-wizard.md` section 5.3), unchanged: the kept files go to the backup folder before anything is written, so a failed update can always give them back.
- **The bootloader is written only for `bootloaderFirst`** (D61): an iPod Classic whose old firmware folder is still on the volume and whose bootloader may not know the new folder (`firmware-branding.md` section 8.3). `bootloaderFirst` is read the same way as for "keep my music", when the flow starts, then kept in the progress file. Any other change of bootloader goes through Uninstall, then Install.
- The progress file gets the flow `update` and the version it installs. A resume of an update starts again at the step that was cut, as for an install.
- **The first screen** of the update window: the title "Update TacetOS", the line "TacetOS 0.2.0 > 0.3.0", the backup folder line of `install-screens.md` section 2.1, the "What will happen" line (download, checks, backup of the settings, firmware, check), and the main button "Update". There is no path to choose and no package to pick. The other screens are those of the install.
- **The end**: `finish` says "TacetOS 0.3.0 is on the iPod. Eject it: it starts on the new version." and offers Eject, as for an install.

## 8. On the home screen

**When the app reads the channel** (D62): by itself, at most once per launch, the first time a card of an iPod whose firmware line is `tacetos` is shown; and again each time the user presses Refresh. Without such an iPod there is no network access at all. There is no setting to turn the check off. Only the index is read; the packages are read by the update.

**What the card shows** (`home-screen.md` section 4), on `ready` only, when the channel has an update for the model of the iPod (section 5):

- under the firmware line, one more line in the secondary style with the `accent` colour: "Update available: TacetOS 0.3.0";
- one more button, "Update", which becomes the main button. The buttons are then Eject, Show files, Uninstall (secondary) and Update (main). It opens the update window (section 7).

When a progress file exists for the iPod, Resume stays the only main button and the update line is not shown: the flow in progress comes first.

**Silence**: an index that cannot be read (no network, a time out, a missing file, a bad format, a model not in the index, a version that is not newer) shows nothing, no error and no line. Each case writes one log line with its reason. The default address of a build before the beta always ends in "no update", silently.

## 9. What the tests need

All tests run offline: no test reads the real channel.

- **Versions**: the order of section 5 (numbers, `-dev`, the commit ignored), every shape that cannot be compared (a leading zero, a number over 65535, two dots, a short commit, upper case hexadecimal), the line of the card for a release and a `-dev` version.
- **The index**: a good index; each key missing or of the wrong type; `format` 2; a target twice; a file over `INDEX_MAX`; a model missing from the index (no update for it only).
- **The reads**: a fake HTTP server on `127.0.0.1` started by the test (an answer, a 404, a redirect to `http://` refused, six redirects, a server that stops sending, a file larger than its `size`); the local channel as a folder made by the test.
- **The address**: `TACETOS_UPDATE_CHANNEL` with each kind of value, a value refused, the default address kept in a package build.
- **The download**: the right files; a sha256 wrong in the manifest and in a file; a `.part` left by a cut; a file already there; not enough space.
- **The update**: on a `tacet_fakepod` image with TacetOS installed and user files in the firmware folder (settings, a theme, a saved game), an update to a newer package: the new firmware is there, the user files are the same bytes, the backup of the settings exists. With `bootloaderFirst` on a Classic image: the list of steps has `classic-bootloader` before `firmware`. A cut at each step of `firmware`, then a resume.
- **The release index**: `release-index.sh --self-test` (section 3).
- **The release itself** is checked by the CI run of a tag, never by a test.

## 10. Constants and open points

| Name | Value | Where |
|---|---|---|
| Channel address | `https://github.com/vadmyro/TacetOS/releases/latest/download/` | constants file |
| Environment variable | `TACETOS_UPDATE_CHANNEL` | constants file |
| Index file name | `tacetos-index.json` | constants file, `release-index.sh` |
| Key of the info file | `TacetOS` (`kDistributionKey`) | constants file, firmware patch |
| `INDEX_MAX` | 64 KiB | constants file |
| `CHANNEL_TIMEOUT` | 15 s with no byte | constants file |
| `REDIRECT_MAX` | 5 | constants file |
| `DOWNLOAD_MARGIN` | 50 MB | constants file |

The app reads the channel with Qt Network (part of Qt itself, no new library). A system with no TLS support for Qt makes every read fail: no update is shown, and the log says why.

**Open points**:

- **Authenticity**: the sha256 of the index and of the manifests protect against a damaged download, not against a channel that lies, since they come from the same place. Signed packages come with the signature of the beta (P12.3).
- **The app's own update**: not in this spec. It comes with the packages of the beta (P12.2).
