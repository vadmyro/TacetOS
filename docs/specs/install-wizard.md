# The install wizard

The install wizard is the part of the app that takes an iPod from Apple's software to TacetOS, and back. It runs a list of steps in order: the checks, the backup, the format when it is needed, the firmware, the bootloader, the final check. Each step can start again after a cut (the app closed, the iPod unplugged, the computer turned off) without guessing. This spec says which steps run for each iPod and each choice of the user, what each step reads and writes, what is kept on the computer, and what the app says when something fails. It is meant to be enough to write the logic of the wizard without having seen any screen. How the screens look is in `install-screens.md`.

The wizard does not write a sector itself. Every write to a disk goes through the helper (`helper.md` section 2.2), and what each helper command does on the disk is in `bootloader-legacy.md` and `bootloader-classic.md`. The files of the mounted music volume (the firmware files, the backups of settings) are written through the file system, as `helper.md` section 2.2 allows. This spec is about the order, the checks between the commands, and what is kept so that a cut never leaves the user without a way back.

It has four parts:

- **The flow** (sections 1 to 3): the words, the two paths and the steps of each family, the progress file and the resume after a cut.
- **Before any write** (sections 4 and 5): the checks of the computer and of the iPod, and the backups.
- **The writes** (sections 6 to 10): format and conversion, the firmware package, the bootloader of each family, the uninstall.
- **The end** (sections 11 and 12): the final check and the first start, and the report the user can paste on the Discord.

Section 13 lists the errors, section 14 the constants, section 15 what the tests need, and section 16 the points still open.

**Order to write the code.** First the library with its list of steps, its runner and its progress file, with steps that do nothing, tested on every family and every cut (sections 2 and 3). Then the checks (section 4), the backups (section 5), the format (section 6), the firmware (section 7), the two bootloaders (sections 8 and 9), the uninstall (section 10), each with its own tests on images, temporary folders and recorded events. The report (section 12) is independent and can be written at any time. The screens come after, on top of the runner.

## 1. Words used here

- **Wizard**: the whole of this spec: the list of steps, the runner, the progress file.
- **Flow**: one run of the wizard on one iPod, from the first step to the last. A flow is an **install** or an **uninstall**.
- **Family**: how the bootloader is installed, from `method` in `models/models.toml`. `partition` for the older iPods (`bootloader-legacy.md`), `dfu` for the iPod Classic (`bootloader-classic.md`).
- **Path**: the choice of the first screen for an install (D38). **Clean install**: everything on the iPod is copied to the computer, the iPod is formatted, then TacetOS is put on an empty iPod. **Keep my music**: only the settings, the themes, the playlists and the firmware partition are copied, and TacetOS is added next to the music.
- **Step**: one item of the list of a flow (section 2.3). A step has a name, it starts, it ends well or fails, and it may wait for the user.
- **Runner**: the object that runs the steps of a flow one after another, on a worker thread, and writes the progress file.
- **Progress file**: the small file, one per iPod, that says which step a flow has reached (section 3).
- **Backup folder**: the folder where the backups of one iPod are kept, by default `Documents/TacetOS Backups/<serial>/` (section 5.1).
- **Backup**: one dated folder inside the backup folder, with its manifest.
- **Manifest**: the file of a backup that lists every file copied with its size and sha256 (section 5.4).
- **Package**: the three files of a firmware build: the firmware zip, the bootloader file and the manifest of the package (`firmware/README.md`, section "Package").
- **Firmware folder**: the folder of the firmware on the music volume. Its name is in the one file of constants (`src/device/constants.h`), and nowhere else.
- **Firmware folder**: the hidden folder of the firmware of the model (`firmware_folder` in `models.toml`, else the default one, `firmware-branding.md` section 8). Backups and `remove-firmware` also count the default folder, which an older install may have left.
- **Kept files**: the files of the firmware folder that belong to the user and survive a new install (section 7.3).
- **Serial**: the serial number of the iPod as detection gives it (the USB serial string, `detection.md` section 3.1).
- **Data folder**: the folder of the app's data (`helper.md` section 5.4): the journal, the progress files, the state file of paused programs. Not the backup folder.

## 2. The flows and their steps

### 2.1 What starts a flow

The home screen offers the wizard on the card of an iPod (`install-screens.md` section 1):

- **Install TacetOS** on an iPod whose firmware line is `apple`, or `unknown`.
- **Uninstall** on an iPod whose firmware line is `tacetos` or `custom`, on an older iPod whose state read by the helper is `installed`, and always on an iPod Classic (`bootloader-classic.md` section 14.1: the uninstall is harmless when there is nothing to remove).
- **Resume** when a progress file exists for the serial of the iPod (section 3).

A flow needs, before its first step, four things that never change during the flow: the serial, the family, the flow (install or uninstall), and for an install the path and the package (section 7.1). They are chosen on the first screen and written in the progress file.

An iPod with no serial is refused: the wizard does not start (`no-serial`). The serial names the progress file and the backup folder, and a flow that cannot find its iPod again after a cut must not run.

### 2.2 The two paths of an install (D38)

| | Clean install | Keep my music |
|---|---|---|
| Chosen by default | yes | no |
| What is copied to the computer | The whole music volume as it is, hidden folders included (D36), plus the firmware partition on an older iPod | The firmware folder, the playlists, plus the firmware partition on an older iPod (section 5.3) |
| Space needed on the computer | The used space of the music volume, plus the margin (section 5.2) | The size of what is copied, plus the margin |
| The iPod after | Empty, formatted in FAT32, with TacetOS only | The same music, with TacetOS added |
| The music comes back | From the library sync (later versions), or in one click from the backup (section 5.7) | Stays where it is |

Rules:

1. **Clean install is chosen by default.** If the computer has not enough room for it (section 5.2), it is greyed with the reason ("Not enough space on this computer: 92 GB needed, 40 GB free"), and Keep my music is chosen instead.
2. **A Mac iPod can only take the clean install.** Its music volume is HFS+, which the firmware cannot read, and the conversion of `bootloader-legacy.md` section 21 erases the music volume. Keep my music is greyed with the reason ("This iPod is formatted for Mac. TacetOS needs a Windows format, which erases the iPod"). If the computer has not enough room either, the install cannot start, and the message says how much space is needed. An iPod Classic formatted for Mac takes neither path in this version (section 6.4): both are greyed with "This iPod Classic is formatted for Mac, which TacetOS cannot change yet. Restore it with Apple's software on a Windows computer first", and the check `format` blocks.
3. **Nothing is erased until the copy is verified.** The format of section 6 refuses to run without a complete and verified manifest of the whole volume for this serial (section 6.1).

### 2.3 The steps, by family and path

The names below are the names in the code, the progress file and the tests. Each step is described in the section given.

| Step | What it does | Section |
|---|---|---|
| `diagnose` | The checks of the computer and the iPod | 4 |
| `backup-keep` | Copies the firmware folder and the playlists, then the firmware partition of an older iPod | 5.3 |
| `backup-full` | Copies the whole music volume, then the firmware partition of an older iPod | 5.3 |
| `backup-settings` | Copies the kept files before an uninstall, then the firmware partition of an older iPod | 5.3 |
| `disk-mode` | Asks the user to put an older iPod in Disk Mode, and believes it (D27) | 8.1 |
| `format` | Writes an empty FAT32 file system on the music partition | 6 |
| `convert` | Turns a Mac iPod into a Windows iPod | 6 |
| `firmware` | Puts the firmware of the package in the firmware folder, keeping the user's files | 7 |
| `bootloader` | Installs or updates the bootloader of an older iPod | 8 |
| `classic-bootloader` | Installs the bootloader of an iPod Classic over DFU mode | 9 |
| `remove-bootloader` | Removes the bootloader of an older iPod | 10 |
| `classic-remove` | Removes the bootloader of an iPod Classic over DFU mode | 10 |
| `remove-firmware` | Deletes the firmware folder from the music volume | 10 |
| `verify` | Reads everything again and checks the result | 11 |
| `finish` | Says how to start each firmware, and asks to eject | 11 |

