# The helper

The helper is the one program of TacetOS that reads and writes the sectors of a disk, and the one that talks to an iPod Classic in DFU mode. The app never does it itself: it asks, the helper checks and acts. This spec says how the app and the helper talk, which commands exist, what the helper refuses, and how each system gives it the rights it needs. It is meant to be enough to write the helper and the code of the app that drives it.

It has four parts:

- **The process and its messages** (sections 1 to 4): where the helper sits, how it starts, the format of the messages.
- **The commands** (sections 5 and 6): the disk named in a request, then the closed list of commands with their arguments and results.
- **The guards** (sections 7 to 10): the refusals, the lock on each disk, the gate of development builds, progress and cancel.
- **The systems** (sections 11 and 12): Linux in full, Windows and macOS in outline.

Section 13 lists the error codes, section 14 the constants, section 15 what the tests need, and section 16 the points still open.

**Order to write the code.** First the process, the messages, the table of commands, the lock, the gate and the journal, with a single command (`read-state`) on disk image files. Then the commands of the older iPods (sections 6.4 to 6.8), still on image files, tested from end to end through the real helper program. Then the Linux layer of section 11 (udisks2, sysfs, pkexec), tested on a fake bus and on recorded sysfs trees. The Classic command (section 6.9) and the device sheet (section 6.3) come with their first real test. Windows and macOS come with the ports of those systems.

The behaviour of each operation on the disk (the checks, the order of the writes, what to do after a failure) is not repeated here: it is in `bootloader-legacy.md` and `bootloader-classic.md`. This spec is about the envelope around them.

## 1. Words used here

- **App**: the TacetOS desktop program, the one with windows.
- **Helper**: the program `tacet-helper`, started by the app, with no window.
- **Client**: the part of the app that starts the helper, sends requests and reads answers. Nothing else in the app talks to the helper.
- **Request**: one message from the app, asking for one command.
- **Event**: one message from the helper, about one request: accepted, progress, log line, result.
- **Target**: the disk a request is about: a real disk, or a disk image file (section 5).
- **Real disk**: a disk of the computer, as opposed to an image file.
- **Image file**: a regular file that holds the bytes of a whole disk, used by the tests and by development.
- **Scan token**: an opaque text that `read-state` returns, and that every later command on the same disk gives back, so that the helper can tell that the disk is still the one the user saw (section 5.3).
- **First write**: the moment the helper asks the system to write the first sector of an operation, or sends the first block of a DFU transfer that cannot be taken back (section 10.3).
- **Data folder**: the folder of the app's data on the computer, where backups, install records and the state file of paused programs are kept.

## 2. Where the helper sits

### 2.1 A separate process

The helper is a separate program, started by the app as a child process. There are three reasons for this:

1. **One door.** Every sector written to a disk goes through a short list of fixed commands, each with its own checks. A bug in the screens of the app cannot write to a disk, because the app has no code that does.
2. **Rights.** On Windows, reading and writing a whole disk needs the rights of an administrator. Only the helper asks for them, never the app (section 12.1). On Linux and macOS, the helper runs as the user, and the system asks for the user's password when the disk is opened (sections 11.1 and 12.2).
3. **Crash isolation.** If the app crashes in the middle of a write, the helper finishes the write and its verification (section 10.4).

### 2.2 What goes through the helper, and what does not

Through the helper:

- every read or write of the raw sectors of a disk, the reading of the state of an iPod included (it reads the partition table and the firmware partition);
- the SCSI questions to a disk (section 6.2);
- every USB exchange with an iPod in DFU mode;
- the unmount of a volume when it is part of a write (a format, a conversion, the eject before DFU mode);
- the one change to the system that needs the administrator: the udev rule on Linux (section 6.11).

Not through the helper:

- the files of a mounted volume: the music, the firmware files of TacetOS, the marker files of detection. They are written through the file system, as any program writes files, and never sector by sector;
- the mount, unmount and eject that the user asks for from the home screen (`device-state.md` section 2.1), done by the disk service of the system;
- the watching of disks and USB devices for detection.

"Every disk write goes through the helper" means the first list. A write of a file on a mounted volume is not a disk write in this sense.

### 2.3 Finding and starting the helper

The app looks for the helper in one place only: the folder of its own executable (or, in a package, the folder of private programs that the package declares). Never in the `PATH`, never from a setting. If the program is not there, the app says "The helper program is missing. Reinstall TacetOS", and offers nothing that needs it.

The app starts the helper the first time a command is needed, and keeps it running until the app quits. One helper per app. The helper takes no options on its command line: any argument makes it exit with code 2 and a line on its error output. Everything it needs comes in the requests.

On Linux and macOS, starting the helper does not ask for any password: the password comes when a disk is opened. On Windows it does (section 12.1).

### 2.4 The rights of each system, in short

| System | The helper runs as | A disk is opened by | USB in DFU mode | Administrator asked for |
|---|---|---|---|---|
| Linux | the user | udisks2 (`OpenDevice`), with polkit's password window | libusb, allowed by a udev rule | only to put the udev rule in place, through `pkexec` |
| Windows | an administrator (UAC, once per helper) | the system, directly | libusb with WinUSB, or Apple's driver | at the start of the helper |
| macOS | the user | the system's `authopen` tool, with its password window | IOKit | never |

## 3. Messages

### 3.1 Framing

The app writes to the helper's standard input, the helper writes to its standard output. Each message is **one line**: a single JSON object, in UTF-8, ended by a line feed (`\n`, on every system). The helper's error output is not part of the protocol: the app copies it to the journal as `warning` lines.

Rules:

