# Detecting a plugged-in iPod

How TacetOS notices that an iPod is plugged in, which mode it is in, which model it is, which volume of the computer is its music partition, and what is already on it. This spec is meant to be enough to write the `device` module without having seen any other installer. Detection only reads. It never writes to the iPod, never mounts, and never asks for more rights than a file manager has, except for the read-only disk reads that already belong to `bootloader-legacy.md`.

Detection answers in three layers, each one refining the previous one:

- **USB** (sections 3 and 4): is an iPod plugged in, in which mode, and which models it can be.
- **Volume** (sections 5 and 6): which disk and which mounted volume belong to that USB device.
- **Marker files** (section 7): what the volume holds that tells the model, the firmware and the state of the iPod.

Section 8 merges the three into one result, section 9 says how it stays live while the user plugs and unplugs, section 10 gathers the messages, section 11 the constants, section 12 the test fixtures, and section 13 the points still open.

**What is not here.** Reading the partition tables, the firmware banner and the firmware directory of the older iPods is in `bootloader-legacy.md` (sections 2 to 6, and 10). The DFU modes and the double boot of the Classic are in `bootloader-classic.md`. The facts per model (USB ids, install method) are in `models/models.toml`, which is the source of truth for ids: the tables below only show what the code needs to be tested.

**Order to write the code.** Pure functions first, fed with recorded data (sections 4, 6, 7 and 8, fixtures of section 12). Then the three system readers, one per OS, behind one interface. Then the live part (section 9) with a fake clock. Real hardware comes last.

## 1. Words used here

- **Device**: one USB device, with a vendor id, a product id, and a place where it is plugged.
- **USB id**: the pair `vendor:product`, four lowercase hexadecimal digits each, for example `05ac:1261`. The same shape as in `models.toml`, so that a plain text compare works.
- **Port path**: where the device is plugged, as the OS names it. It stays the same when the device resets and comes back on the same port.
- **Disk**: the whole storage the OS shows for a device, as in `bootloader-legacy.md`.
- **Volume**: a filesystem of that disk that the OS has mounted, with a mount point (a folder, or a drive letter on Windows).
- **Music volume**: the mounted volume of the disk that holds the music. On the older models it is the second partition (slot 2 of `bootloader-legacy.md` section 2.3), on the Classic the first one (section 6.1).
- **Marker file**: a file or folder whose presence, name or content tells what the iPod is or what it runs.
- **Firmware folder** and **info file**: the hidden folder that the TacetOS firmware lives in, on the music volume, and the small text file that every firmware build puts in it. Their real names are in the one constants file of the app (the same file as in `bootloader-classic.md` section 9), never written anywhere else.
- **Evidence**: one piece of information about the iPod, with its origin: a USB id, a disk reading, a marker file.
- **Helper**: as in the other specs. Detection uses it only for the read-only disk reads.

## 2. What detection produces

One **detected iPod** per iPod found. The app keeps a list of them, and the list changes live (section 9). A detected iPod holds:

| Field | Meaning |
|---|---|
| `usb_id` | The id seen on USB, or none if the iPod is only known from a volume that was picked by hand (section 6.4) |
| `mode` | `normal` (normal mode or Disk Mode, they share the id), `dfu`, `other` (an id of the same model family that is neither) or `none` (no USB id, section 6.4) |
| `port_path` | The place on the computer, used to follow the same iPod across a reset |
| `serial` | The USB serial number string, when the OS gives it. Optional |
| `candidates` | The models the USB id can be, from `models.toml`. One, several, or none |
| `model` | The model decided by section 8, or none |
| `model_source` | Which evidence decided: `usb`, `disk`, `info-file`, `user` |
| `variant` | The exact iPod inside the model (capacity and release period, section 8.4), or none. The model decides what can be written, the variant is only for display |
| `variant_source` | Which clue decided the variant: `device-sheet`, `sysinfo`, `capacity`, or none |
| `disk` | The node of the disk, its size in bytes and its sector size. Optional |
| `volume` | Mount point, filesystem type, label, free bytes. Optional |
| `state` | One of the states of section 8.3 |
| `firmware` | What the marker files say is installed (section 7.4) |

The user never sees the words `candidates` or `model_source`. They exist so that the logs, and the support on the Discord, can show why a decision was taken.

## 3. Layer 1: the USB list

### 3.1 What is read

The list of USB devices the computer sees now, with for each: vendor id, product id, port path, and the serial number string if the OS has it in its own database. **The app does not open the devices** to read their name strings: opening needs rights on some systems, and it can disturb a device in the middle of a transfer. Names for the logs come from the OS database when it is free to read, or from a copy of the public id list, and are never used to decide anything.

| System | Where the list comes from | Notes |
|---|---|---|
| Linux | The folders of `/sys/bus/usb/devices`. Each device folder holds the files `idVendor` and `idProduct` (4 hexadecimal digits, no prefix), `busnum`, `devpath`, `serial` | The port path is the folder name, for example `1-2.3`. No special rights are needed |
| macOS | The IOKit registry, class of USB devices. Properties `idVendor`, `idProduct`, `USB Serial Number`, `locationID` | The port path is the `locationID`. No special rights |
| Windows | The device information set of all present devices (SetupAPI). The hardware id of a USB device is a list of strings, for example `USB\VID_05AC&PID_1261&REV_0001` and `USB\VID_05AC&PID_1261` | Compare in upper case: the case of the keys differs between Windows versions, and old ones write `Vid_` and `Pid_`. Accept both forms of the string, with or without the `&REV_` part. The port path is the location path property of the device |