The list of a flow is a pure function of five inputs: the family, the flow, the path, whether the music volume is in Mac format (read in `diagnose` the first time, then kept in the progress file), and `bootloaderFirst`, which only an iPod Classic with "keep my music" reads (`firmware-branding.md` section 8.3: the old firmware folder is on the volume and the bootloader may not know the new one; read when the flow starts, then kept in the progress file). The lists:

**Install, older iPod, clean install:** `diagnose`, `backup-full`, `disk-mode`, `format` (or `convert` for a Mac iPod), `firmware`, `bootloader`, `verify`, `finish`.

**Install, older iPod, keep my music:** `diagnose`, `backup-keep`, `disk-mode`, `firmware`, `bootloader`, `verify`, `finish`.

**Install, iPod Classic, clean install:** `diagnose`, `backup-full`, `format` (or `convert`), `firmware`, `classic-bootloader`, `verify`, `finish`.

**Install, iPod Classic, keep my music:** `diagnose`, `backup-keep`, `firmware`, `classic-bootloader`, `verify`, `finish`.

**Install, iPod Classic, keep my music, `bootloaderFirst`:** `diagnose`, `backup-keep`, `classic-bootloader`, `firmware`, `verify`, `finish`.

**Uninstall, older iPod:** `diagnose`, `backup-settings`, `disk-mode`, `remove-bootloader`, `remove-firmware`, `verify`, `finish`.

**Uninstall, iPod Classic:** `diagnose`, `backup-settings`, `classic-remove`, `remove-firmware`, `verify`, `finish`.

Notes:

- **Disk Mode is asked once, before the first write of sectors** of an older iPod (the format, the conversion, or the bootloader). The Classic never needs it: the helper ejects it before DFU mode (`helper.md` section 6.9).
- **The firmware comes before the bootloader** on both families, so that the first start after the bootloader finds the firmware (`bootloader-classic.md` section 3, condition 4). On an older iPod, a bootloader with no firmware would show an error screen. The one exception is `bootloaderFirst`: the firmware is already in the old folder, and the new bootloader finds it there.
- **The firmware folder is removed after the bootloader** in an uninstall, so that the iPod never starts a bootloader that finds no firmware.
- **There are no "first settings" in v0.2** (D40): the install ends on `verify` and `finish`.
- **In v0.2, only the iPod Classic has a package** (D39). The flows of the older iPods are complete in the code and the tests, with packages made by the tests; a real install on an older iPod waits for the packages of every model.

### 2.4 The runner

1. The runner runs one step at a time, in the order of the list, on a worker thread: never on the thread of the screen, because the disk service and the helper can wait without any limit (a password window, `device-state.md` section 2.4).
2. It tells the screen, by signals: a step started, progress inside a step (bytes or seconds, done and total, and a short stage name), a step ended well, a step failed (an error code of section 13 and a detail for the journal), and a step waits for the user. The user's answers come back by calls: confirm, the typed name, ready, cancel.
3. **Waiting for the user** is a state of a step, not a step of its own. The waits are: Disk Mode (section 8.1), the typed name (section 6.2), ready for DFU mode (section 9.2), the battery question (section 4.3), and an offer after a failure (try again, write the backup back). The progress file says the step waits (section 3.2).
4. **One flow per iPod.** A second flow for the same serial is refused at once (`flow-running`), in the same app (one runner per serial) and between two copies of the app (the lock of section 3.5).
5. **Cancel.** The user can cancel while a step waits, and during a step until its first write (`helper.md` section 10.3). A cancel during a helper command sends `cancel` to the helper and waits for its answer. After the first write, the cancel button is off. A cancelled flow keeps its progress file: Resume starts again at the cancelled step.
6. **One step's failure stops the flow.** The runner never goes to the next step after a failure. What it offers (try again, write the backup back, nothing) is in the section of the step.
7. **The journal.** Every start, end, failure and wait of a step is one entry in the journal (`device-state.md` section 3), domain `op`, with the step name, the flow, and for a failure the code. The serial stays in the local journal (`device-state.md` section 3.2); the report masks it (section 12).
8. **The iPod is held** (`device-state.md` section 2.6) for the whole flow: a restart that a step expects (Disk Mode, DFU mode, the restart after a bootloader) shows `rebooting`, not `unplugged`.

## 3. The progress file and the resume

### 3.1 Where it is

One file per serial, in the data folder: `wizard/<serial>.json` (the serial made safe for a file name, section 5.1). JSON, UTF-8. It is written by replacing the whole file in one move (a temporary file in the same folder, flushed, then renamed over the old one), so that a cut leaves either the old file or the new one, never half of one.

It is written **before each step starts** (the step is `running`), **when a step waits** for the user, and **after each step ends** (`done`, and the index moves on, or `failed`). It is deleted when the flow ends well, after `finish`.

### 3.2 What it holds

| Key | Type | Meaning |
|---|---|---|
| `format` | number | `1`. Any other value is refused (section 3.4) |
| `appVersion` | text | The version of the app that wrote it, for the journal |
| `serial` | text | The serial, as detection gave it (not made safe) |
| `model` | text | The model id of `models/models.toml` |
| `family` | text | `partition` or `dfu` |
| `flow` | text | `install` or `uninstall` |
| `path` | text | `clean` or `keep` for an install, absent for an uninstall |
| `macFormat` | boolean | Whether the music volume was in Mac format at the first `diagnose` |
| `bootloaderFirst` | boolean | Section 2.3: the bootloader of a Classic goes in before the old firmware folder moves. Absent in a file written before the key existed: false |
| `steps` | list of text | The list of section 2.3, as computed when the flow started |
| `current` | number | The index of the step in `steps` that runs, waits, failed or is next |
| `status` | text | `running`, `waiting`, `failed` or `next` (the step at `current` has not started) |
| `error` | text | The code of the failure when `status` is `failed`, else absent |
| `backup` | text | The absolute path of the dated backup folder of this flow, once `backup-*` has started |
| `package` | object | For an install: the absolute path of the package manifest and its sha256 (section 7.1) |
| `facts` | object | What a step found and a later step needs, by step name (below) |
| `started`, `updated` | text | UTC, ISO 8601 |

The `facts` are few, and each one is named here: `diagnose` keeps the state read by the helper (`clean`, `installed`, `inconsistent`) for an older iPod; `backup-*` keep the path of the manifest; `bootloader` keeps the path of the install record of the helper, and after a failed write the `next` of the helper (`offer-retry`, `restored`, `restore-failed`) or `restored` after the user's yes to the backup; `classic-bootloader` and `classic-remove` keep the `stateBefore` they sent; `finish` keeps what the last screen shows (section 11.2). Nothing else goes in the file: no file name of the music, no text for the user.

### 3.3 The resume

When the app starts, and each time an iPod is detected, the app looks for a progress file of its serial. If there is one, the card offers **Resume** (and the screen says which step and since when). The user can also **start again**, which deletes the progress file after a confirmation; the backups stay.

A resume always does the same things, in this order:

1. Reads the progress file (section 3.4). If it is refused, nothing is resumed.
2. Takes the lock (section 3.5).
3. Runs `diagnose` again, whatever step the flow had reached, because the computer or the iPod may have changed. A blocking check that fails stops the resume there. The facts of the first `diagnose` that cannot change (`macFormat`) are taken from the file; a different family or model is `progress-mismatch`.
4. Goes to the step at `current` and applies its rule of resume, below.

The rule of each step, for a step that was `running` or `waiting` when the cut came, or `failed`:

| Step | After a cut |
|---|---|
| `diagnose` | Run again |
| `backup-keep`, `backup-settings` | Run again in the same dated folder. Files already in the partial list (section 5.4) are not copied again |
| `backup-full` | The same: copies only the files that are not yet verified (section 5.5) |
| `disk-mode` | Asked again |
| `format` | Wait for the volume (section 6.3 step 3). A FAT32 volume that holds nothing but what the system makes there means the format was done: the step is done. Anything else: the format runs again from the start, which is safe because the backup is complete and the format does not touch the firmware partition (`bootloader-legacy.md` section 20) |
| `convert` | Read the state. A Windows iPod of the same model means the conversion reached its last write (`bootloader-legacy.md` section 21, step 6): the step is done. A Mac iPod: run again from the start, which the order of the writes allows |
| `firmware` | Look at the folders of section 7.5 and finish or undo what was half done, then run again |
| `bootloader` | Read the state. `installed`: the step is done (the record may be missing, which the journal says). `clean`: run again. `inconsistent`: stop and offer to write the backup back (`bootloader-legacy.md` section 19) |
| `classic-bootloader` | Look at what runs on the iPod (`detection.md` section 7.6). `other`: the step is done. `apple`: the step runs again from the wait for the user, and the screen says that the last try did not finish |
| `remove-bootloader` | Read the state. `clean`: done. `installed`: run again. `inconsistent`: as for `bootloader` |
| `classic-remove` | Look at what runs. `apple` with no firmware folder, or `apple` after a cut during the wait for the restart: run again (the uninstall is harmless twice, `bootloader-classic.md` section 14.3 step 3). `other`: run again |
| `remove-firmware` | Run again: deleting a folder that is partly gone is the same as deleting it |
| `verify`, `finish` | Run again |