1. A line longer than `MAX_REQUEST_LINE` bytes is refused (`bad-request`) and the rest of it up to the next line feed is thrown away.
2. A line that is not a JSON object is refused (`bad-request`).
3. Numbers are integers. Sizes and offsets are given in bytes, never in sectors, and stay below 2^53 so that every JSON reader keeps them exact.
4. Binary data (a sector, a page of the device sheet) is written in base64.
5. Text for people is English. It goes to the journal, never to the screen: the app shows its own messages, translated, chosen from the error code (section 13).

### 3.2 The hello

As soon as it starts, before reading anything, the helper writes one event:

```json
{"event":"hello","protocol":1,"version":"0.1.0","system":"linux","realWrites":false,"classic":true,"usb":true}
```

| Field | Meaning |
|---|---|
| `protocol` | The version of this spec's messages. Changed whenever a message changes |
| `version` | The version of the helper program, the same as the app it ships with |
| `system` | `linux`, `windows` or `macos` |
| `realWrites` | The gate of section 9: whether this build may write to a real disk |
| `classic` | Whether this build has the parts of the Classic DFU image (`bootloader-classic.md` section 18). Without them, `classic-dfu` answers `unavailable` |
| `usb` | Whether this build can talk to USB devices (the USB library was found at build time) |

If the hello does not come within `HELLO_TIMEOUT`, or if `protocol` or `version` differ from the app's, the app stops the helper and says "The helper program does not match this version of TacetOS. Reinstall TacetOS". It never talks to a helper of another version.

### 3.3 Requests

```json
{"id":12,"cmd":"read-state","args":{"target":{"kind":"image","path":"/home/u/fixtures/ipod-video.img","sectorSize":512}}}
```

| Field | Meaning |
|---|---|
| `id` | A positive integer, greater than every `id` the app has sent before to this helper |
| `cmd` | One name of the list of section 6 |
| `args` | An object with exactly the arguments of that command (section 6) |

### 3.4 Events

Every event carries the `id` of the request it answers, and an `event` field:

| `event` | When | Other fields |
|---|---|---|
| `accepted` | The request has passed the checks of section 7 steps 1 to 4 and has started | none |
| `progress` | During the command, at most every `PROGRESS_INTERVAL`, and at each change of stage | `stage`, `done`, `total`, `cancellable` (section 10) |
| `log` | A line for the journal | `level` (`info`, `warning`, `error`), `text` |
| `result` | The end of the request. Always the last event of a request, and always exactly one | `ok`, then `data` when ok, or `error` and `detail` when not |

A request that is refused before it starts gets a `result` with no `accepted` before it. A `result` with `ok` false has:

- `error`: one code of section 13;
- `detail`: one line of English for the journal, the reason behind the code;
- `data`: when the command says so, what the app needs to act on the failure (for example the step that failed and the state read again, `bootloader-legacy.md` section 19).

### 3.5 Strict reading

The helper reads a request the strict way. Any of these is a refusal, `bad-request`, with nothing done:

- a field other than `id`, `cmd` and `args`, or one of them missing;
- an `id` that is not greater than the last one;
- an argument that the command does not take, an argument missing, an argument of the wrong type, or a value outside its list;
- a path that is not absolute.

An unknown `cmd` is refused with `unknown-command`. The helper never guesses what an argument meant, and never fills one in with a default that the command does not list.

### 3.6 One command at a time

The helper runs at most one command at a time. A request that arrives while another one runs is refused at once with `busy`, except `cancel` (section 6.12). The queue of `device-state.md` section 2.4 lives in the app, not in the helper: the app sends the next request after the `result` of the last one.

Different helper processes (two copies of the app, or a development tool) may run at the same time. The lock of section 8 keeps them off the same disk.

### 3.7 The end of the helper

- **The app closes the helper's input** (the app quits, or crashes): the helper cancels the running command if it can still be cancelled (section 10), otherwise finishes it with its verification and its install record (section 10.4), then exits with code 0.
- **Exit codes**: 0 after a normal end, 2 for a wrong command line, 3 when the helper cannot write to its output any more. Any other code, or the helper killed, is a crash for the app.
- **The helper exits while a request runs**: the app ends that operation as `failed` with the code `helper-crashed`. For a write, it then reads the state of the iPod again, as `bootloader-legacy.md` section 19 says for "a directory write", before offering anything.

## 4. The client in the app

The client is a small class of the app around the helper process. It:

1. starts the helper (section 2.3) and waits for the hello (section 3.2);
2. gives each request its `id`, writes it, and calls back the code that asked for it with each event of that `id`;
3. turns `log` events into entries of the journal (`device-state.md` section 3), with the domain `op` and the port path of the iPod of the request, through the same cleaning as every other entry;
4. never shows the `detail` text: the screen gets a translated message chosen from the `error` code;
5. when the helper exits while a request runs, gives that request the result `helper-crashed` (section 3.7), and starts a new helper for the next request.

The client never kills the helper while a command is not cancellable (section 10.3). When the user closes the window then, the app says "TacetOS is writing to the iPod. Wait until it is done", and stays open until the `result` comes.

## 5. The disk named in a request

### 5.1 The target

Every command about a disk has an argument `target`, which is one of:

```json
{"kind":"disk","node":"/dev/sdb","sizeBytes":160041885696,"serial":"000A27001A2B3C4D","port":"1-2"}
{"kind":"image","path":"/home/u/fixtures/ipod-video.img","sectorSize":512}
```

**A real disk** (`kind` `disk`) is named by what detection has seen (`detection.md`): the name the system gives the whole disk (`/dev/sdb` on Linux, `\\.\PhysicalDrive2` on Windows, `disk4` on macOS), its size, the USB serial string of the iPod (empty when the system gives none), and the port path. The helper trusts none of it: it finds the disk again by itself (section 11.2 on Linux) and compares. If anything differs, it refuses with `disk-changed`.

**An image file** (`kind` `image`) is a regular file of the computer, with the sector size given by the request, because a file cannot report one. Rules:

1. The path is absolute. Links are followed, and the file they lead to must be a **regular file**: a device, a folder, a pipe or anything under `/dev` is refused (`bad-target`).
2. `sectorSize` is 512, 2048 or 4096.
3. The size of the file is a whole number of sectors, and not zero.

An image file is never "removable and on USB", and it does not need to be: the refusals of section 7 step 3 are for real disks. It has no port and no serial.

### 5.2 The whole disk only

A target is always a whole disk, never a partition: the helper reads the partition table itself. A name that the system gives to a partition (`/dev/sdb2`, a volume of Windows, `disk4s2`) is refused with `bad-target`. So is any disk that is not a plain disk of the system: a loop device, a device-mapper or RAID volume, a network block device, an optical drive.

### 5.3 The scan token

`read-state` returns a `scanToken`. It is made by the helper from what it read, and given back by the app in every later command on the same disk. It holds:

- for every target: the size of the disk, the sector size, and a SHA-256 of sector 0 and of the first sector of the firmware partition (the bytes compared by check 1 of `bootloader-legacy.md` section 11);
- for a real disk, in addition: the USB bus number and device number of the iPod. They change each time a device is plugged in, even on the same port, so a token taken before an unplug never matches after it (`bootloader-legacy.md` section 24, rule 11).

The app does not read the token. The helper does not keep it either: each command reads the same values again on the disk and compares. A forged or old token can only fail.

Every command that writes requires a token, and also the state that was shown to the user (`expectedState`): the helper refuses with `state-changed` if the state it reads now is another one (`bootloader-legacy.md` section 13, rule 1).

### 5.4 The data folder

The commands that make files (a backup, an install record, the state file of paused programs) take an argument `dataDir`: the data folder of the app, given by the app. The helper checks that it is an absolute path, that it is a folder or can be created, and that it is **not on the target disk**: a backup kept on the iPod it saves is no backup. Otherwise: `bad-data-dir`.

The helper never deletes a backup, an install record or a file it did not create.

## 6. The commands

### 6.1 The closed list

The list below is all there is. A command that is not in it is refused (`unknown-command`), and adding one means changing this spec first.

| Command | What it does | Writes to a disk | Section |
|---|---|---|---|
| `read-state` | Recognises the iPod and reads its state | no | 6.2 |
| `read-device-sheet` | Reads the property list the iPod gives about itself | no | 6.3 |
| `backup-firmware` | Copies the firmware partition to a file | no | 6.4 |
| `install-bootloader` | Installs the bootloader of an older iPod | yes | 6.5 |
| `update-bootloader` | Replaces it with a newer one | yes | 6.5 |
| `uninstall-bootloader` | Removes it | yes | 6.5 |
| `restore-backup` | Writes a backup of the firmware partition back | yes | 6.6 |
| `format-music` | Writes an empty FAT32 file system in the music partition | yes | 6.7 |
| `convert-to-fat32` | Turns a Mac iPod into a Windows iPod | yes | 6.8 |
| `classic-dfu` | Installs or removes the bootloader of an iPod Classic, through DFU mode | yes (the NOR of the iPod) | 6.9 |
| `resume-paused` | Resumes the programs a crash left paused | no | 6.10 |
| `install-udev-rule` | Puts the DFU udev rule in place (Linux) | no (a system file) | 6.11 |
| `cancel` | Asks the running command to stop | no | 6.12 |

Each section below gives the arguments of its command, in a table. Every argument is required unless the table says otherwise.

### 6.2 `read-state`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | Section 5.1 |

The helper opens the disk **for reading only** and runs the recognition of `bootloader-legacy.md` sections 2 to 6, then the reading of section 10. The `data` of the result:

| Field | Meaning |
|---|---|
| `scanToken` | Section 5.3 |
| `sectorSize` | As reported by the system, or as given for an image |
| `sizeBytes` | The size of the disk |
| `table` | `dos`, `apple` or `none` |
| `slots` | The slots of `bootloader-legacy.md` section 2.3: start and size in bytes, and the type |
| `method` | `partition` (an older iPod, section 6 of the legacy spec found it), `dfu` (a Classic: one FAT32 partition, no firmware partition), or `unknown` |
| `target` | For `partition`: the model target of `bootloader-legacy.md` section 5 |
| `state` | For `partition`: `clean`, `installed` or `inconsistent`, with the message of section 10 for the journal |
| `entries` | For `partition`: the directory entries (type, `devOffset`, `len`, `entryOffset`), for the journal and the support |

A disk that is not recognised as an iPod is still a success, with `method` `unknown` and the reason in a `log` event: the command reads, it does not decide what the app offers.

### 6.3 `read-device-sheet`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | A real disk only: an image file has no device to ask (`bad-target`) |

The iPod in Disk Mode or in normal mode answers SCSI INQUIRY requests for vendor pages of vital product data, which together hold a small property list (XML) about the device: the exact model, the version of Apple's firmware, the serial number and more. The helper reads it **without writing anything**:

1. It opens the disk for reading only.
2. It sends INQUIRY (operation code `0x12`) with the EVPD bit set, for page `0xC0`. The answer lists the page codes that hold the property list.
3. For each code of that list, in order, it sends the same request for that page, and appends the data of the answer (after the 4-byte header of the page) to the text.
4. It stops at `SHEET_MAX_BYTES` (refusal `sheet-too-big`), and checks that the text starts like an XML document (otherwise `sheet-unreadable`).

INQUIRY is the only SCSI command the helper ever sends. It has no other: no mode page, no vendor command, nothing that writes.

The `data` of the result is the text of the property list, as the iPod gave it. Reading the keys of that list is the app's work (a pure function, tested on recorded sheets), and every key used must first be seen on real iPods (`detection.md` section 13). An iPod that does not answer page `0xC0` gives `sheet-unsupported`, which is not an error for the app: it goes on without the sheet.