Only devices with the vendor `05ac` (Apple) are kept; all the others are dropped at once and never logged beyond a count. Apple's other products (phones, tablets, keyboards) share this vendor id. An Apple id that is in no model of `models.toml` is not an iPod we know: it is not shown to the user, and the log keeps one line for it.

### 3.2 Mapping an id to the models

`candidates` is the list of models of `models.toml` that have the id in one of the three lists of `[model.usb]`. The `mode` comes from the list the id is found in:

- in a model's `normal` or `disk_mode` list: `normal`;
- in a model's `dfu` list: `dfu`;
- an id that is in the `normal` list of one model and in the `dfu` list of another is not expected. If it ever happens, the mode is `other` and the app asks the user.

Cases to know, all from `models.toml` and from `bootloader-classic.md` section 2:

| Id | What it is | What the code does |
|---|---|---|
| `05ac:1205` | 1st and 2nd generation mini | Two candidates. Neither the USB id nor anything cheap tells them apart. The disk reading of `bootloader-legacy.md` section 5 does (`0x40` or `0x70`). Without it, the info file target (section 7.3), then the user |
| `05ac:1223` | DFU mode of the Classic. The 3rd generation nano uses the same id in DFU mode, but it is not in `models.toml` | One candidate, the Classic, but the id cannot rule out the nano. Before anything is sent, the app asks "iPod Classic?" (`bootloader-classic.md` section 2), unless it has seen the same iPod in normal mode just before (section 9.4) |
| `05ac:1261` | Classic, normal or Disk Mode | One candidate |
| Disk Mode of every other model | The same id as the normal one | `mode` is `normal` for both. The app does not need to know the difference: what it needs is a disk (section 5) |

The older first and second generation iPods have no USB, only FireWire, per `models.toml`. They cannot be detected by this spec; their `usb` lists are empty and stay so until a real one is tested (see `to_verify` there).

### 3.3 Normal mode without a disk

An iPod in normal mode, when the user turned "disk use" off in Apple's software, shows on USB (the id is known) but exposes no disk. The app must not treat that as an error before waiting: the OS needs a few seconds to create the disk and mount it, after the USB device appears (section 9.2). After the wait, `state` becomes `no-disk` (section 8.3) and the message says how to get a disk: enable disk use, or put the iPod in Disk Mode (the buttons of the model page). This behaviour is reported by users of Apple's software and has to be confirmed on a real iPod (section 13).

## 4. Pure function: from the raw list to the devices

The code is split so that the reading (system specific) and the decision (not) are different functions. The decision functions take plain data and return plain data, and are tested without any iPod.

1. `parseUsbList(raw, system)` returns a list of `{usb_id, port_path, serial}`. One parser per system, fed with the recorded output of that system (section 12).
2. `classifyUsb(list, models)` drops every non Apple entry, then returns, for each remaining entry that matches a model, the fields `usb_id`, `mode`, `candidates`, `port_path`, `serial`.
3. Two entries with the same `port_path` cannot exist at the same time. If a reader returns them anyway (it can happen during a reset), keep the one that appears last in the list and log the other.

## 5. Layer 2: from the USB device to a disk

Finding a disk that "looks like an iPod" by scanning all the disks is what `bootloader-legacy.md` section 6 does for the installer. For detection the app does better and cheaper: it starts from the USB device, and finds the disk that sits under it. A disk with no USB ancestor of vendor `05ac` is never opened, never listed, never shown. A USB stick or an external drive is therefore never read by detection, which the existing installer does not guarantee (it looks at the files of every mounted volume).

The link is found in the device tree of the OS, going up from the disk until a USB device is met:

| System | Going up from the disk | Then |
|---|---|---|
| Linux | Take `/sys/class/block/<name>`, resolve its link, and go to the parent folders until one has the files `idVendor` and `idProduct` | Compare them with the USB device. The disk is the whole-disk node (`sdb`), its partitions are the nodes below it (`sdb1`, `sdb2`). A partition has a file `partition` that holds its number |
| macOS | For each disk (DiskArbitration), take its BSD name (`disk2`), then go up the IOKit registry from the media object until a USB device | The partitions are `disk2s1`, `disk2s2`. DiskArbitration also gives the protocol of the device, which must be `USB`, the kind of filesystem and the volume path |
| Windows | For each disk device of the system, ask for its device number (the `N` of `\\.\PhysicalDriveN`), then go up the device tree from the disk device instance (parent after parent) until an id that starts with `USB\VID_` | The volume of a partition is found by asking each volume which disk it lives on (section 6.1) |

The match with the USB device is made on the **port path**, not on the id alone. If two iPods of the same model are plugged in, they have the same id and two different port paths, and each gets its own disk. Using the id alone would give both iPods the same disk.

If the USB device exists and the walk finds no disk under it, the iPod has no disk yet (section 3.3). If a disk is found but its USB ancestor has an id that is not in `models.toml`, the disk is ignored.