**The iPod unplugged during a step.** A step that does not expect a restart fails with `ipod-gone` (a helper command gives `disk-gone`, a file copy gives a read or write error). The progress file says the step failed. When an iPod with the same serial is plugged in again, the card offers Resume.

**The computer turned off or the app closed.** The same as a crash: the progress file says the step was `running`. A helper command that had made its first write ends by itself (`helper.md` section 10.4); the state read again at the resume tells what happened.

**The app never resumes by itself.** A resume always waits for the user to press Resume.

### 3.4 A file that cannot be trusted

A progress file is refused, and nothing is resumed, when:

- it is not valid JSON, or a key of section 3.2 is missing or has the wrong type;
- `format` is not `1`;
- `serial` is not the serial of the file name, or not the serial of the iPod;
- `steps` is not the list that section 2.3 gives for its `family`, `flow`, `path` and `macFormat` (an older version of the app with another list);
- `current` is outside the list, or `status` is not one of the four values;
- `backup` names a folder that does not exist, for a step after `backup-*`.

A refused file is renamed `<serial>.json.rejected` (an old one of that name is replaced), the journal says why (`progress-refused`, the reason), and the card offers a new install or uninstall. The backups are not touched: they keep their own manifests.

### 3.5 Two copies of the app

The runner takes a lock before its first step: a file `wizard/<serial>.lock` in the data folder, created only if it does not exist, holding the process id of the app. A lock whose process does not run any more is taken over (as the lock of the helper, `helper.md` section 8). A lock held by a running process gives `flow-running` ("Another TacetOS window is working on this iPod"). The lock is deleted when the flow ends, fails or is cancelled, and when the app quits.

## 4. The diagnosis

### 4.1 Rules