### 6.4 `backup-firmware`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | Section 5.1 |
| `scanToken` | text | Section 5.3 |
| `dataDir` | text | Section 5.4 |

Opens the disk for reading only, checks the token, and copies the whole firmware partition to a new file of the data folder, named as in check 6 of `bootloader-legacy.md` section 11, then reads the file back and compares. The `data` is the path of the backup. This is the backup the user can ask for alone, before any install; the write commands of 6.5 make their own as well.

### 6.5 `install-bootloader`, `update-bootloader`, `uninstall-bootloader`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | Section 5.1 |
| `scanToken` | text | Section 5.3 |
| `expectedState` | text | `clean` for an install, `installed` for an update or an uninstall |
| `bootloaderFile` | text | The path of the bootloader file, as downloaded. Not taken by the uninstall |
| `bootloaderVersion` | text | The version written in the install record. Not taken by the uninstall |
| `dataDir` | text | Section 5.4: the backup and the install record go there |

The helper reads the bootloader file itself (at most `BOOT_FILE_MAX` bytes), so that what is written is what was checked. It runs the 8 checks of `bootloader-legacy.md` section 11, then the operation of sections 12 to 17, then the verification and the install record of section 18. The serial number in the record is the one the helper read on the disk, not the one of the request.

The disk is opened for reading first, and for reading and writing only after the checks have passed (`bootloader-legacy.md` section 13.1).

The `data` of a success: the path of the backup, the path of the install record (or the reason it could not be written), and the warnings of check 7. The `data` of a failure after the first write: the step that failed, the state read again (`clean`, `installed`, `inconsistent` or `unread`), what the app does next as in the table of `bootloader-legacy.md` section 19, and whether the backup was already written back.

### 6.6 `restore-backup`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | Section 5.1 |
| `scanToken` | text | Section 5.3 |
| `backupFile` | text | The backup to write back. It must be in `dataDir` |
| `dataDir` | text | Section 5.4 |

"Writing the backup back" of `bootloader-legacy.md` section 19. The helper refuses a backup whose sector 0 and banner do not match the disk (`backup-mismatch`). The app sends it only when the user accepts the offer of section 19, except for the case "the move of images", where the helper writes the backup back by itself during the failed command, without a second request.

### 6.7 `format-music`

| Argument | Type | Meaning |
|---|---|---|
| `target` | object | Section 5.1 |
| `scanToken` | text | Section 5.3 |
| `ipodName` | text | The name of the iPod shown in the dialog of `bootloader-legacy.md` section 22 |
| `typedName` | text | What the user typed in it |
| `volumeLabel` | text | The label of the new file system |

First of all, before the disk is even opened, the helper compares `typedName` with `ipodName` with the rule of section 22 (spaces around the typed text do not count, the case does, an empty name never matches). A mismatch is refused with `confirmation-mismatch`. The confirmation is checked here, in the helper, and not only in the dialog: a request without the typed name can never format.

Then the helper unmounts the music partition (refusal `in-use` when it cannot), runs the checks of section 20.1 and writes section 20. The `data` of a success is the size of the new file system. The app asks the disk service to mount the volume again, as it does for any iPod.

### 6.8 `convert-to-fat32`

The arguments of `format-music`, plus `dataDir` (section 5.4) for the backup of section 21 step 1. Same confirmation, checked first. Then section 21, in its order: backup, format of the music partition, new sector 0, recognition again. The `data` of a success is the result of the recognition again, as `read-state` would give it, with a new `scanToken`.

### 6.9 `classic-dfu`

| Argument | Type | Meaning |
|---|---|---|
| `operation` | text | `install` or `uninstall` |
| `target` | object | The disk of the Classic, in normal mode or Disk Mode, to eject (a real disk only) |
| `bootloaderFile` | text | For an install: the bootloader file given to the image builder (`bootloader-classic.md` section 9). Not taken by the uninstall |
| `stateBefore` | text | `present`, `absent` or `unknown`: whether the bootloader was there, as the app read it |
| `dataDir` | text | Section 5.4: the state file of paused programs (`bootloader-classic.md` section 15.1) |

The app checks the conditions of `bootloader-classic.md` section 3 before it sends this command, and copies the firmware files to the music volume first (condition 4). The helper then:

1. Checks that the target is a Classic: its USB product id is the one of the Classic in normal mode in `docs/models/`. Anything else is refused (`not-classic`).
2. Builds the image itself, from the parts of the build and the bootloader file, and checks it as it is about to be sent (`bootloader-classic.md` sections 8 and 9: for an install, the single boot byte must be 0). The app never sends an image: the helper only sends what it built and checked.
3. Ejects the iPod, waits for the disk to stop, pauses Apple's programs (section 15.1 of the Classic spec).
4. Waits for exactly one device `05ac:1223` (section 5 step 1), then the quiet time of section 16.1 with its check of the address. Each second of the wait is a `progress` event, so that the app can show the countdown and the "release the buttons now" message.
5. Sends the image (section 5 steps 2 to 7), then resumes what it paused, in every case.

The result comes when the device has been released (step 7). Watching for the iPod to come back and reading its state (step 8, section 16.2) is the app's work, done by detection, which already watches the USB bus.

Any device other than `05ac:1223` is refused, even when the user picks it (section 6 of the Classic spec). A failure gives the code of section 13 for its case and the `hint` of the Classic spec in `detail`.

### 6.10 `resume-paused`

| Argument | Type | Meaning |
|---|---|---|
| `dataDir` | text | Section 5.4 |

Resumes the programs noted in the state file of `bootloader-classic.md` section 15.1, then deletes the file (the only file the helper deletes, since it made it). The app sends this command at its start, before any other, when the file exists. On Linux, nothing is ever paused, and the command answers at once.