Disk reads of `bootloader-legacy.md` sections 3 to 5 (table, banner, model) happen on that disk, **through the helper, read-only**, and only for models whose install method is `partition`. They need rights the user may not have. A refusal for permission is not "not an iPod": it is recorded as a missing evidence, and the detected iPod keeps its USB `candidates`. The Classic has no model to read on its disk (its id is unique), so it never needs this read for detection.

## 6. Layer 2 continued: from the disk to the music volume

### 6.1 Finding the mounted volume

Given the disk of section 5, the music volume is one of the mounted filesystems of its partitions. The slot is not fixed: the older models keep their firmware in slot 1 and the music in slot 2, but a real Classic has a single FAT32 partition, in slot 1, on a disk of 4096 byte sectors. When several partitions of the disk are mounted, the first one in this order wins: a FAT32 or HFS+ filesystem before any other (section 6.2), then slot 2, then the lowest slot. A volume of another filesystem is still kept when it is the only one, so that the state can be `unsupported-filesystem`. The firmware partition of the older models has no filesystem, so it is never mounted and never chosen. A slot is the partition number the system gives (section 5), not a suffix added to the name of the disk, because the names differ from system to system (`sdb2`, `disk2s2`, a drive letter).

| System | How the volume is found |
|---|---|
| Linux | Read `/proc/self/mountinfo`. Each line has the mount point (5th field) and, after a lone `-`, the filesystem type and the source device. Match the source with the node of the partition (`/dev/sdb2`), by the device number (`major:minor`, 3rd field) rather than by name, so that a link such as `/dev/disk/by-label/...` still matches. Mount points escape the space, tab, newline and backslash as `\040`, `\011`, `\012`, `\134`: decode them |
| macOS | The volume path given by DiskArbitration for the partition `disk2s2`. If it has none, the partition is not mounted |
| Windows | For each drive letter, open the volume and ask which disk extents it covers. A volume that covers exactly one extent, on the disk number found in section 5, and at the offset of one of its partitions, is a candidate, and the order above picks the music volume. A volume that spans several disks is ignored |

### 6.2 Filesystems

| What the OS calls it | System | Means |
|---|---|---|
| `vfat`, `msdos` | Linux, macOS | FAT32 (a Windows iPod) |
| `hfsplus`, `hfs` | Linux, macOS | HFS+ (a Mac iPod) |
| `FAT32` | Windows | A Windows iPod |
| `HFS` | Windows, only through a third party driver | A Mac iPod |
| anything else | all | `unsupported-filesystem`: the volume is kept in the result, but nothing is read from it |

A Mac iPod on Windows normally has **no** drive letter, because Windows cannot read HFS+. In this case the disk exists and the volume does not. The state is then `mac-format` when the disk reading (partition test of `bootloader-legacy.md` section 3) told it, and `no-volume` otherwise. On Linux and macOS the HFS+ volume is mounted and its marker files are read, but the state is `mac-format` as well (section 8.3): TacetOS needs FAT32 on every system (`bootloader-legacy.md` section 21). The message must tell the user not to answer yes to a Windows prompt that offers to format the disk.

### 6.3 Which attributes are read

From the music volume, the app reads only: mount point, filesystem type, label (for display only), total and free bytes. The label is whatever the user typed in Apple's software and is never used to decide anything. The size is compared with the `capacities_gb` hint of the model only to log a warning when it is very different; a user may have swapped the storage.

### 6.4 When the link cannot be made

The device tree can be hidden or different: a virtual machine that passes only the disk, a Windows system without the needed rights, an unusual card reader. In this case the app shows a button "Choose the iPod folder", and the user picks a folder. Marker files are then read from it (section 7), the USB id is `none`, and `model_source` is `info-file` or `user`. This path lets the user **look** (what is installed, what the firmware version is) and **never install a bootloader**, because the install needs the disk, and the disk is found only by section 5.

### 6.5 Several iPods

More than one detected iPod is normal: two cables on the computer. Each has its own port path, disk and volume. The app lists them with the model name and the size, and never picks one for the user when an action needs a choice (`bootloader-legacy.md` section 6, step 6).

## 7. Layer 3: marker files

Marker files are read only from a **music volume** found by sections 5 and 6, or from the folder picked by hand (section 6.4). The app does not look at any other volume.

### 7.1 Rules for every read

1. Names are compared without regard to case: FAT and HFS+ on these systems are case insensitive, and the same folder may be listed as `iPod_Control` or `IPOD_CONTROL` depending on the system and the tool that made it.
2. A listing of the root must include **hidden** and **system** entries. The folders that matter here are hidden.
3. A file is read for at most 64 KiB (constant). More than that is not one of our files.
4. A file is read as text only if it holds no zero byte in what was read. Line ends may be `\n` or `\r\n`.
5. Links are never followed outside the volume.
6. A read that fails because the volume is gone (the user unplugged the iPod) is not an error to show: it is the end of that detected iPod (section 9.3).
7. Nothing is written, not even a temporary file.

### 7.2 Apple's files