The diagnosis is a list of checks, run in a fixed order: first the computer, then the iPod (D14, D34). Each check has an id, a side (`computer` or `ipod`), whether it is **blocking**, a result (`pass`, `warn`, `fail`, `ask` when it waits for the user's answer, `skipped`), a message in clear words, and for what is missing on the computer the command to type.

1. Every check of the computer runs.
2. The checks of the iPod run only when `helper` has passed, because most of them need the helper. Otherwise they are `skipped`, with "Checked once the computer is ready".
3. A check that needs the result of another one that failed is `skipped` (for example `bootloader-state` when `model` failed).
4. The flow cannot go past `diagnose` while a blocking check has `fail` or `ask`. A `warn` is shown and does not stop.
5. "Check again" runs the whole list again. Nothing of a previous run is kept.
6. Each source of a check is behind an interface (the helper, the disk service, the file of the system release, the free space of a folder, the clock), so that the tests give their own answers and nothing real is called.

### 4.2 The computer

| Id | Blocking | Passes when | Message when it does not | Command |
|---|---|---|---|---|
| `system` | yes | The system is Linux. On Windows and macOS, `fail` until their ports (`helper.md` section 12) | "Installing on this system comes in a later version of TacetOS" | none |
| `helper` | yes | The helper program is found (`helper.md` section 2.3) and its hello came with the protocol of this app. The app starts the helper when the wizard window opens, and the check waits for the hello (`helper.md` section 3.2) instead of reading it before it came | "The helper program is missing. Reinstall TacetOS", or "The helper does not match this version of TacetOS. Reinstall TacetOS" | none |
| `writes` | no | The hello says `realWrites` true. Else `warn` | "This development build does not write to iPods. Every step runs up to the first write, then stops" (`helper.md` section 9) | none |
| `udisks` | yes (Linux) | The disk service answers on the system bus, and its version is at least the oldest of `helper.md` section 14 | "The disk service udisks2 is missing", or "udisks2 is too old (2.6.0, 2.7.3 or newer needed)", or "udisks2 is not running" | By distribution (section 4.4) |
| `classic-build` | yes (Classic) | The hello says `usb` and `classic` true (`helper.md` section 3.2) | "Classic install is not available in this build" | none |
| `udev-rule` | yes (Classic, Linux) | The file of the udev rule of `helper.md` section 14 holds the rule of this helper, read as `helper.md` section 11.5 step 1 says (a rule put by an older build, without the line that names the polkit rule, counts as missing). Else `fail`, with the action "Install the rule" (`install-udev-rule`, `helper.md` section 6.11), which puts the polkit rule in place too | "TacetOS needs a USB permission to talk to the iPod in DFU mode" | The action, and the commands of `helper.md` section 11.5 for those who want to type them |
| `disk-access` | no (older iPod, Linux) | The same file as `udev-rule`, read the same way: it is the sign that the polkit rule of `helper.md` section 11.5 is in place. Else `warn`, with the action "Allow TacetOS" (`install-udev-rule`), which asks for the password once and puts both rules in place. On a system with no folder of polkit rules, the udev rule without its last line passes too: nothing more can be done | "Without this permission, the system asks for your password each time TacetOS opens the iPod" | The action, and the commands of `helper.md` section 11.5 |
| `backup-folder` | yes | The backup folder (section 5.1) is an absolute path, can be created and written, and is not on an iPod | "Cannot write to the backup folder" and the folder, or "The backup folder is on an iPod. Choose a folder on this computer" | none |
| `backup-space` | yes | The free space of the backup folder is enough for the path chosen (section 5.2). For a clean install that does not fit, the first screen has already moved to Keep my music (section 2.2) | "Not enough space for the backup: 92 GB needed, 40 GB free" | none |

The version of the system and of the disk service are written in the journal, whatever the results.

### 4.3 The iPod

| Id | Blocking | Passes when | Message when it does not |
|---|---|---|---|
| `model` | yes | Detection gave one model, supported by TacetOS, and `read-state` agrees on the method (`partition` or `dfu`) | "An iPod TacetOS does not support", or "The iPod could not be read" and the code of the helper |
| `serial` | yes | The serial is known (section 2.1) | "The serial number of this iPod could not be read" |
| `single-ipod` | yes (Classic) | No other iPod is plugged in (`bootloader-classic.md` section 16.4) | "Unplug the other iPod first" |
| `volume` | yes | The music volume is mounted, readable and writable (displayed state `ready`; a volume mounted read only is the detection state `read-only`, which fails). When the disk is there and its volume is not mounted (state `no-volume`, after an eject for example), the check offers the action "Mount the volume" (`mount-volume`): the app mounts it through the disk service, waits up to 10 s for detection to see it `ready`, then the checks run again | The message of detection for its state (`detection.md` section 10). For `read-only`, "The iPod is open for reading only" followed by "Unplug the iPod, plug it in again, and if it stays so, check the volume on this computer". The app never repairs the volume itself, and offers no action |
| `format` | yes | FAT32. HFS+ is a `warn` that keeps only the clean install (section 2.2 rule 2). Anything else: `fail` | "This iPod is formatted for Mac", or "This file system cannot be used" |
| `apple-firmware` | yes | Older iPod: `read-state` finds the firmware partition with its OSOS entry (`bootloader-legacy.md` section 11 checks 2 and 3). Classic: the device sheet gives a version (`helper.md` section 6.3, key `VisibleBuildID`) and it is in the table of `bootloader-classic.md` section 3 for the revision | "Apple's firmware is not on this iPod. Restore it with Apple's software first", or "Update the iPod with Apple's software first: version 2.0.1 needed, 1.0.2 found", or "The version of Apple's firmware could not be read" |
| `bootloader-state` | yes (older iPod) | `read-state` gives `clean` or `installed`. `inconsistent` fails with the message of `bootloader-legacy.md` section 10 | "The firmware partition of this iPod is not as expected. Nothing will be written" |
| `package` | yes (install) | The package passes the checks of section 7.1 | The message of the check that failed |
| `firmware-room` | yes (install) | The free space of the music volume is enough for the firmware (section 7.2). For a clean install, the size of the volume counts, since it will be empty | "Not enough space on the iPod for TacetOS: 40 MB needed, 12 MB free" |
| `battery` | yes | The user answered yes to "Is the iPod charged?". `ask` until then. The app does not read the battery (`bootloader-classic.md` section 3, condition 5) | "Charge the iPod for an hour first" |

On an uninstall, `package`, `firmware-room` and `apple-firmware` of a Classic do not run (`bootloader-classic.md` section 14.1), and `format` only checks that the volume can be read, so that the kept files can be saved.

The checks of `bootloader-legacy.md` section 11 and `bootloader-classic.md` section 3 run **again** inside the helper or the step that writes, just before the write. The diagnosis is there to tell the user early, not to replace them.

### 4.4 The commands to type

The command is chosen from the file of the system release (`/etc/os-release`, then `/usr/lib/os-release`), from `ID`, then from each word of `ID_LIKE`, in order:

| Found | udisks2 missing or too old | udisks2 not running |
|---|---|---|
| `fedora`, `rhel`, `centos` | `sudo dnf install udisks2` | `sudo systemctl enable --now udisks2` |
| `debian`, `ubuntu` | `sudo apt install udisks2` | the same `systemctl` line |
| `arch` | `sudo pacman -S udisks2` | the same `systemctl` line |
| anything else, or no file | no command: "Install udisks2 with the package manager of your system" | the same `systemctl` line |

The command is copied as it is; the app never runs it.

## 5. The backups

### 5.1 Where they go

- **By default**: `Documents/TacetOS Backups/<serial>/<date>/` (D40), where `Documents` is the documents folder of the system.
- **Another folder**: the user can choose any folder of the computer. The backups then go to `<folder>/<serial>/<date>/`, with the same layout. The choice is kept in the settings of the app, for every iPod.
- `<serial>` is the serial with every character that is not a letter, a digit, `-` or `_` replaced by `_`.
- `<date>` is the local time when the backup started, as `2026-11-15_183005`: no `:`, which Windows refuses in a name.
- The folder is never on an iPod: a backup kept on the iPod it saves is no backup (`helper.md` section 5.4). The app compares the device of the folder with the disks of every detected iPod.

The backup folder of the flow is also the `dataDir` given to the helper for `backup-firmware`, the three bootloader commands and `convert-to-fat32`: their backup of the firmware partition and their install record go next to the files. `classic-dfu` and `resume-paused` get the data folder of the app instead, because the state file of paused programs must be found at the next start.

### 5.2 The space

Before a backup starts, and in `backup-space` (section 4.2), the app compares the free space of the folder with:

- **Clean install**: the used space of the music volume (its size minus its free space, from the system: quick, and never less than the files), plus the size of the firmware partition for an older iPod, plus `BACKUP_MARGIN`.
- **Keep my music, uninstall**: the size of the files to copy (they are few: the app adds their sizes), plus the firmware partition, plus `BACKUP_MARGIN`.

If the space runs out during the copy anyway, the step fails with `backup-full-disk`, the half-copied file is deleted, and nothing is written to the iPod.

### 5.3 What is copied

| Step | From the music volume | The firmware partition (older iPod) |
|---|---|---|
| `backup-full` | Every file and folder of the volume, hidden ones included, with their dates of change (D36) | `backup-firmware` |
| `backup-keep` | The firmware folder, whole, when it exists; the `Playlists` folder at the root; the playlist files (`.m3u`, `.m3u8`) at the root | `backup-firmware` |
| `backup-settings` | The kept files of section 7.3 | `backup-firmware` |

Rules:

1. The files of the music volume are copied through the file system, never by the helper (`helper.md` section 2.2). The firmware partition is copied by the helper only.
2. Symbolic links are not followed and not copied (FAT32 has none; HFS+ may), and the journal counts them.
3. A file that cannot be read fails the step (`backup-read-failed`) with the count of files not read; the name of the file goes to the screen, never to the journal (`device-state.md` section 3.2).
4. The iPod Classic has no firmware partition: its backups have no `backup-firmware` (`helper.md` error `wrong-method`).

### 5.4 The manifest

Each backup has a `manifest.json` at its root, written last, in one move (as in section 3.1), only once every file has been copied and checked. The files of the music volume are kept under a `volume/` folder of the backup, with their paths, so that no name of the iPod meets the manifest or the copy of the firmware partition that the helper writes at the root.

| Key | Type | Meaning |
|---|---|---|
| `format` | number | `1` |
| `kind` | text | `full`, `keep` or `settings`, from the step |
| `serial`, `model` | text | The iPod |
| `appVersion` | text | The version of the app |
| `created` | text | UTC, ISO 8601 |
| `volumeLabel` | text | The label of the music volume, for the screen of the restore |
| `files` | list | One object per file: `path` (relative to the volume, `/` between folders), `size`, `modified` (UTC, ISO 8601), `sha256` |
| `folders` | list of text | The folders, empty ones included, so that a restore gives the same tree |
| `firmwarePartition` | object | For an older iPod: `file` (the name the helper gave), `size`, `sha256`. Absent for a Classic |
| `complete` | boolean | Always true in a written manifest |

During the copy, the app appends one line per verified file to `manifest.partial` (JSON lines, the same objects as `files`), flushed after each line. It is what a resume reads (section 3.3). It is deleted once the manifest is written.

**A backup is verified** when its manifest can be read, `complete` is true, its `serial` is the serial of the iPod, and every file it names is in the folder with its size. The sha256 are checked during the copy (section 5.5) and again by a restore (section 5.7); they are not read again each time a step asks whether a backup is verified, which on 100 GB would take an hour.

### 5.5 The copy

For each file, in the order of a walk of the volume (folders first, names sorted, so that the order is the same after a cut):

1. Note its size and date of change.
2. Copy it to a temporary name in the backup (`<name>.part`) by pieces of `COPY_PIECE`, computing its sha256 while reading.
3. Flush the copy, then read it back from the computer's disk and compute its sha256 again. A difference fails the step (`backup-verify-failed`).
4. Read the size and date of change of the source again. If they changed, copy the file once more from step 1. If it changes again, the step fails (`source-changed`): something else is writing to the iPod.
5. Give the file its date of change, rename it to its name, append its line to `manifest.partial`.

At the end, the firmware partition is copied by the helper, then read by the app and its sha256 added to the manifest. Then the manifest is written.

- **Progress**: bytes done and total, and the time left, computed from the speed of the last 10 seconds. A clean install of a full 120 GB iPod takes about an hour over USB 2 (plan P5.1).
- **Cancel**: between two pieces. The `.part` file is deleted; `manifest.partial` stays, so that a later run goes on from there.
- **Resume**: the files of `manifest.partial` whose copy in the backup still has its size are not copied again. A `.part` file is deleted and copied again.
- **Names**: the names of the volume are kept as they are, accents included. A name that the computer's file system refuses (on Windows, a `:` or a name ending in a dot, which HFS+ allows) fails the step with `backup-name-refused` and the count; a later version may rename them.

### 5.6 After the backup

- `format` and `convert` refuse to run unless the backup of this flow is a verified `full` backup of this serial (section 6.1).
- `bootloader`, `remove-bootloader` and the conversion refuse to run unless the backup of this flow holds a firmware partition of this serial, with the sha256 of its manifest checked again just before (it is small).
- A failure of any backup step stops the flow before any write to the iPod: the runner never goes on to a step that writes (section 2.4 rule 6).

### 5.7 Restore in one click

The screen lists the backups of the folder of section 5.1, by serial and date, from their manifests. A backup without a manifest, or with a manifest that cannot be read, is listed as "Incomplete" and cannot be restored. Restore needs the iPod of the same serial, mounted.

- **`keep` and `settings` backups** (small): the app checks the sha256 of every file of the backup first. A difference refuses the whole restore (`backup-damaged`), with nothing written. Then it copies each file back to its place on the volume (to a temporary name, then renamed over the file there), and reads it back.
- **`full` backups** ("Put my music back"): every file and folder of the manifest is copied back the same way, by pieces, the sha256 computed while reading the backup and compared with the manifest **before** the file is renamed to its name on the iPod: a damaged file is never placed, and the restore stops there (`backup-damaged`), saying how many files were put back. A file already on the iPod with the same size and sha256 is skipped. Progress, cancel and the time left are as in section 5.5. The firmware folder of the backup is not copied back over a TacetOS install: it is the old one.
- **A write that fails on the iPod** (it is full, or its file system refuses a name) stops the restore with `restore-write-failed`, saying how many files were put back. The temporary file is deleted, and a new restore skips the files already there.
- **The firmware partition** is written back only by `restore-backup` (`helper.md` section 6.6), and only when `bootloader-legacy.md` section 19 offers it or the user asks for it on the restore screen, with the confirmation of an install (`bootloader-legacy.md` section 22).

## 6. Format and conversion

### 6.1 When they run

`format` runs in a clean install of a FAT32 iPod, `convert` in a clean install of a Mac iPod. Both refuse to start, with `backup-not-verified`, unless the backup of the flow is a verified `full` backup of the same serial (section 5.4). On an older iPod, the backup must also hold the firmware partition.

### 6.2 The confirmation

The step waits for the user with the dialog of `bootloader-legacy.md` section 22: "All music on this iPod will be erased", the number of files and the space used (from the manifest of the backup, which has just counted them), and the field where the user types the name of the iPod.

- **The name of the iPod** is the label of the music volume as the system gives it. When there is none, it is the name of the model of `models/models.toml` ("iPod Classic").
- The runner sends the typed text to the helper as `typedName`, with the name as `ipodName` (`helper.md` section 6.7). The helper checks it again; a mismatch is never shown, because the button of the dialog stays off until the text matches with the rule of `bootloader-legacy.md` section 22.
- **The label of the new file system** is the old label when it is a valid FAT32 label (at most 11 characters, uppercase letters, digits, spaces and `_`, after it is turned to uppercase), else `IPOD`.

### 6.3 The run

1. `read-state` for a new scan token.
2. `format-music` or `convert-to-fat32` with the token, the names, the label, and for the conversion the backup folder as `dataDir`. Each `progress` of the helper goes to the screen (stages `unmount`, `open`, `check`, `backup`, `write`, `verify`).
3. On success, the app asks the disk service to mount the music volume (`helper.md` section 6.7), then waits for detection to show the iPod `ready` again, for at most `VOLUME_RETURN`. The volume must be FAT32 and empty except what the system makes there: `System Volume Information` (Windows), `.fseventsd`, `.Spotlight-V100`, `.Trashes` and `.TemporaryItems` (macOS), and the `.Trash-<user id>` folders of the Linux desktops. A volume that holds anything else fails the step as one that did not come back.
4. A volume that does not come back fails the step with `volume-not-back`: "The iPod was formatted, but its volume did not come back. Unplug the iPod, plug it in again, then press Resume". The resume finds an empty FAT32 volume and counts the step as done (section 3.3).
5. A helper that stops (`helper-crashed`, `helper.md` section 3.7) fails the step: the state is read again and the step can be run again (section 3.3).

### 6.4 The iPod Classic

The helper formats the music partition of the older iPods only (`bootloader-legacy.md` section 20.1: 512-byte sectors, music in slot 2). The iPod Classic has 4096-byte sectors and its music in slot 1. In this version, its `format` **empties the music volume through the file system** instead:

1. The checks of section 6.1 and the confirmation of section 6.2 are the same. No helper is asked, so the app checks the typed name itself, with the rule of `bootloader-legacy.md` section 22 (`confirmation-mismatch`).
2. Every entry at the root of the volume is deleted, folders with everything in them, except what the systems make there (section 6.3). The first deletion is the first write: the step can be cancelled until then.
3. The step is done when the volume is FAT32 and empty but for what the systems make there. An entry that cannot be deleted fails the step with `erase-failed`; a volume that went away, with `ipod-gone`.

The label of the volume does not change. The resume is the one of `format` in section 3.3: an empty FAT32 volume means the step was done, anything else runs it again from the confirmation. A Classic formatted for Mac cannot be converted in this version (section 2.2 rule 2).

## 7. The firmware

### 7.1 The package

In v0.2 the user gives the package with a file dialog on the first screen (D39): they pick the manifest, and the two other files must be next to it. The checks, in this order, before anything is written to the iPod:

1. The manifest is a JSON object, at most 64 KiB, and `format` is `1` (else `package-format`: "This package is for another version of TacetOS").
2. `target` is the `bootloader_target` of the model of the iPod in `models/models.toml` (else `package-wrong-model`: "This package is for another iPod model").
3. `files` has exactly two entries, one of kind `firmware` and one of kind `bootloader`, with names that have no folder part, and both files are next to the manifest (else `package-incomplete`: "This package is incomplete"). A manifest that cannot be read at all is incomplete too.
4. The size and sha256 of each file are those of the manifest (else `package-damaged`: "This package is damaged. Download it again").
5. The zip of the firmware can be read by the reader of section 7.6, and it has at least one entry, all inside one top folder (else `package-damaged`). That folder is the firmware folder of the model (else `package-other-folder`: "This package is for an older version of TacetOS. Choose the package of this version"; an old package keeps the firmware in another folder).

These checks run in `diagnose` (check `package`), and again at the start of `firmware`, of `bootloader` and of `classic-bootloader`, because the files may have changed on the computer. The progress file keeps the path of the manifest and its sha256: a manifest that changed between two steps of the same flow is `package-changed` ("This package changed on this computer since the install started. Start again"), and the flow must start again.

### 7.2 The space

The step needs, free on the music volume: the unpacked size of the zip (the sum of the sizes of its entries), plus the size of the kept files (section 7.3), plus `VOLUME_MARGIN`. The old firmware folder is not counted as free: it is deleted only after the new one is in place.

### 7.3 What is kept

A new install replaces the files of the firmware, and keeps what belongs to the user. The kept files come from the old firmware folder on the iPod, or, when there is none and an uninstall of this serial left a `settings` backup (D40), from the newest such backup, if the user accepts the offer "Put back your settings and themes from <date>".

| Kept | What it is |
|---|---|
| `config.cfg` | The settings |
| `fixed.cfg` | Settings that the user fixed |
| `.resume.cfg` | Where playback stopped |
| `.playlist_control` | The current playlist |
| `most-recent.bmark` | The bookmarks |
| `battery_levels.cfg` | The battery curve the user measured |
| The folders `themes`, `wps`, `backdrops`, `icons`, `fonts`, `eqs`, `recpresets`, `rocks.data` | Themes and their parts, equalizer and recording presets, the data of the plugins (scores, saved games). Only the files that the package does not have: a file of the package with the same path wins |

Not kept: the database files of the firmware (every file whose name starts with `database_`), which the firmware builds again and whose format may change between versions; the caches (`dircache.dat`, `.glyphcache`); everything else, which the package brings.

These names are names the firmware looks for. In the code they are in the one file of constants (`src/device/constants.h`), next to the name of the firmware folder, and nowhere else. A name of this list is compared without case, as FAT32 does.

### 7.4 The steps

1. Wait for the music volume, as long as the step before waited for the iPod: with `bootloaderFirst`, the Classic comes back from `classic-bootloader` before its volume is mounted. The step never writes without a mount point (an empty path would be the folder the app runs from). It fails with `volume-not-back` only when the wait is over. Then check the package (section 7.1).
2. Clean up what a cut left (section 7.5). Then, for a model with its own firmware folder, move the default folder into it and rewrite the paths of its settings and screens (`firmware-branding.md` section 8.3, steps 2 and 3). Then check the space (section 7.2), only when the step unpacks: a run that goes straight to the check after writes nothing, and the folders a cut left would count against it.
3. **Unpack** the zip into a folder next to the firmware folder, on the music volume: the name of the firmware folder followed by `NEW_SUFFIX`. The top folder of the zip is not kept in the path: its content goes straight into the new folder. Each file is written, flushed, closed, then read back and its CRC-32 compared with the zip.
4. **Copy the kept files** from the old folder (or the backup) into the new folder, each one read back and compared with its sha256.
5. **Swap**: rename the old firmware folder to its name followed by `OLD_SUFFIX` (when there is one), then rename the new folder to the name of the firmware folder. Then delete the old folder.
6. **Check after**: every entry of the zip is at its path with its size and CRC-32, read again from the volume; every kept file has its sha256; the info file of the firmware (`detection.md` section 7.3) is valid and its target is the model of the iPod.
7. Ask the system to write everything to the iPod (flush the folder and the files) before the step is done.

The step can be cancelled until the swap of step 5; then the new folder is deleted and the old one is as it was. From the first rename on, the step goes to its end.

### 7.5 After a cut

The three folders tell where the step stopped. At the start of the step (section 7.4 step 2), and at a resume:

| Firmware folder | New folder | Old folder | What happened | What the step does |
|---|---|---|---|---|
| any | yes | no | Cut before the swap | Deletes the new folder, then runs from the start |
| no | yes | yes | Cut between the two renames | Renames the new folder to the firmware folder, deletes the old one, then goes to the check after |
| yes | no | yes | Cut after the swap | Deletes the old folder, then goes to the check after |
| no | no | yes | The rename of the new folder failed | Renames the old folder back, then runs from the start |
| any | no | no | Nothing half done | Runs from the start, or goes to the check after if the progress file says the swap was done |

The app only ever deletes folders with its own two suffixes, and the firmware folder in `remove-firmware`. It never deletes anything else on the volume.

### 7.6 Reading a zip

Qt has no public way to read a zip. TacetOS has **its own reader** in a small library of its own (`src/archive/`, Qt Core only), which the firmware step uses now and the albums given as zips (D35) will use later:

- The format is the public zip format: the end record, the central directory, the local headers. Zip64 records are read, for archives over 4 GiB (albums).
- Two methods: stored (0) and deflate (8). The deflate decoder is our own, written from the public description of the format (RFC 1951). **No new library**: no zlib to find on Windows, the same code on every system.
- The CRC-32 of each entry is checked while it is unpacked.
- Refused, with the whole archive: an encrypted entry, another method, a name with `..`, a name that starts with `/` or a drive letter, a `\` in a name, two names that differ only by case, an entry marked as a link, more than `ZIP_ENTRY_MAX` entries, a total unpacked size over `ZIP_UNPACK_MAX`, an entry that unpacks to more than its declared size, a central directory that does not match the local headers.
- The tests make their zips themselves (stored entries, and deflate data made by `qCompress` with its header and its sum taken off), never with a real build.

## 8. The bootloader of the older iPods

### 8.1 Disk Mode

The step `disk-mode` waits for the user: the screen shows how to enter Disk Mode on this model and a button "The iPod is in Disk Mode". The app cannot tell Disk Mode from Apple's firmware (D27), so it believes the user. After the button:

- whether the iPod went away and came back on the same port (`detection.md` section 9.4) during the wait or did not go away, the app waits for detection to show it `ready`, for at most `RESTART_WINDOW` plus `VOLUME_RETURN` (the restart, then its volume), then the step is done;
- if it is not `ready` by then, the step fails with `ipod-not-back`.

### 8.2 The run

1. Check the package again (section 7.1) and that the backup of the flow holds a firmware partition of this serial (section 5.6). No backup: `backup-not-verified`, nothing is sent.
2. `read-state`, for the state and a new token. `inconsistent` stops the step (`check-failed` with the message of `bootloader-legacy.md` section 10).
3. `install-bootloader` for `clean`, `update-bootloader` for `installed`, with the bootloader file of the package, the version of the package, and the backup folder as `dataDir` (`helper.md` section 6.5). Each `progress` goes to the screen.
4. `read-state` again. It must say `installed`. Else the step fails with `verify-failed`.
5. The path of the install record (or the reason it was not written) goes in the progress file and the journal.

A failure after the first write follows the table of `bootloader-legacy.md` section 19, from the `data` of the result: the message, and the offers it lists (try again, write the backup back). The runner never writes the backup back without the user's yes, except where the helper did it itself (the move of images).

## 9. The bootloader of the iPod Classic

### 9.1 Before DFU mode

The conditions of `bootloader-classic.md` section 3 are checked again, at the start of the step: FAT32 (`format`), the version of Apple's firmware (`apple-firmware`), the battery (`battery`, answered in this flow), one iPod only (`single-ipod`), the build (`classic-build`), the udev rule (`udev-rule`). Condition 4, the firmware on the music volume, is the step `firmware` of this flow, done: the progress file says so, and the check after of section 7.4 step 6 runs again. With `bootloaderFirst` (section 2.3) the step comes before `firmware`: the info file of the old firmware folder, for this model, is enough. Any failure stops the step before the iPod is ejected.

### 9.2 The run

1. **Ready.** The step waits for the user. The screen shows the DFU tutorial (`install-screens.md` section 6) and a button "Start". Nothing is ejected before it.
2. **`stateBefore`.** `present` when an install record of the Classic for this serial says installed (section 9.3) and no uninstall came after it, `absent` when the last record of this serial is an uninstall, `unknown` otherwise.
3. **`classic-dfu`** with `install`, the target of the iPod, the bootloader file of the package, `stateBefore`, and the data folder of the app as `dataDir` (section 5.1). The `progress` events are relayed: `eject`, `spin-down`, `pause`, then `wait-device` each second (the countdown and "Hold MENU and the center button"), `quiet` ("Release the buttons now", given by the helper as soon as it sees the DFU device, and again after each reset of its wait), `send`, `resume`.
4. **The wait for the return.** When the result comes, the helper has released the iPod. The app now waits, through detection, for the iPod to come back in normal mode, for at most `RESTART_WINDOW` (`device-state.md` section 2.6). The iPod is held, so the card shows `rebooting`.
5. **TacetOS detected.** The iPod is back in normal mode, with the same serial, and its firmware line says that something other than Apple's firmware runs (`running` is `other`, `detection.md` section 7.6; in v0.2 the firmware has no TacetOS line yet, so the line is `custom`). Then the step writes the install record (section 9.3) and is done.

### 9.3 The install record of the Classic

The helper writes no record for a DFU transfer: the app does. A file `install-record.json` in the backup of the flow: `format` 1, `operation` (`install` or `uninstall`), the date (UTC, ISO 8601), `serial`, `model`, the version of the package, the name and sha256 of the bootloader file, `firmwareFolder` (the firmware folder of the model, which this bootloader looks in first, `firmware-branding.md` section 8.3), and `serialUnderTacetos`: the serial the iPod gave when it came back under TacetOS, when it is not `serial` (detection.md section 9.4). It is what `stateBefore` reads (section 9.2 step 2), and what the uninstall uses to say "TacetOS installed on <date>".

The app names an iPod by the serial of its flow: its card, its progress file and Resume, its backups and records, its window. For a Classic under TacetOS, that is the `serial` of the last install record whose `serialUnderTacetos` is the serial seen now, so the iPod keeps the name it had under Apple's firmware; any other iPod keeps the serial seen. The helper and the mount always get the serial seen. During a flow, the iPod of the flow is the one of that serial, or else the one on its port, and the check `single-ipod` does not count it twice. This only works on the computer that installed: nothing more is written on the iPod.

### 9.4 What else can happen

| What the app sees | Code | What it says and does |
|---|---|---|
| During `wait-device`, the iPod comes back as a normal mode iPod on the same port (with a disk, after it had left), instead of DFU mode | `disk-mode-instead` | Not a failure: the runner sends `cancel` to the helper (the waits can be cancelled, `helper.md` section 10.2), and the step waits for the user again. The screen explains that MENU and the center button must be held, not the center button and Play (`install-screens.md` section 6) |
| After Start, `classic-dfu` answers `disk-gone` before the `send` stage: the disk service does not know the disk (the iPod restarts, or is still in DFU mode) | none | Not a failure, nothing was done: the step gives the stage `wait-disk`, waits for detection to show the iPod with its disk on the same port (looking again every 2 s), then sends `classic-dfu` again by itself, without a second Start. The disk is the one detection shows now, its name can change after a restart. After `RESTART_WINDOW` without the disk, the step waits for the user again with the reason `no-disk` |
| No DFU device within the wait of the helper (60 s, `bootloader-classic.md` section 5 step 1) | `dfu-failed` with the hint of the helper | "The iPod did not appear in DFU mode", and the step waits for the user again |
| Two iPods in DFU mode, another id | `dfu-failed` | The message of `bootloader-classic.md` section 16.4 |
| The iPod does not come back in `RESTART_WINDOW` | `ipod-not-back` | The messages of `bootloader-classic.md` section 12: reset, then DFU mode, then a restore by Apple's software |
| The iPod comes back with Apple's firmware | `bootloader-not-taken` | "The iPod restarted but the change was not made. It is as before" (`bootloader-classic.md` section 16.2). Offers to try once more; after a second time, the causes of section 16.3 |
| The iPod comes back in DFU mode, after it had left the bus | `back-in-dfu` | "The iPod went back to DFU mode": the buttons were held. Not an error of the install: reset the iPod |
| The iPod comes back as a WTF id | `wrong-mode` | "The iPod is in the wrong mode. Reset it" |

A development build stops at the result of `classic-dfu` with `dev-build`, before the eject (`helper.md` section 9.4): the step fails with that code, and the screen says it plainly.

## 10. The uninstall

The uninstall takes the iPod back to Apple's software only (D40): no bootloader, and no firmware folder on the volume.

1. **`backup-settings`** copies the kept files of section 7.3 (settings and themes) to a `settings` backup, and on an older iPod the firmware partition.
2. **Older iPod: `disk-mode`, then `remove-bootloader`**: `read-state`, then `uninstall-bootloader` with `expectedState` `installed` and the backup folder (`helper.md` section 6.5), then `read-state` again, which must say `clean`.
3. **Classic: `classic-remove`**: the same as section 9.2, with `uninstall` and no bootloader file. The return is awaited the same way; the end is the iPod back with **Apple's** firmware running (`bootloader-classic.md` section 14.4). Back with something else running: `still-tacetos` ("The iPod still starts TacetOS. Try again"), with one more try offered.
4. **`remove-firmware`** first waits for the music volume, as long as the step before waited for the iPod: the Classic comes back from `classic-remove` before its volume is mounted (8 s on the real one). It fails with `volume-not-back` only when the wait is over. Then it deletes the firmware folder from the music volume, and the folders with the two suffixes of section 7.4 if a cut left them. Then the install records of the Classic for this serial get a record of the uninstall (section 9.3), which is how a later `stateBefore` knows.
5. A later install of this serial offers to put the kept files back (section 7.3).

**Nothing to do.** On an older iPod, when `read-state` says `clean` and there is no firmware folder, the uninstall does not start: "This iPod has no TacetOS. There is nothing to remove". On a Classic, the state of the NOR cannot be read: when there is no firmware folder and no install record, the screen says "TacetOS does not seem to be on this iPod. Remove it anyway?", and the flow runs if the user says yes (it is harmless, `bootloader-classic.md` section 14.3 step 3).

**Order.** The bootloader goes first, then the folder. A cut between the two leaves an iPod that starts Apple's firmware with a folder that does no harm; the resume deletes it.

## 11. The end

### 11.1 `verify`

Reads everything again, from the iPod and not from memory:

- **Install**: the iPod is `ready`; the firmware folder passes the check after of section 7.4 step 6; on an older iPod, `read-state` says `installed`; on a Classic, the iPod came back with `running` `other` in this flow (the fact `returned` `tacetos` of `classic-bootloader`, or its install record for a flow from before that fact), and runs `other` now, or `apple` since the double boot starts Apple's firmware when Menu is held at power on.
- **Uninstall**: on an older iPod, `read-state` says `clean`; on a Classic, `running` is `apple`; there is no firmware folder.

After a restart, detection sees the iPod before its volume is mounted (6 s on the real Classic): the step first waits for the iPod to be `ready`, as long as the step before waited for its return, then reads. The iPod of the flow is the one of its serial, or else the one on its port: the Classic gives another serial under TacetOS than under Apple's firmware. A difference fails the step with `verify-failed` and what differs. Nothing is written by `verify`.

### 11.2 `finish`

The last screen. It says:

- **Install**: "TacetOS is installed", the version of the package, and how to start each firmware: for the Classic, `bootloader-classic.md` section 13.3 (from Apple's firmware to TacetOS, and back); for an older iPod, the text of its model file. The app asks to eject the iPod before unplugging it (`bootloader-legacy.md` section 18), with the eject of the home screen.
- **Uninstall**: "Apple software only", and where the settings were saved.
- The backup of the flow: its folder, with a button that opens it.

`finish` keeps these as facts: `backup` (the backup folder of the flow), `version` (the package, after an install) and `settings` (the folder of the settings backup, after an uninstall). The runner gives them to the screen with the end of the flow, since the progress file that held them is then gone.

Then the progress file is deleted, the lock is released, and the journal gets the end of the flow.

## 12. The report

The report is a text the user can paste on the Discord TacetOS when something went wrong (D18). The app builds it and shows it; it never sends it anywhere.

### 12.1 What it holds

From the journal (`device-state.md` section 3) and the last flow:

- the version of the app, the system and its version, the version of the disk service;
- the model of the iPod and its family, the firmware line;
- the flow, the path, the step reached, and the codes of the failures, with their `detail`;
- the entries of the journal of the last flow, and the `REPORT_CONTEXT` entries before it.

### 12.2 What is taken out

Every text of the report goes through one pure function that removes:

| What | Replaced by |
|---|---|
| A serial of an iPod (every serial that detection saw in this session or that a progress file or backup names), anywhere in a line | `ipod-` and the first 8 hexadecimal digits of an HMAC-SHA256 of the serial with a key kept in the data folder (`report-key`, 16 random bytes made the first time). The same serial always has the same name, on this computer |
| The home folder, the account name | `~` and `<user>`, as `device-state.md` section 3.2 (done again: some texts do not come from the journal) |
| The labels of the music volumes of the iPods detected | `<volume>` |
| The backup folder, when it is not under the home folder | `<backups>` |
| The name of the computer | `<host>` |

The serial made safe for a file name (section 5.1) is replaced too, so that a backup path does not show it.

### 12.3 The two forms (D40)

- **The summary**: at most `REPORT_SUMMARY_MAX` characters (one Discord message), to copy. First the lines of section 12.1 (one per item), then the entries of the journal of level `warning` and `error` of the last flow, then the last entries of the flow, the newest kept when there is no room. It is cut at the end of a whole line, and then ends with `... (N more lines in the full report)`. The log lines are inside a code block, whose fences count in the limit.
- **The full report**: every line of section 12.1, then every entry, to save as a `.txt` file that the user chooses where to put.

## 13. Errors

The wizard has its own codes, and passes on the codes of the helper (`helper.md` section 13) with their messages. Every message names a way out, or says there is none.

| Code | When | What the app says, and the way out |
|---|---|---|
| `no-serial` | Section 2.1 | "The serial number of this iPod could not be read. TacetOS cannot keep its backups apart" |
| `flow-running` | Sections 2.4, 3.5 | "Another TacetOS window is working on this iPod" |
| `progress-refused` | Section 3.4 | "The saved progress of this iPod cannot be used. Start again: your backups are kept" |
| `progress-mismatch` | Section 3.3 | The same |
| `no-progress` | Resume with no progress file | Nothing: the card offered Resume by mistake, it offers a new flow |
| `bad-flow` | A flow asked with a shape that section 2.2 does not allow, or a step with no code | "Internal error" and the log |
| `check-blocking` | Section 4.1 rule 4 | The message of the check |
| `ipod-gone` | Section 3.3 | "The iPod was unplugged. Plug it in again and press Resume" |
| `backup-full-disk` | Section 5.2 | "The backup folder is full", the space needed, "Choose another folder" |
| `backup-read-failed` | Section 5.3 | "Some files of the iPod could not be read", the count. Nothing was written to the iPod |
| `backup-verify-failed` | Section 5.5 | "The copy on this computer does not match the iPod. Check the disk of this computer, then try again" |
| `source-changed` | Section 5.5 | "Files changed on the iPod during the backup. Close the programs using the iPod" |
| `backup-name-refused` | Section 5.5 | "Some names of the iPod cannot be used on this computer", the count |
| `backup-not-verified` | Sections 5.6, 6.1, 8.2 | "No verified backup of this iPod. Run the backup again" |
| `backup-damaged` | Section 5.7 | "This backup is damaged", how many files were put back |
| `restore-write-failed` | Section 5.7 | "Some files could not be written back to the iPod. Check that it has room, then try again", how many files were put back |
| `erase-failed` | Section 6.4 | "Some files of the iPod could not be erased. Unplug it, plug it in again, then press Resume" |
| `volume-not-back` | Sections 6.3, 7.4 and 10 | "The iPod was formatted, but its volume did not come back. Unplug it, plug it in again, press Resume" |
| `package-format`, `package-wrong-model`, `package-incomplete`, `package-damaged`, `package-other-folder`, `package-changed` | Section 7.1 | The message of the check, "Choose another package" |
| `firmware-no-room` | Section 7.2 | "Not enough space on the iPod for TacetOS", the space needed |
| `firmware-write-failed` | Section 7.4 | "The firmware could not be written to the iPod. Press Resume": section 7.5 cleans up |
| `bootloader-restored` | Section 8.2 | "The backup was written back: the iPod is as it was before the bootloader. Press Resume to try again" |
| `ipod-not-back` | Sections 8.1, 9.4 | Section 9.4 |
| `disk-mode-instead`, `bootloader-not-taken`, `back-in-dfu`, `wrong-mode` | Section 9.4 | Section 9.4 |
| `still-tacetos` | Section 10 | "The iPod still starts TacetOS. Try again" |
| `nothing-to-do` | Section 10 | "This iPod has no TacetOS. There is nothing to remove". Not a failure of the iPod: the flow stops and nothing was written |
| `verify-failed` | Sections 8.2, 11.1 | What differs, and the way out of that step |

## 14. Constants at a glance

Kept in the one file of constants of the wizard, with the same names. The names the firmware looks for are in `src/device/constants.h` (section 7.3).

| Name | Value | Section |
|---|---|---|
| `PROGRESS_FORMAT` | 1 | 3.2 |
| Progress folder | `wizard/` in the data folder | 3.1 |
| `BACKUP_MANIFEST_FORMAT` | 1 | 5.4 |
| Default backup folder | `TacetOS Backups` in the documents folder | 5.1 |
| `BACKUP_MARGIN` | 1 GiB | 5.2 |
| `COPY_PIECE` | 1 MiB | 5.5 |
| Speed window for the time left | 10 s | 5.5 |
| `VOLUME_RETURN` | 30 s, starting value | 6.3, 8.1 |
| `RESTART_WINDOW` | 60 s (`device-state.md` section 4) | 8.1, 9.2 |
| `PACKAGE_MANIFEST_MAX` | 64 KiB | 7.1 |
| `VOLUME_MARGIN` | 64 MiB | 7.2 |
| `NEW_SUFFIX`, `OLD_SUFFIX` | `.tacetos-new`, `.tacetos-old` | 7.4 |
| `ZIP_ENTRY_MAX` | 65 536 | 7.6 |
| `ZIP_UNPACK_MAX` | 1 GiB for a firmware | 7.6 |
| `REPORT_SUMMARY_MAX` | 2000 characters | 12.3 |
| `REPORT_CONTEXT` | 200 entries | 12.1 |

## 15. What the tests need

Nothing in the tests touches a real disk, a real iPod, the system bus, a real USB device or `pkexec`. The data folder, the backup folder and the "music volume" are temporary folders; the disks are images made by the fake iPod tool of the tests; the files of the music come from the fixtures of the tests; the packages are made by the tests. Nothing is ever read from the user's own folders.

### 15.1 The flow (sections 2 and 3)

- The list of each family, flow, path and Mac format gives the list of section 2.3.
- A runner with steps that do nothing: the signals in order, a wait, a cancel during a wait, a failure that stops the flow.
- A cut at each step (the runner destroyed in the middle of the step): the progress file says `running`, a new runner resumes at the right step with its rule of section 3.3.
- A progress file that is not JSON, with a missing key, another `format`, another serial, another list of steps, or `current` out of range: refused, renamed, nothing resumed.
- Two runners for the same serial in one process, and a lock held by a live process: `flow-running`. A stale lock is taken over.

### 15.2 The checks (section 4)

- One case per line of the tables of sections 4.2 and 4.3, with fake sources: the result, blocking or not, the message.
- The command of section 4.4 for files of Fedora, Ubuntu, Debian, Arch, a derivative found by `ID_LIKE`, an unknown system, and no file.
- The order, and the iPod checks `skipped` when `helper` fails.

### 15.3 The backups (section 5)

- A temporary folder as the volume, with files of the fixtures, names with accents, empty files, a deep folder, a hidden folder: the backup, its manifest, the dates kept.
- A cut in the middle, then the resume: the files already verified are not copied again.
- A file changed during the copy (changed once: copied again; changed twice: `source-changed`).
- A backup folder that becomes full (the same way of making a folder full as the helper's tests).
- The firmware partition from end to end on an image: client, helper program, image.
- A damaged manifest or file: restore refused, nothing written.
- The format refused without a verified `full` backup.

### 15.4 The writes (sections 6 to 10)

- Format and conversion from end to end on images (older iPods in FAT32 and Mac format, Classic): a wrong typed name, a helper cut in the middle (the cut program of the helper's tests), a volume that does not come back (recorded detection events).
- The firmware with packages made by the test: every refusal of section 7.1, a zip refused by section 7.6, a volume that is full, the kept files byte for byte, and a cut at each row of section 7.5.
- The zip reader: stored and deflate entries, Zip64 records, every refusal of section 7.6, a CRC-32 that does not match.
- The bootloader of the older iPods on the images of each family, the 2nd generation nano included: install, update, `inconsistent` stopped before writing, a cut at each point of the helper's tests, no backup refused.
- The Classic with the fake iPod in DFU mode of the helper's tests and recorded detection events: success, Disk Mode instead of DFU mode, no DFU mode in time, no return, back with Apple's firmware, back in DFU mode.
- The uninstall: each family, nothing to do, a cut between the bootloader and the folder, back with TacetOS still running.

### 15.5 The report (section 12)

- Each row of section 12.2 on journals made by the test; the same serial gives the same name twice; two serials give two names.
- The summary cut at a whole line at the limit, the code block counted, the line of what is left.

## 16. Not decided yet

- The serial: the real Classic gives one in normal mode under Apple's firmware, and another one under TacetOS, which the install record keeps (section 9.3). Disk Mode and the older models are still to be seen. An iPod without one cannot run the wizard today (section 2.1).
- How the user starts Apple's firmware on each older model with the bootloader installed. The text comes from the model files, which do not say it yet (section 11.2).
- `VOLUME_RETURN` and `RESTART_WINDOW` keep their starting values. Measured on the real install of a 120 GB Classic (2026-11-22): after the DFU send, the iPod was back on the same port and `ready` again, its volume mounted, 6 to 10 s later; `verify` waited 9 s of its 10.5 s for the volume. That leaves a margin of three times or more. The older iPods are still to be measured.
- An iPod that Apple's firmware leaves in its recovery screen ("Use iTunes to restore", after a DFU try that went wrong) is shown `ready`, with its firmware read as Apple's: only the check `apple-firmware` stopped the real install, because the version did not fit. The app should know that state and say "Restore the iPod with Apple's software first" on the card. What tells it apart (the device sheet, missing files on the volume) is still to be found on a real iPod in that state.
- The copy speed and the backup of an iPod full of music: the real Classic was empty (the backup took 0.3 s), so the time shown before a clean install is still a guess.
- Whether a format of a large iPod asks for the password of the disk service twice (`helper.md` section 16), which would add a wait in the middle of the step.
- Names that the computer refuses (section 5.5): failing is safe, but a user with such names cannot make a clean install. Renaming them in the backup, with a list in the manifest, is for a later version.
- Whether the list of kept files (section 7.3) is complete for the firmware of v0.3, which adds its own settings.
- A real format of the iPod Classic (4096-byte sectors, music in slot 1), and the conversion of a Classic formatted for Mac. This version empties the volume of a Classic instead (section 6.4).