### 6.11 `install-udev-rule`

No arguments. Linux only: on the other systems, `unsupported-system`.

The helper puts the udev rule of `bootloader-classic.md` section 15.2 in place, so that the user can talk to an iPod in DFU mode without being root. The text of the rule is **built into the helper**: it is never read from a file given by the app. The steps are in section 11.5. The `data` of a success says whether the rule was already there, in which case nothing was asked of the user.

The packages of TacetOS install the rule themselves; this command is for the builds that are not installed by a package (AppImage, archive). It is offered by the app only when the rule is missing and the user starts a Classic install.

### 6.12 `cancel`

| Argument | Type | Meaning |
|---|---|---|
| `request` | integer | The `id` of the running request |

Answered at once, without waiting for the running command:

- `ok` true when the running command can still be cancelled. The running command then ends with the error `cancelled`, at its next stopping point (section 10.2);
- `too-late` when the first write has been made (section 10.3). The running command goes on to its end;
- `no-such-request` when that request is not running.

## 7. Refusals

### 7.1 In this order

The helper checks a request in this order, and stops at the first refusal. Up to step 4 it has opened nothing.

1. **The message**: section 3.5 (`bad-request`, `unknown-command`).
2. **Another command runs**: section 3.6 (`busy`).
3. **The typed confirmation**, for `format-music` and `convert-to-fat32` (`confirmation-mismatch`).
4. **The target** (section 5): its shape (`bad-target`). For a real disk:
   - the disk is found again by the helper, and its size, serial and port are the ones of the request (`disk-changed`, `disk-gone`);
   - the disk is **removable and on USB**, as the system reports both (`not-removable-usb`). Section 11.2 says how on Linux;
   - the USB device it belongs to has Apple's vendor id `05ac` and a product id of an iPod in `docs/models/` (`not-an-ipod`);
   - no file system of the disk holds the system, the home folder or the data folder, and no partition of it is used as swap (`system-disk`). This one cannot fail on a real iPod; it is there so that no mistake in the steps above can ever reach a disk of the computer.
5. **The lock** of section 8 (`disk-locked`).
6. **Opening the disk**: refused by the user (`permission-denied`), held by another program (`in-use`), or another failure (`open-failed`).
7. **The recognition** of `bootloader-legacy.md` section 6, and the token of section 5.3 (`not-an-ipod`, `unsupported-model`, `disk-changed`, `state-changed`).
8. **The checks of the command**: `bootloader-legacy.md` section 11, section 20.1, section 21; `bootloader-classic.md` sections 3 and 8 (`check-failed`, with the number of the check in `data`, or the code of the case).
9. **The gate** of section 9 (`dev-build`), at the moment of the first write.

The `accepted` event comes after step 4.

### 7.2 Why "removable and on USB"

Every iPod that TacetOS supports is seen by the computer as a removable disk on USB. A disk inside the computer, a USB hard disk that reports itself as fixed, a memory card in a built-in reader, a USB stick: none of them is an iPod. A USB stick is removable and on USB, so the test of Apple's vendor id is the one that turns it away. Each of the three tests is enough to refuse; together they leave no room for a disk that is not an iPod.

The helper reads these facts from the system each time. It never takes them from the request.

## 8. The lock on each disk

`bootloader-legacy.md` section 24, rule 10: two operations on the same iPod at the same time must never happen. The helper holds a lock on the disk for the whole command:

1. **What is locked**: a real disk by the number the system gives it while it is plugged in (on Linux, its device number, major and minor), an image file by its path after the links are followed, the USB side of `classic-dfu` by the id `05ac:1223` (only one iPod in DFU mode is allowed anyway).
2. **How**: one lock file per locked thing, in a folder of the user that is cleared at each start of the system (on Linux, `$XDG_RUNTIME_DIR/tacetos/locks/`, or the temporary folder when the variable is not set). The lock file holds the process id of its owner. A lock left by a process that no longer runs is stale, and is taken over.
3. **When**: taken at step 5 of section 7.1, before the disk is opened, released after the `result` is written, in every case: success, failure, cancel, exception.
4. **Every command on a disk takes it**, the ones that only read included: a read in the middle of another process's write would read a state that is neither the old one nor the new one.
5. **No wait**: a lock already held is a refusal (`disk-locked`), not a queue.

The lock is between processes. Inside one helper, section 3.6 already allows only one command at a time.

The helper does not use the system's own exclusive open of a disk (on Linux, `O_EXCL`): it fails as long as a partition of the disk is mounted, and the music partition of an older iPod stays mounted during an install (`bootloader-legacy.md` section 13.1).

## 9. The gate of development builds

`bootloader-legacy.md` section 11 check 8, and the rule of the project: before the beta, a development build must not write to a real iPod.

1. The gate is a build option (`TACETOS_ALLOW_REAL_WRITES`), off by default, on only for the builds of the beta and after. It is read at build time and cannot be changed after: no environment variable, no file, no argument turns it on. Building with the option on is the one way to test on one's own iPod.
2. The helper reports it in the hello (`realWrites`), so that the app can show "This development build does not write to iPods" next to the buttons.
3. The gate applies to every write to a real disk and to the DFU transfer to a real iPod. It does not apply to image files, which any build may write.
4. On a development build, a write command still runs everything up to the first write: the target, the lock, the recognition, the checks, the backup (which only reads the disk). It stops there with `dev-build`. This is how the checks are tried on a real iPod before the beta.
5. Reading (`read-state`, `read-device-sheet`, `backup-firmware`) and `install-udev-rule` are never gated: they change nothing on an iPod.

## 10. Progress and cancel

### 10.1 Progress

A `progress` event has:

| Field | Meaning |
|---|---|
| `stage` | One name of the list of the command, below |
| `done`, `total` | Bytes done and bytes to do in this stage, or seconds for the waits. `total` 0 when it is not known |
| `cancellable` | Whether a `cancel` would still be accepted (section 10.3) |

The stages, in their order (a command goes through those that apply to it):

| Command | Stages |
|---|---|
| `read-state` | `open`, `read` |
| `read-device-sheet` | `open`, `read` |
| `backup-firmware` | `open`, `check`, `backup` |
| `install-bootloader`, `update-bootloader`, `uninstall-bootloader` | `open`, `check`, `backup`, `write`, `verify`, `record` |
| `restore-backup` | `open`, `check`, `write`, `verify` |
| `format-music` | `unmount`, `open`, `check`, `write`, `verify` |
| `convert-to-fat32` | `unmount`, `open`, `check`, `backup`, `write`, `verify` |
| `classic-dfu` | `check`, `eject`, `spin-down`, `pause`, `wait-device`, `quiet`, `send`, `resume` |

The stage `open` can last as long as the user takes to type a password: there is no time limit on it, and the app shows "Waiting for your permission". The app shows the time an operation takes before the start, from the size of the work, and says "Keep the iPod plugged in" (`bootloader-legacy.md` section 13, rule 7).

### 10.2 Stopping points

A command that can be cancelled looks at the cancel at least between two pieces of reading (`bootloader-legacy.md` section 25, the size of a piece), at each second of a wait, and at each change of stage. When it stops, it closes what it opened, resumes what it paused, releases the lock, and answers `cancelled`. Nothing has been written then.

### 10.3 The first write

Once the first write has started, the command cannot be cancelled any more (`bootloader-legacy.md` section 13, rule 7). The helper sends a `progress` event with `cancellable` false right before it, and the app disables its cancel button on that event. From then on, a `cancel` gets `too-late`.

| Command | The first write is |
|---|---|
| The 3 bootloader commands, `restore-backup` | The first sector written to the disk |
| `format-music`, `convert-to-fat32` | The first sector written to the music partition (after the unmount, which is not a write) |
| `classic-dfu` | The empty block that ends the transfer (section 5 step 5 of the Classic spec). Until then, the ROM has only received data in memory, and stopping changes nothing on the iPod (section 16.4) |

### 10.4 When the app goes away during a command

If the input of the helper closes (section 3.7): before the first write, it is a cancel; after it, the helper goes on to the end, the verification and the install record included, because stopping half way is the one thing the order of the writes is made to avoid. The events it writes then have no reader. The next start of the app reads the state of the iPod again, as for any iPod plugged in, and finds the install record in the data folder.

## 11. Linux

### 11.1 Opening a disk: udisks2

The helper runs as the user and is never root (decision of the project: udisks2 for the disks, `pkexec` only for the udev rule). It asks udisks2, the disk service of the system, for an open file of the disk, with the method `OpenDevice` of the block device object (on the system D-Bus, interface `org.freedesktop.UDisks2.Block`):

- mode `r` for every command, and again `rw` only for the writes, after the checks (`bootloader-legacy.md` section 13.1);
- the polkit action behind it is `org.freedesktop.udisks2.open-device` for a removable disk. Its usual default asks for the administrator's password and keeps the answer for a few minutes, so that the read and the write of the same operation ask once. The answer of udisks2 when the user closes the window or gives a wrong password is `permission-denied`;
- for a disk that udisks2 counts as a disk of the system, the action is `open-device-system`, which always asks. An iPod is never one: if the helper meets it, section 7.1 step 4 has already refused;
- the file given back is the only way the helper touches the disk. It does not open `/dev/sdX` by itself.

`OpenDevice` exists in udisks2 since version 2.7.3. An older udisks2 is refused with `unsupported-system`, and the app says which version it needs (the check of the app at start).

This works the same from an AppImage and from a Flatpak (with access to the system bus for udisks2), and needs nothing installed beyond udisks2, which every desktop distribution has.

### 11.2 Finding the disk again, removable and on USB

From the `node` of the request, the helper:

1. Finds the block device object of udisks2 whose `Device` (the `/dev` name) is that node, and checks that it is not a partition (it has no `Partition` interface) and is not a loop device (no `Loop` interface). Then its drive object.
2. Reads on the drive: `Removable` (or `MediaRemovable`) must be true, and `ConnectionBus` must be `usb`. Reads `Size` and `Serial`, and compares them with the request.
3. Reads the system's device tree (`/sys/dev/block/<major>:<minor>`, then up through its parents): the first parent that is a USB device gives `idVendor`, `idProduct`, `busnum`, `devnum`, the port path (its name, for example `1-2`) and `serial`. The vendor must be `05ac` and the product one of `docs/models/`. The port and the serial must be the ones of the request.
4. Reads the mounted file systems (`/proc/self/mountinfo`) and the swap areas (`/proc/swaps`) for the check `system-disk` of section 7.1.

The reading of the device tree and of the D-Bus answers is done by pure functions, fed in the tests with recorded trees and recorded answers (section 15).

### 11.3 Unmounting

For `format-music`, `convert-to-fat32` and the eject of `classic-dfu`, the helper asks udisks2 to unmount (`Filesystem.Unmount`) the music partition, without the `force` option. The eject of `classic-dfu` then powers the drive off (`Drive.PowerOff`), as the eject of `device-state.md` section 2.1. A refusal because a program uses the volume is `in-use`.

### 11.4 The device sheet

The SCSI requests of section 6.3 go through the open file of section 11.1 (mode `r`) with the generic SCSI request of Linux (`SG_IO`). INQUIRY is one of the commands the kernel allows on a file opened for reading only: no other right is needed.

### 11.5 The udev rule

The rule gives the user of the session access to `05ac:1223` and to nothing else (`bootloader-classic.md` section 15.2). Its file in the system is `/etc/udev/rules.d/70-tacetos-dfu.rules` (a number below 73, so that the access tag is applied).