| Name | What it tells | How it is used |
|---|---|---|
| Root folder `iPod_Control` | The volume was set up by Apple's software, and is an iPod's music volume | Confirms. Absence is **not** an error: a freshly formatted iPod does not have it until the first sync. It is used to give a score when several volumes are candidates in a manual pick (section 6.4), and never alone to decide that something is an iPod |
| `iPod_Control/Device/SysInfo` | A small text file written by Apple's firmware with the model number string and the serial number | Optional evidence of the exact variant (section 8.4). The key name and the layout of the model number are to be checked on a real iPod with Apple's firmware before the code relies on them (section 13). Without it, nothing is lost. It is empty on an iPod that runs another firmware |
| `iPod_Control/iTunes/iTunesDB` | Apple's music database | Detection only checks that it exists, to say "this iPod has music managed by Apple's software". The library module reads it, not this one |

The existing installer used to look for the root folder too, then turned that check off in its sources. The reason is not written there. TacetOS keeps it as a weak confirmation, never as a decision.

### 7.3 The info file

Every build of the TacetOS firmware puts a small text file in the firmware folder (names in the constants file). It is the best evidence of "TacetOS is installed here", of the version, and of the target the firmware was built for.

**Format.** Lines of `Key: value`. A line is a key, a colon, white space and a value. The key starts with an upper case letter and then has letters and spaces. Lines that do not have this shape are skipped (a build also writes lines of compiler names and the like; they are of no use here). The keys detection reads:

| Key | Example | Used for |
|---|---|---|
| `Target` | `ipod6g` | The build target. Compared with the `bootloader_target` of `models.toml` to give the model |
| `Target id` | `71` | The number of the target. Logged. Not used to decide |
| `Version` | `4.0` | The firmware version, shown to the user and logged |
| `Binary` | the name of the firmware file | Logged |

Other keys (`Memory`, `Features`, `Voice format`...) are present in a real file and are ignored by detection. A key may appear twice: the last one wins.

**A valid info file** has the `Target` key, and its value is the `bootloader_target` of a model of `models.toml`. A file without it, or with an unknown target, makes `firmware` say `unknown` but does not make the whole detection fail. The text of the file is never shown as is to the user.

**A TacetOS line is needed.** The info file of a plain build of the firmware base has nothing that tells our firmware from another build of the same base, and `Version` is the version of the base. The firmware build of TacetOS must add one line that names the distribution and its own version, so that detection can say "TacetOS 0.3 installed" and not only "a firmware of version 4.0 is installed". The name of that key is decided with the firmware patches (v0.3 in the roadmap) and goes in the constants file. Until it exists, `firmware` can only be `present` (a valid info file) or `absent`.

### 7.4 State of the firmware as seen from the volume

| `firmware` | When |
|---|---|
| `absent` | The firmware folder does not exist on the volume |
| `present` | The folder exists and the info file is valid, but it carries no TacetOS line |
| `tacetos` | The folder exists, the info file is valid, and it carries the TacetOS line. The version is kept |
| `unknown` | The folder exists but the info file is missing, unreadable, or has an unknown target |

This is the state of the **files**. Whether the **bootloader** is installed is another question, answered differently for each install method: for the older iPods by reading the firmware partition (`bootloader-legacy.md` section 10, needs the helper), for the Classic by nothing the computer can read (the bootloader lives in the NOR flash, not on the disk) except the install record of the app (`bootloader-legacy.md` section 18) and, when the iPod is plugged in running our bootloader, the files above. The app must say "the firmware files are there" and "the bootloader is installed" as two separate statements, and must not claim the second one for a Classic without an install record or a boot check.

### 7.5 What TacetOS writes on the music volume

Nothing for the sake of detection. The record of an install stays on the computer (`bootloader-legacy.md` section 18). The only files TacetOS puts on the music volume are the ones of the firmware itself, and they carry their own info file (section 7.3). A marker file written only so that the app can find the iPod again would be one more thing to clean up, would have to be written through the helper, and could be wrong after the iPod is formatted by someone else.

### 7.6 The firmware line

The line shown for the firmware of an iPod that has a readable volume. It says what the **files** say, like section 7.4, and never that a bootloader is installed (section 7.4, last paragraph: for a Classic nothing the computer reads proves it).

The first row that matches is the line:

| Kind | When | Shown |
|---|---|---|
| `tacetos` | `firmware` is `tacetos` and the info file is for the decided model | `TacetOS <version>`. The value of the distribution line is the version; a `TacetOS` in front of it is dropped |
| `custom` | `firmware` is `present` (a valid info file with no TacetOS line) and it is for the decided model | `Custom firmware <version>`, the `Version` key of the info file |
| `apple` | The Apple model file has at least one line | `Apple firmware <version>`. The version is the part in brackets of the build key (constants file, `0x01168000 (1.1.6)` gives `1.1.6`). Without it: `Apple firmware` |
| `unknown` | Anything else: no volume, an empty Apple model file, an info file with an unknown target, a filesystem that is not read | `Firmware unknown` |

Notes:

- Files for another model (section 8.2 rule 4) are not used for the line: they are not the firmware of this iPod.
- The files come before the Apple model file because the Apple model file is empty on an iPod that runs another firmware, and may stay on the volume when Apple's software was there first. An iPod that boots both (dual boot) gets one line, the one of the files. The app does not know which firmware is running.
- In DFU mode the line of the normal mode is kept with the other fields (section 8.2 rule 5), else it is `unknown`.
- The key of the build in the Apple model file comes from one source only (the model table of libgpod) and is not checked on an iPod yet (section 13).

## 8. Merging the evidence

### 8.1 Order of trust

When two pieces of evidence name different models, the one closer to the hardware wins, and a disagreement is reported, not hidden:

1. **User's choice** (always wins, and is only asked when the others leave an ambiguity).
2. **Disk reading** (`bootloader-legacy.md` section 5): it comes from the firmware that runs on this very iPod.
3. **USB id**, when it has exactly one candidate.
4. **Info file target**: it describes the firmware that was copied to the volume, which might have been copied from another iPod by the user.
5. **`SysInfo`**, once the table is checked (section 13). It can only name a variant of the decided model, never decide the model itself (section 8.4).

### 8.2 The rules

1. If the USB id gives one candidate, `model` is that candidate, `model_source` is `usb`.
2. If it gives several (the minis), the disk reading decides. If there is none, the info file target decides, if it is one of the candidates. If none, `model` stays empty and `state` is `ambiguous`; the user is asked, and the answer is `model_source = user`.
3. If a disk reading exists and names a model that is **not** among the USB candidates, the state is `conflict`. Nothing is offered that writes. The log keeps both values. This case is a failure of the tables or a strange device, not something to guess.
4. If an info file names a model that is different from the one decided by rules 1 to 3, `firmware` gets the extra flag `other-model`. The user is told that the firmware files on the volume are for another model. The decision of rules 1 to 3 is not changed.
5. In DFU mode (`mode = dfu`) there is no disk, so no disk reading and no volume. If the same `port_path` was seen in normal mode in the last 60 seconds (constant, `MODE_MEMORY`), the detected iPod keeps the fields it had (model, volume, firmware), and `state` is `dfu`. Otherwise only the USB candidates are known (section 3.2, `05ac:1223`).

### 8.3 States

| `state` | When | What the app offers |
|---|---|---|
| `waiting` | A USB device of a known id has just appeared, and the wait of section 9.2 is not over | A "reading the iPod" message. Nothing else |
| `ready` | Model decided, volume present and readable | Everything the model allows |
| `dfu` | `mode` is `dfu` | The DFU actions of `bootloader-classic.md`, after the model question of section 3.2 |
| `no-disk` | The USB id is known, the wait is over, there is no disk | How to get a disk (section 3.3) |
| `no-volume` | A disk exists, no mounted music volume | Says why when it knows: Mac format, or not mounted. Does not mount it |
| `mac-format` | The disk reading, or the filesystem of the volume (section 6.2), says HFS+ | The conversion of `bootloader-legacy.md` section 21, with its confirmations |
| `unsupported-filesystem` | The volume exists with a filesystem of section 6.2 "anything else" | Nothing. The text explains |
| `ambiguous` | Several models are possible (section 8.2 rule 2) | A question to the user |
| `conflict` | Section 8.2 rule 3 | A message, no action |
| `permission` | The disk read was refused and it is the only missing piece | How to give the right, per system |
| `unsupported-model` | The disk reading gave a generation that is not in the table of `bootloader-legacy.md` section 5, or the id is a known iPod but with no support | "An iPod we do not support", no action |

The states are listed here so that every one has a test and a message (section 10). The first row that matches in this order is the state: `conflict`, `unsupported-model`, `permission`, `ambiguous`, `dfu`, `waiting`, `no-disk`, `mac-format`, `no-volume`, `unsupported-filesystem`, `ready`.

Where "when" needed a precise meaning, the code reads it like this:

- `waiting` lasts while the wait of section 9.2 is not over **and there is no mounted volume yet**. A volume that shows up early ends it at once. While it lasts, a model that is not decided yet is not asked about (`ambiguous` is skipped), because the disk reading may still decide it.
- `permission` is a disk read that was refused. It comes before `ambiguous` because the read is what would decide the model of the minis.
- `unsupported-model` is also the case where the disk reading names a generation that has no model in `models.toml`.
- A folder picked by hand (section 6.4) has no USB id and no disk. Its model comes from the info file or the user, and with neither the state is `ambiguous`. A volume with no filesystem name is not `unsupported-filesystem`.
- A volume that vanished during the read counts as no volume.

### 8.4 The exact model

Sections 8.1 to 8.3 decide the **model**: one entry of `models.toml`, which is what the bootloader, the screen size and the install path depend on. Many models cover several iPods that Apple sold: the Classic has three capacities in four versions, the Video two generations. The **variant** is the entry of `[model.variant.*]` (see `models/README.md`) that says which one it is, to show "iPod Classic 120 GB (late 2008)" instead of "iPod Classic". **A variant never changes the model and never enables or blocks an action that writes.** If every clue is missing or wrong, the only loss is a less precise name.

**Order of the clues**, from the closest to the hardware to the weakest. Each clue only looks at the variants of the model decided by 8.2; a clue that points to another model is logged and ignored.