1. If the file is there and its content is the rule built into the helper, the command succeeds at once, with nothing asked.
2. Otherwise the helper writes the rule to a new file of its own temporary folder, and runs `pkexec` once, with `/bin/sh`, a fixed script built into the helper, and the path of that file as the script's only argument. The script copies the file into place, owned by root and readable by all (`install -m 0644 -o root -g root`), then reloads the rules (`udevadm control --reload-rules`). Nothing of the request goes into the script: it has no argument the app could change.
3. The exit code of `pkexec`: 126 when the user closed the password window (`cancelled`), 127 when the authorisation was refused (`permission-denied`), anything else that is not 0 is `udev-failed` with the code in `detail`.
4. The helper reads the file of the system back and compares it with the rule. A difference is `udev-failed`.
5. The rule is applied when the device appears, so the app then asks the user to put the iPod in DFU mode again.

When `pkexec` is missing (a Flatpak, a minimal system), the command answers `unsupported-system`, and the app shows the rule and the two commands to type in a terminal, as `bootloader-classic.md` section 15.2 says. The app never runs `sudo`.

`pkexec` is never used for anything else.

### 11.6 USB in DFU mode

Through libusb, as `bootloader-classic.md` sections 6 and 15.2 say. The helper needs no other right than the one the udev rule gives.

## 12. Windows and macOS, in outline

These are the choices as known now. They are made precise, and tested on a real system, with the port of each system.

### 12.1 Windows