1. **The device's own property sheet** (the SCSI inquiry page of section 13). Not read by the first versions. If it is ever read, it comes first because it is written by the iPod itself, and its key names must be checked on real iPods before use.
2. **`SysInfo`**. Read the model number string (the key name is in section 11, checked as in section 13). It is one letter, the region, then the four characters of the Apple model number, for example a letter and `B562`. The first character is dropped when it is a letter, the rest is compared in upper case with the `apple_models` of the variants. One match gives the exact variant. An empty file, a missing key, or a number that no variant has: go on to the next clue.
3. **The capacity of the disk.** The size of the whole disk (not of the music volume) in bytes, as the OS reports it, divided by 10^9. A variant matches when the result is between `CAPACITY_LOW` and `CAPACITY_HIGH` times its `capacity_gb`: the drive is a little bigger than the box says, a flash chip a little smaller. The size is never computed as sectors times 512: the disk of a 120 GB Classic has sectors of 4096 bytes (measured, section 13), and a count in 512-byte sectors would be eight times too small. The sector size is logged and not used to decide, because one measurement is not enough.
4. **The USB id.** It gives candidate models (section 3.2) and nothing about the variant. It is last because it has already done its work in 8.2.

**Reading the result of the capacity clue.**

- One variant matches: it is the variant, `variant_source` is `capacity`.
- Several variants match, with the same `name`: the variant stays empty and the name is shown without a release period. This is the case of the Classic 160 GB (2007 thick, 2009 slim) and only that one so far. Showing "iPod Classic 160 GB" is right whatever the year is.
- Several variants match with different names: no variant, the model name and the capacity are shown. This is the case of a 10 GB disk in the model of the first two generations, and of a 30 GB disk in the Video (5th or 5.5th generation).
- None matches (a storage that was swapped, which is common on these iPods): no variant. The model name and the real size are shown.

**Rules when clues disagree.**

1. A clue higher in the list wins. Lower clues are still computed and logged.
2. If `SysInfo` gives a variant and the capacity clue matches none of its window, the variant is kept (it is the iPod as Apple sold it) and the detected iPod gets the flag `storage-changed`. The message shows the real size.
3. The capacity never decides a **model**. A 6 GB mini can only be a 2nd generation if the disk is the original one, but a user may have put a bigger storage in a 1st generation. The question of section 8.2 rule 2 stays, and the app may add "a 6 GB disk is usually the 2nd generation" as a hint in the question.
4. A variant is shown only if `variant_source` is not none. The words `variant` and `variant_source` are for the logs, like `model_source`.

**What is shown.** If the variant is known: its `name`, then its `released` in brackets. If only a set of variants with one common name is left: that name alone. Else: the model name and the size, for example "iPod Classic (6th generation), 256 GB". The size shown is the real one of the disk, in GB.

**Where the facts come from.** `models.toml` only. The app has no other table of model numbers. Every variant says in `to_verify` what is still weak (release periods have no source yet), and the code must not treat a weak fact as better than a missing one: it shows it, because it is only a name.

**Order in which the code can use it.** The capacity clue needs nothing but the disk size of section 5, so it works in the first version. The `SysInfo` clue needs the volume (section 7). The device sheet needs the helper and comes later.

## 9. Staying live

The aim of the app is that plugging, unplugging or entering DFU changes the screen in less than a second (the aim of v0.1 in the roadmap). The USB layer must therefore answer by events, not by a button that the user presses.

### 9.1 Sources of events

| System | USB devices | Disks and volumes |
|---|---|---|
| Linux | The udev monitor (kernel events on the `usb` and `block` subsystems) | The same monitor, plus a watch on `/proc/self/mountinfo` for mounts and unmounts |
| macOS | IOKit notifications for a USB device added or removed | DiskArbitration callbacks for disk appeared, disk disappeared, disk description changed (this one carries the mount) |
| Windows | `WM_DEVICECHANGE` with a device notification registered for USB devices and for volumes | The same message |

Where an event source is not available (a sandbox, a system without it), the app falls back on **polling**: the full reading of sections 3 and 5 every 500 ms for the USB layer, every 1000 ms for disks and volumes. The reading of the USB list is cheap (it opens nothing), so polling at that rate is acceptable. Both ways share the same code: an event only asks for the next reading to be done now.

An event is never trusted as data. It is a hint to read the list again; the list is what counts. Next to the event sources, a slow **backup reading** runs every 2 seconds (constant, `POLL_BACKUP`): a source can open and still hear nothing (the udev monitor in a sandbox with its own network, seen on a real system), and with the backup a lost event only delays the change. It sends the same hints as an event.

### 9.2 Plug in

The steps, as they are expected to happen on a real iPod (times are indicative, to be measured, section 13):

1. The USB device appears. **At once** (within the second), the app adds a detected iPod with `state = waiting` and the USB fields. The screen changes here.
2. The disk appears one to a few seconds later. The model reading starts.
3. The volume is mounted a bit later still. The marker files are read. The state becomes `ready` or one of the others.
4. If nothing more appears for 10 seconds after step 1 (constant, `DISK_WAIT`), the state becomes `no-disk`. A hard disk iPod can need more time to spin up: a disk or volume that appears after `no-disk` simply replaces the state, with no message of failure.

Each step updates the same detected iPod, found by its port path. The screen shows what it knows so far; it does not wait for the end to show that something is there.

### 9.3 Unplug

When the USB device disappears, its detected iPod is removed from the list **at once**. Everything tied to it is dropped: the disk, the volume, the reads in progress. A read in progress ends with an error from the OS (the volume is gone); this is handled as the end of the iPod and shown as nothing. The user does not see an error for unplugging.

If an operation that writes was running (a helper job), what happens is described in the specs of that operation (`bootloader-legacy.md` section 19, `bootloader-classic.md` section 16). Detection only tells the app that the iPod left.

### 9.4 A reset on the same port

Entering DFU, and the restart after an install, make the iPod disappear and appear again with another id or the same one. To avoid showing a "disconnected" screen in the middle of a flow that the user is following:

1. When a USB device disappears, the app does not drop its detected iPod from the screen immediately **if an operation of the app is running on it** or if its state was `dfu` or the app asked the user to hold buttons. For a plain unplug with nothing running, it does drop at once (section 9.3).
2. If a device with the same `port_path` appears within 3 seconds (constant, `SAME_PORT_WINDOW`), it is the same iPod in another mode: the mode and `usb_id` are updated, nothing else is lost.
3. If it does not, the iPod is dropped.
4. For the identity across modes, `port_path` is the key and `serial` is only a second check, because the serial string may differ in DFU mode (section 13). The 60 seconds memory of section 8.2 rule 5 uses the same key.

The three seconds fit a reset of the USB device. A clean restart of the iPod takes longer, so an operation that ends in a restart gives its own window with the hold (`device-state.md` section 2.6).

The flow "the user holds the buttons until DFU shows up" depends on the app seeing `05ac:1223` with no delay: this id must reach the screen through the same one second path as a plain plug in.

### 9.5 Refresh

The user can press a "Refresh" button at any time. It does one full reading of every layer and replaces the list. It is a way out for a lost event, not the normal way.

### 9.6 What the screen shows

The screen shows more than the state of this spec: an operation that runs, an iPod that was ejected, an iPod that is restarting. How these combine with the states of section 8.3, which operations exist, and what goes into the log, is in `device-state.md`.

## 10. Messages

The text the user sees for every situation. Short, in the plain words of the app, translatable (the app is translated, see `translations/`), and no name of a file that the user does not know.

| Situation | Message |
|---|---|
| `waiting` | Reading the iPod... |
| `no-disk` | The iPod is connected but does not show its storage. Turn on disk use in the software of the iPod, or put the iPod in Disk Mode. |
| `no-volume`, not mounted | The iPod storage is there but is not open on this computer. Open it from the file manager, then press Refresh. |
| `mac-format` | This iPod was formatted for a Mac. TacetOS needs the Windows format: the iPod can be converted (this erases the music on it). If the system offers to format the disk, answer No. |
| `unsupported-filesystem` | The iPod uses a filesystem TacetOS cannot read. |
| `ambiguous` | Several iPod models share this connection. Which one is yours? |
| `conflict` | The iPod and its files do not agree on the model. TacetOS will not change anything. |
| `permission` | TacetOS may not read the disk of the iPod. (The line for the system follows: a group on Linux, an administrator prompt on Windows, the disk access setting on macOS.) |
| `unsupported-model` | This iPod is not supported. |
| `dfu` | The iPod is in DFU mode. (The next step from `bootloader-classic.md`.) |
| `ready` | The model name, the size, and "Windows" or "Mac" format |

The firmware line (section 7.6) is a separate line, whatever the state:

| Situation | Message |
|---|---|
| `tacetos` | TacetOS 0.3 |
| `custom` | Custom firmware 4.0 |
| `apple` | Apple firmware 1.1.6, or Apple firmware when the version is not known |
| `unknown` | Firmware unknown |

## 11. Constants at a glance

Kept in the one file of constants, with the same names. The times are starting values, to be tuned on a real iPod.

| Name | Value | Section |
|---|---|---|
| `USB_VENDOR_APPLE` | `05ac` | 3.1 |
| `DISK_WAIT` | 10 s | 9.2 |
| `SAME_PORT_WINDOW` | 3 s | 9.4 |
| `MODE_MEMORY` | 60 s | 8.2 |
| `POLL_USB` | 500 ms | 9.1 |
| `POLL_DISKS` | 1000 ms | 9.1 |
| `POLL_BACKUP` | 2000 ms | 9.1 |
| `MARKER_MAX_BYTES` | 64 KiB | 7.1 |
| Music volume | slot 2 first, when several are mounted | 6.1 |
| Apple's root folder | `iPod_Control` | 7.2 |
| Apple's model file | `iPod_Control/Device/SysInfo` | 7.2 |
| Apple's database | `iPod_Control/iTunes/iTunesDB` | 7.2 |
| Firmware folder, info file | in the constants file | 7.3 |
| Info file keys read | `Target`, `Target id`, `Version`, `Binary` | 7.3 |
| Distribution key of the info file | to be named in v0.3 | 7.3 |
| `CAPACITY_LOW` | 0.90 | 8.4 |
| `CAPACITY_HIGH` | 1.05 | 8.4 |
| Key of the model number in `SysInfo` | `ModelNumStr` (one source so far, section 13) | 8.4 |

## 12. What the tests need

Everything in sections 4, 6.1, 6.2, 7 and 8 is tested with recorded data and no iPod. Real hardware only confirms the readers.

### 12.1 USB readers (sections 3 and 4)