- **Rights.** Opening a whole disk (`\\.\PhysicalDriveN`), even for reading, needs an administrator. The helper is started elevated (the system's request for administrator rights, once per helper), the app never is.
- **The channel.** An elevated program does not inherit the standard input and output of the program that started it. The messages of section 3 then go through a named pipe that the app creates before it starts the helper, with a random name given on the helper's command line (the one argument the helper takes on Windows) and access for the current user only. The messages themselves do not change.
- **Removable and on USB.** The storage property query of the disk gives the bus type (`BusTypeUsb`) and whether the media is removable; the parent USB device gives the vendor and product ids.
- **Writes.** Whole sectors, at offsets that are multiples of the sector size, from aligned buffers (`bootloader-legacy.md` section 13.1). Before a format or a conversion, the music volume is locked and dismounted by the helper.
- **DFU.** Apple's driver or WinUSB, as `bootloader-classic.md` section 15.3 says. Apple's background programs are paused and resumed by the helper.
- **The lock** of section 8 uses the disk number, in the temporary folder of the user.

### 12.2 macOS

- **Rights.** The helper runs as the user. It opens the raw disk (`/dev/rdiskN`) with the system's `authopen` tool, which asks for the administrator's password and hands the open file back: the same idea as `OpenDevice` on Linux, with no program of TacetOS running as root.
- **Removable and on USB.** The properties of the disk in the system's disk registry (removable, the physical interconnect `USB`), and the vendor and product ids of the USB device above it.
- **Writes.** The system refuses to write to a disk that has a mounted volume: the helper unmounts the whole disk first, through the system's disk service, and the app waits for the volume to be mounted again at the end (`bootloader-legacy.md` sections 13.1 and 18).
- **DFU.** IOKit, no administrator, Apple's programs paused and resumed (`bootloader-classic.md` section 15.4).
- **The lock** of section 8 uses the BSD name of the disk (`disk4`), in the temporary folder of the user.

## 13. Error codes

The code is what the app reads; the `detail` is for the journal. The app shows its own message for each code, translated, and **every message names a way out** (or says there is none).

| Code | When | What the app says |
|---|---|---|
| `bad-request` | Section 3.5 | "Internal error, the helper refused a request" and the log |
| `unknown-command` | Section 3.5 | The same |
| `busy` | Section 3.6 | Nothing: the app's queue waits and sends again |
| `bad-target` | Section 5 | "Internal error" and the log |
| `bad-data-dir` | Section 5.4 | "Cannot write to the data folder" and the folder |
| `disk-gone` | Section 7.1 step 4 | "The iPod was unplugged" |
| `disk-changed` | Sections 5, 7.1 steps 4 and 7 | "The iPod was unplugged" and the scan starts again |
| `state-changed` | Section 5.3 | "The iPod changed since it was read" and the scan starts again |
| `not-removable-usb` | Section 7.1 step 4 | "This disk is not an iPod" |
| `not-an-ipod` | Sections 7.1 steps 4 and 7 | "This disk is not an iPod" |
| `system-disk` | Section 7.1 step 4 | "This disk is not an iPod", and the log at the level `error` |
| `unsupported-model` | Section 7.1 step 7 | "An iPod TacetOS does not support" |
| `not-classic` | Section 6.9 | "This iPod is not an iPod Classic" |
| `disk-locked` | Section 8 | "Another TacetOS window is working on this iPod" |
| `permission-denied` | Sections 7.1 step 6, 11.5 | "Permission denied", and how to give it on this system |
| `in-use` | Sections 7.1 step 6, 11.3 | "Close the programs using the iPod" |
| `open-failed` | Section 7.1 step 6 | "The iPod could not be opened" and the log |
| `confirmation-mismatch` | Sections 6.7, 6.8 | Never shown: the button of the dialog stays off until the name matches |
| `check-failed` | Section 7.1 step 8 | The message of that check in the spec of the operation |
| `dev-build` | Section 9 | "This development build does not write to iPods" |
| `cancelled` | Section 10 | Nothing: the user asked for it |
| `too-late` | Section 6.12 | "The iPod is being written. Wait until it is done" |
| `no-such-request` | Section 6.12 | Nothing |
| `write-failed` | A write, a flush or a read back after the first write | As the table of `bootloader-legacy.md` section 19 says, from the `data` |
| `verify-failed` | `bootloader-legacy.md` section 18 | The same table, row "final verification" |
| `backup-failed` | Check 6 of `bootloader-legacy.md` section 11 | "Cannot save a backup in the data folder" and the free space needed |
| `backup-mismatch` | Section 6.6 | "This backup is not of this iPod" |
| `dfu-failed` | `bootloader-classic.md` sections 7 and 16 | The message and the way out of that case, from the `data` |
| `sheet-unsupported` | Section 6.3 | Nothing: the app goes on without the sheet |
| `sheet-too-big`, `sheet-unreadable` | Section 6.3 | Nothing, the log only |
| `unavailable` | Section 3.2 (`classic` or `usb` false) | "Classic install is not available in this build" |
| `udev-failed` | Section 11.5 | "The USB permission could not be installed", and the rule and its two commands |
| `unsupported-system` | Sections 6.11, 11.1, 11.5 | What is missing on the computer and the command to install it |
| `helper-crashed` | Section 3.7 | "The helper stopped", then what `bootloader-legacy.md` section 19 says for a directory write, after a new reading of the state |

## 14. Constants at a glance

| Name | Value | Where |
|---|---|---|
| `PROTOCOL` | 1 | Section 3.2 |
| `HELLO_TIMEOUT` | 10 s | Section 3.2 |
| `MAX_REQUEST_LINE` | 65 536 bytes | Section 3.1 |
| `PROGRESS_INTERVAL` | 250 ms at most between two `progress` events of the same stage | Section 3.4 |
| `BOOT_FILE_MAX` | 4 MiB | Section 6.5 |
| `SHEET_MAX_BYTES` | 65 536 bytes | Section 6.3 |
| The sheet's list page | `0xC0` | Section 6.3 |
| Image sector sizes | 512, 2048, 4096 | Section 5.1 |
| Lock folder (Linux) | `$XDG_RUNTIME_DIR/tacetos/locks/` | Section 8 |
| The udev rule's file | `/etc/udev/rules.d/70-tacetos-dfu.rules` | Section 11.5 |
| Oldest udisks2 | 2.7.3 | Section 11.1 |
| Exit codes | 0 normal, 2 command line, 3 output closed | Section 3.7 |

## 15. What the tests need

Nothing in the tests talks to a real disk, to udisks2, to `pkexec` or to a USB device. The real helper program runs in the tests only on image files.

### 15.1 The messages and the guards (sections 3 to 10)

- The helper program, started by the test as the app starts it, on fixture images of `bootloader-legacy.md` section 26: the hello, a `read-state` that reads the image right.
- Refusals, each with nothing written (the image compared byte for byte before and after): an unknown command, an argument too many, an argument missing, a wrong type, a line too long, an `id` that does not grow, a relative path, a device or a folder given as an image, a second command while one runs (`busy`).
- The lock: two helper programs on the same image, the second gets `disk-locked`; a stale lock (a process id that does not run) is taken over; the lock is released after a failure and after a cancel.
- The gate: a request on a "real disk" through the fake system layer of 15.3, with the gate off, runs up to the first write and stops with `dev-build`, with nothing written; the same on an image file writes.
- Cancel: accepted during the reading (nothing written), refused with `too-late` after the `progress` event with `cancellable` false; the input closed before the first write cancels, after it the command ends and the image is in its new state.
- A typed name that does not match: `confirmation-mismatch`, the image not even opened.

### 15.2 The commands of the older iPods (sections 6.4 to 6.8)

One test from end to end per command (client, helper program, image file), each checking the result, the image after, and the files of the data folder: backup, install, update, uninstall, a write that fails (the failing image of `bootloader-legacy.md` section 26.3) with the `data` of section 19, restore, format, conversion. A `dataDir` inside the target image's own folder is fine; a `dataDir` on the target disk is refused through the fake layer of 15.3.

### 15.3 Linux (section 11)

- A fake of udisks2 on a private bus, or behind an interface of the helper: answers recorded from real systems for an iPod, a USB stick, a USB hard disk that reports itself fixed, an internal disk, a memory card reader. Each one is accepted or refused as section 7.2 says, with the right code.
- Recorded device trees (`/sys`) for the same disks, in the folder of the tests, read through a root folder given to the code. Names that Windows refuses in a file name (a `:` in the folders of `/sys`) are protected so that the tests run on every system.
- The `pkexec` step: the command line built for it (the script, the one argument) and the mapping of its exit codes, with a fake runner. Never the real `pkexec`.
- The device sheet: the parsing of recorded INQUIRY answers (the list page, then the pages), and the stop at `SHEET_MAX_BYTES`.

## 16. Not decided yet

- The page codes of the device sheet (section 6.3) come from public documentation of the iPod and from tools that read it. They must be read on the Classic at hand, in Disk Mode and in normal mode, before the app uses the sheet. Whether the older iPods answer the same way is open until one is tested.
- Whether the password that udisks2 keeps "for a few minutes" is enough for a long operation that opens the disk twice (a format of a large iPod reads, then writes). If not, the helper keeps the file it opened in mode `rw` from the first open of a write command, still writing nothing before the checks.
- Whether `udevadm control --reload-rules` is enough on every distribution, or whether some need `udevadm trigger` as well for the access to apply the next time the iPod enters DFU mode.
- Windows: the elevated helper and its named pipe (section 12.1) against the other ways (a service, a helper elevated only for the writes, with a second one for DFU, which needs no rights). To be decided with the Windows port.
- macOS: `authopen` (section 12.2) against a helper installed with the system's service manager, which needs a signed app. To be decided with the macOS port, after a test on a real Mac.
- Where the logs of a helper whose app went away should go (section 10.4). Today they are lost; the install record and the state read again are enough to explain the result.