- One recorded listing per system, with an iPod in normal mode, in DFU, and with no iPod, as a folder tree of sysfs files for Linux, a text dump of the registry for macOS, and a list of hardware id strings (with and without `&REV_`, upper and lower case) for Windows.
- A listing with two iPods of the same model, and one with a phone from the same vendor.
- A listing where one device is listed twice (section 4 rule 3).

### 12.2 Disks and volumes (sections 5 and 6)

- A fake `mountinfo` with a FAT32 volume, an HFS+ volume, a mount point with spaces and a backslash (escaped), and a source named by another link.
- A sysfs tree where the disk is under the USB device, and one where it is not (an internal disk).
- A Windows case where a volume has two extents, and a case with no volume at all (Mac iPod).

### 12.3 Marker files (section 7)

- A volume folder with the Apple folder in capitals, a hidden firmware folder and an info file with `\r\n` line ends.
- An info file taken from a real firmware build (kept in the fixtures, not retyped), one with only some keys, one with an unknown target, one with a zero byte in it, one of 1 MiB.
- A volume with `iPod_Control` and nothing else, and an empty volume.
- A volume where the folder vanishes during the read (the unplug case).

### 12.4 Merging (section 8)

A table of cases: a list of evidence in, the detected iPod out. At least: each state of section 8.3; the minis with and without a disk reading; a disk reading outside the candidates (`conflict`); an info file of another model (`other-model`); DFU after normal on the same port, and DFU alone; two iPods at once.

A second table for section 8.4, on the real `models.toml`: a model number with and without the region letter, in upper and lower case, gives the right variant; a number of another model is ignored; an empty `SysInfo`; a disk of each marketed size of each model, including a disk with sectors of 4096 bytes; the Classic 160 GB gives the common name and no year; a swapped storage; `SysInfo` and capacity that disagree (`storage-changed`); the 6 GB mini never changes the model; a disk of 0 bytes or no size.

### 12.5 Live behaviour (section 9)

Driven by a fake clock and a scripted event stream, with no sleeping: plug (USB first, disk 1 s later, volume 2 s later), plug with no disk (the state turns `no-disk` at 10 s and a later disk replaces it), unplug at each step, DFU reset on the same port within and after 3 seconds, a lost event recovered by the next poll. The check is on **when** the list changes, which must be within one second of the USB event.

## 13. Not decided yet

- Whether the iPods in normal mode really show no disk when "disk use" is off, on each model, and what the OS then does (section 3.3). A real Classic is available for this test.
- How long a hard disk iPod really takes between the USB event and the mounted volume, on each system. `DISK_WAIT` and the polling rates are guesses until measured.
- Whether the USB serial string of an iPod is the same in normal mode, in Disk Mode and in DFU mode (section 9.4). The app uses the port path only until it is known.
- The content of `iPod_Control/Device/SysInfo`. The key `ModelNumStr` and the leading region letter come from one source only (the model table of libgpod, which skips a first letter of the value), and the file of the real Classic is empty, so nothing was seen on an iPod yet. To be read on one that still runs Apple's firmware, and on the models without USB for the first two generations. The table from the model number to the variants is in `models.toml`, with one source for the numbers and the capacities and none for the release periods.
- The serial number as a clue: the same table of libgpod maps the last three characters of the serial number to a model number. It could work from the USB serial string without any volume. Not used: nobody checked that the USB serial of an iPod is the serial of its label, nor that the list is complete.
- Whether the capacity windows of section 8.4 hold on real iPods of each model, and what a swapped storage looks like. Measured so far: a Classic of 120 GB, sectors of 4096 bytes.
- Whether the disks of hard disk iPods report themselves as removable on each system. The spec does not use that flag for this reason, only the USB ancestor.
- Answered on a real Classic (120 GB, on Linux): the music volume is at slot 1 and the sectors are 4096 bytes (section 6.1). The disk showed 6 seconds after the USB device, so `DISK_WAIT` is 10 seconds. Its `SysInfo` file was empty.
- Whether to let the app ask the OS to mount an iPod volume that is not mounted (section 8.3 `no-volume`). A mount is done by the OS service of the user, not by TacetOS, but on some systems it writes a flag to FAT32 volumes when mounted read-write. Not offered by detection itself. The operations of `device-state.md` section 2 offer it, on the request of the user only.
- The key of the firmware build in the Apple model file (`visibleBuildID`, with a value like `0x01168000 (1.1.6)`, section 7.6) comes from one source (libgpod) and the file of the real Classic is empty. To be read on an iPod that still runs Apple's firmware, with the plain build key next to it.
- The name of the TacetOS line of the info file (section 7.3), and whether the build can write it. Belongs to v0.3, our own firmware build.
- The optional extra evidence of the device itself: older iPods answer a SCSI inquiry for a vendor page that lists further pages, which together hold a small property list with, among other things, the RAM size. It could give the model and the memory without the help of the volume. It needs raw disk access, so it would go through the helper, and it is not used by v0.1. If it is ever used, its key names must be checked on real iPods first.
- On Windows, whether the walk up the device tree always meets the USB device for an iPod, with the storage driver in between (section 5). To be verified with a real iPod on a real Windows system, in the "Windows and macOS" step of the roadmap.
- Eject (asking the OS to release the volume before unplugging) is not detection and is described with the code of each system, in the same step.
