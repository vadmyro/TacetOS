# Bootloader install, older iPods

How TacetOS finds an older iPod, installs, updates and removes its bootloader, and formats the iPod when the user asks for it. "Older" means the models whose install method is `partition` in `docs/models/`: the bootloader lives in a small firmware partition at the start of the disk, next to the music partition. This spec is meant to be enough to write that part of TacetOS. It does not assume that the reader has seen any other installer.

It has four parts:

- **Finding the iPod** (sections 1 to 6): the partition tables, the firmware banner, the model, and the choice among the disks. Read only.
- **Reading the firmware partition** (sections 7 to 11): its layout, the bootloader file, the state of an iPod and the checks before writing. Read only.
- **Install, update, uninstall** (sections 12 to 19): the rules every write follows, the steps in order, and what to do when one fails.
- **Formatting and safety** (sections 20 to 24): formatting the music partition, converting a Mac iPod, confirmations, error cases, and what must never happen.

Section 25 gathers every constant in one table, section 26 lists the test fixtures, and section 27 the points still open, to be settled with a real iPod.

**Order to write the code.** The read-only parts first (sections 2 to 11), tested on the fixtures of 26.1 and 26.2. Then the writes, on disk image files only (sections 12 to 19, fixtures of 26.3). Then formatting and conversion (sections 20 to 22, fixtures of 26.4). Writing to a real iPod comes last, with the beta (section 11 check 8).

The numbers and layouts here are facts about the iPod disk, taken from public documentation and from a study of existing installers, and they are written in our own words. The parts that are our own design (what to accept, what to refuse, what to report) say "must". Where TacetOS does something on purpose that existing installers do not, the text says so, so that nobody "fixes" it back.

## 1. Words used here

- **Disk**: the whole storage of the iPod as the computer sees it in Disk Mode, for example `/dev/sdb`, not one partition of it.
- **Sector**: the unit the disk is read in. The OS reports its size. When it cannot, assume 512 bytes.
- **LBA**: the number of a sector, counted from 0 at the start of the disk.
- **Firmware partition**: the small first partition, holding Apple's firmware and, after install, the TacetOS bootloader next to it.
- **Music partition**: the large second partition, holding the files.
- **Windows iPod**: an iPod formatted by iTunes on Windows. It has a DOS partition table and a FAT32 music partition.
- **Mac iPod**: an iPod formatted by iTunes on a Mac. It has an Apple Partition Map and an HFS+ music partition.
- **Slot**: one of the up to 4 partitions of the common description of section 2.3. Slot 1 is the firmware partition, slot 2 the music partition.
- **Directory**: the list of images at the start of the firmware partition (section 7.2). Each item of the list is an **entry**.
- **Image**: one block of data described by an entry, for example Apple's main firmware, called **OSOS** after its type tag.
- **Image area**: the place the `devOffset` of every entry is counted from (section 7.3).
- **Helper**: the small program with the right to write to disks. The app asks, the helper checks and writes. No other part of TacetOS writes to a disk.
- **Flush**: asking the disk to really store what was written, not only the cache of the OS.
- **Backup**: the copy of the firmware partition made before the first write (section 13, rule 2).
- **Install record**: the small file the app keeps after each operation (section 18).

The code never talks about "winpod" or "macpod" to the user. The app says "formatted for Windows" and "formatted for Mac".

## 2. Two kinds of partition table

The first sector of the disk tells which kind it is. Test in this order:

1. If bytes 510 and 511 are `0x55` and `0xAA`, it is a **DOS partition table** (Windows iPod).
2. Otherwise, if bytes 0 and 1 are the letters `E` and `R`, it is an **Apple Partition Map** (Mac iPod).
3. Otherwise, the disk is not an iPod, or not one we can read. Stop, and report "unknown partition table".

### 2.1 DOS partition table

The table sits in sector 0 and has 4 entries of 16 bytes each, starting at byte `0x1BE` (446). All numbers are little-endian. Only three fields of an entry matter to us:

| Offset in entry | Size | Meaning |
|---|---|---|
| 4 | 1 byte | Partition type |
| 8 | 4 bytes | First sector (LBA) |
| 12 | 4 bytes | Number of sectors |

The other bytes of an entry (boot flag, the old cylinder-head-sector fields) are ignored when reading. On iPods they are zero.

Extended partitions (type `0x05`) are not followed. An iPod never uses them, so seeing one is only a reason to say "this does not look like an iPod".

Two details that surprise people:

- The firmware partition has the type `0x00`, which normally means "empty". On an iPod it is not empty: the type is a way to hide it from the computer, which then never mounts it.
- The music partition has the type `0x0B` (FAT32) on a Windows iPod. Type `0x0C` (FAT32 with LBA) is also found and must be accepted.

### 2.2 Apple Partition Map

Sector 0 holds a short header, starting with `ER`. The only field we use is the size of one map block, a 16-bit big-endian number at byte 2. For a normal 512-byte block it reads `0x0200`. The map entries follow, one per block, each one block long. Entry number 1 is in the block just after the header, so the entry number `n` is at the byte offset `n` times the block size.

Each entry is read like this. All numbers are big-endian:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 2 bytes | The letters `PM`. Anything else means the map has ended |
| 4 | 4 bytes | Number of entries in the whole map |
| 8 | 4 bytes | First block of the partition |
| 12 | 4 bytes | Number of blocks in the partition |
| 16 | 32 bytes | Partition name (not used for the decision) |
| 48 | 32 bytes | Partition type, a text padded with zero bytes |

Read the entries from number 1 up to the count given by entry 1, and also stop at the first block that does not start with `PM`. Of all the entries, keep only two kinds, by their type text:

- `Apple_MDFW`: the **firmware partition**.
- `Apple_HFS`: the **music partition** of a Mac iPod.

All other entries are skipped: the map itself (`Apple_partition_map`), driver partitions and free space are in the list too. The kept entries are numbered in the order they appear in the map, which gives the same two slots as a DOS table: slot 1 is the firmware partition, slot 2 is the music partition.

The type text must be compared on its first characters, up to the zero padding, not on the 32 bytes at once with trailing garbage. A type like `Apple_HFSX` must not be taken for `Apple_HFS`.

Safety rules that the existing tools do not have, and the code must:

- Keep at most 4 entries. A map with many partitions must not write past the slot list.
- A block count in the header that is zero, or larger than 64, is an error, not a reason to loop.
- Never read past the end of the disk. Check each entry's byte offset against the disk size first.

### 2.3 One common description

Both kinds are turned into the same small list of up to 4 slots, so everything after this point does not care which kind it came from:

| Field | Meaning |
|---|---|
| Type | The DOS type number. For a Mac iPod: `0` for the firmware partition and a special value for HFS, chosen outside the range of a one-byte type (the value `0xFFFF` works) |
| First sector | Start, converted to sectors |
| Sector count | Size, converted to sectors |

The start of the firmware partition in bytes is slot 1's first sector multiplied by the sector size. This number is used by every later step, because the firmware banner (section 4) and the image directory are found from it.

An Apple Partition Map counts in map blocks, which are 512 bytes on every iPod seen so far, so blocks and sectors are the same. If the map block size ever differs from the sector size, convert through bytes instead of copying the number. This case has not been seen on a real iPod and is covered by a test with a made-up map only.

## 3. Does this disk look like an iPod

A disk passes the **partition test** when all of this is true:

1. Slot 1 has the type `0`, and a sector count above zero.
2. Slot 2 has the type `0x0B` or `0x0C` (Windows iPod), or the HFS value (Mac iPod).
3. For a scan, slot 1's first sector is not 0. Sector 0 holds the partition table itself, so a firmware partition cannot start there.

This test alone is not enough. A USB stick with a similar layout would pass it. The disk must also pass the **firmware test** of section 4, and only then is the model asked for.

The test decides what the disk is. It never decides that something is safe to write. Writing needs the checks of section 11 too.

## 4. Firmware partition check

The first sector of the firmware partition must carry Apple's banner. It is a block of exactly 256 bytes: 16 rows of 16 characters, except that the last row has 15 characters and is followed by a zero byte. Here are the 16 rows, one per line. The two `|` at the ends of each line only mark the row and are not part of the text; spaces count.

```
|{{~~  /-----\   |
|{{~~ /       \  |
|{{~~|         | |
|{{~~| S T O P | |
|{{~~|         | |
|{{~~ \       /  |
|{{~~  \-----/   |
|Copyright(C) 200|
|1 Apple Computer|
|, Inc.----------|
|----------------|
|----------------|
|----------------|
|----------------|
|----------------|
|---------------|
```

In rows 1 to 7 the stop sign is drawn with the characters `{` `~` `/` `-` `\` `|` and spaces; the backslash is a single character. Rows 8 to 10 read `Copyright(C) 2001 Apple Computer, Inc.` across the row boundaries, then dashes up to the end of row 10. The banner is 255 visible characters followed by the zero byte.

The test compares these 256 bytes with a constant stored in our code. The rows above were checked by the test of the reference partition tool (P4.1c), which accepts them. The copy from a real older iPod is still to do (section 27, first point: a real Classic has no firmware partition, seen on 2026-11-11). If the banner does not match, the disk is not an iPod we handle: stop, and report "firmware partition does not carry the expected banner". Do not try to repair it.

## 5. Model of the iPod

The model is not read from the USB id, because several generations share one. It is read from the firmware itself: the firmware directory of the partition (section 7) holds an entry for the main firmware, and the version stored there tells the generation. Only the upper part of the version matters: shift the number right by 8 bits, then look it up:

| Value | Generation | Target name in TacetOS |
|---|---|---|
| `0x01` | 1st or 2nd generation | `ipod1g2g` |
| `0x02` | 3rd generation | `ipod3g` |
| `0x40` | 1st generation mini | `ipodmini1g` |
| `0x50` | 4th generation (grayscale) | `ipod4g` |
| `0x60` | Photo or Color | `ipodcolor` |
| `0x70` | 2nd generation mini | `ipodmini2g` |
| `0xB0` | Video (5th generation) | `ipodvideo` |
| `0xC0` | 1st generation nano | `ipodnano1g` |
| `0x100` | 2nd generation nano | `ipodnano2g` |

Any other value means "unknown generation": the disk looks like an iPod but is not one we can install on, and the install must refuse. It must not guess the nearest model.

The target names are the same as the `Bootloader target` row of the files in `docs/models/`, so the table above must stay in step with them. `models/models.toml` is the source of truth for the targets, and this table is only the check.

Three generations in this table (the 1st nano, the 2nd nano and the Video) have a model file in `docs/models/` today. The others are listed because the disk reading is the same for all of them, not because TacetOS supports them.

## 6. Finding the iPod among the disks

The app does not look at every disk. The steps, in order:

1. The helper lists the disks that are plugged in over USB and removable. The internal disks of the computer are never opened.
2. For each candidate disk, open it **read-only** and read the sector size from the OS (default 512 if the OS cannot say).
3. Read sector 0 and run the partition table test (sections 2 and 3). If it fails, skip the disk without any message to the user: most disks are not iPods.
4. Read the first sector of the firmware partition and run the banner check (section 4). If it fails, skip.
5. Read the model (section 5). If it is unknown, report it as "an iPod we do not support" and do not offer to install.
6. If exactly one disk passes, it is the iPod. If none passes, say "no iPod found" with the usual causes (not in Disk Mode, no permission to read the disk). If more than one passes, the user must pick one by name and capacity; the app never picks for them.

Permission problems are counted apart from "not an iPod". If every candidate was refused for permission and none was recognised, the message is "permission denied", not "no iPod found".

All reads in this part are read-only. Nothing in this part may write to a disk. A dev build reads a real iPod the same way, because reading is allowed before the beta; only the writes of sections 12 to 22 are refused there.

## 7. Layout of the firmware partition

All offsets in this section are in bytes from the start of the firmware partition, unless said otherwise. All numbers are little-endian. The partition holds a small header, a directory of images, and the images themselves.

### 7.1 Volume header

The first sector holds the banner of section 4 in its first 256 bytes, then:

| Offset | Size | Meaning |
|---|---|---|
| `0x100` | 4 bytes | The characters `]ih[`. Anything else means "bad firmware directory" |
| `0x104` | 4 bytes | Where the directory is, counted from offset `0x200`. The directory starts at this value plus `0x200` |
| `0x10A` | 2 bytes | Format version of the partition: `2` for the 3rd generation, `3` for the later ones |

The bytes in between (`0x108`, a pointer to an extended header) are not used. The directory is not always aligned on a sector. Keep the remainder of its offset divided by the sector size, because the directory is read and written as one whole sector and the entries start at that remainder inside it.

A version other than 2 or 3 is not an error for reading, so the app can tell the user what it saw, but it must stop any write (section 11, check 2).

### 7.2 Directory

The directory is a list of entries of 40 bytes each, one after the other, with no count stored anywhere. The list ends at the first entry whose first 4 bytes are not `!ATA` (some players write `DNAN`; accept both). At most 10 entries are read, and none may start more than 400 bytes after the start of the directory.

One special case: on the 2nd generation nano the first byte at the computed position is zero. The real directory then starts at the beginning of the next sector. This is the only way to tell that layout, so the code must apply it only when the first byte is exactly zero.

Fields of one entry:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 bytes | Device tag, `!ATA` (or `DNAN`) |
| 4 | 4 bytes | Image type, a 4-letter tag stored backwards (see below) |
| 8 | 4 bytes | Image number, usually 0 |
| 12 | 4 bytes | `devOffset`: where the image starts, counted from the **image area** (section 7.3) |
| 16 | 4 bytes | `len`: length of the image in bytes, without padding |
| 20 | 4 bytes | Address in memory where the iPod loads the image |
| 24 | 4 bytes | `entryOffset`: where execution starts inside the image. `0` means the start of the image |
| 28 | 4 bytes | `checksum`: the sum of all bytes of the image, as unsigned 8-bit values, modulo 2^32 |
| 32 | 4 bytes | Version of the image |
| 36 | 4 bytes | Address used for images run from flash, `0xFFFFFFFF` when unused |

The image type tags are stored backwards, so the letters in the file read the other way round:

| In the file | Reads as | Meaning |
|---|---|---|
| `soso` | OSOS | The main firmware. Exactly one is expected |
| `crsr` | RSRC | Resources (fonts, bitmaps), present on the Video |
| `dpua` | AUPD | Firmware updater |
| `kbso` | OSBK | A backup of the main firmware (used on the 2nd generation nano, see 7.5) |
| `ebih` | HIBE | Hibernation image |

An unknown tag does not stop the reading: the entry is kept as "other", never modified, and still counts for the overlap check of section 11. The model of section 5 comes from the version field of the OSOS entry.

### 7.3 Where the images are

The **image area** starts one sector after the start of the partition, except in one case: a 3rd generation partition (format version 2) with more than one image in its directory has the image area at the very start of the partition. This rule is taken from how existing tools behave and has not been checked against a dump of a 3rd generation iPod, see section 27.

The bytes of an image are at: start of partition, plus the image area start, plus `devOffset`, for `len` bytes. Images are padded with zero bytes up to a whole sector. On a normal iPod the OSOS image comes first, and the others follow it with their `devOffset` growing.

A 3rd generation OSOS entry may carry a version of 0. Another tool invents the value 3 in memory in that case, which maps to no model in section 5. TacetOS must not copy this: a 3rd generation iPod whose version field gives no model is reported as an unknown generation until a real dump shows what the field holds.

### 7.4 Where the bootloader sits (all models except the 2nd generation nano)

The bootloader is not a new image. It is appended inside the OSOS image, after Apple's firmware, and the entry's `entryOffset` tells the iPod where to start running:

- Without the bootloader: `entryOffset` is `0`, `len` is the size of Apple's firmware.
- With the bootloader: Apple's firmware is padded with zeros up to a whole sector, the bootloader is stored right after it, `entryOffset` is the position of the bootloader (a multiple of the sector size), `len` is `entryOffset` plus the bootloader size, `checksum` covers all `len` bytes, and the flash address field is set to `0xFFFFFFFF`.

So the test "this iPod has the bootloader" is `entryOffset` different from 0 in the OSOS entry. Apple's own firmware is still there, in front. The bootloader starts it when the user asks for it (the double boot of the project rules).

When the bootloader is already there, an update keeps the same `entryOffset`. It never moves it.

### 7.5 The 2nd generation nano

This model is different: its firmware is encrypted and checked by the hardware, so the bootloader cannot be appended.

- The OSOS entry is renamed to OSBK (a backup of Apple's firmware) and the TacetOS bootloader is written as a new OSOS image after the last image.
- "This iPod has the bootloader" means an OSBK entry exists.
- The sector before the firmware holds hashes that the iPod needs. A bootloader file for this model starts with those `0x800` bytes (2048), and the data is written `0x800` bytes before `devOffset`. The `len` field does not count them.
- The `checksum` field is computed over the unencrypted data and the iPod does not check it. It is still written, to keep the directory coherent.

## 8. The bootloader file

The build gives one file per model, with an 8-byte header followed by the program:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 bytes | Checksum, **big-endian** (the only big-endian number of this part) |
| 4 | 4 bytes | Model tag: 4 ASCII characters |
| 8 | the rest | The program |

The checksum is the model number of the table below plus the sum of all bytes of the program, as unsigned 8-bit values, modulo 2^32. The header itself is not counted. A tag and a number per model make it impossible to check a file made for another model, even if the checksum alone would pass.

| Target | Model tag | Model number |
|---|---|---|
| `ipod1g2g` | `1g2g` | 19 |
| `ipod3g` | `ip3g` | 7 |
| `ipodmini1g` | `mini` | 9 |
| `ipod4g` | `ip4g` | 8 |
| `ipodcolor` | `ipco` | 3 |
| `ipodmini2g` | `mn2g` | 11 |
| `ipodnano1g` | `nano` | 4 |
| `ipodvideo` | `ipvd` | 5 |
| `ipodnano2g` | see below | 62 |

Both columns were read in two places (the tool that makes the files and the tool that checks them) and they agree, except for one thing: the 2nd generation nano is tagged `nn2g` by the one that makes the files and `nn2x` by the one that checks them. The existing installer avoids the problem by carrying its own copy of the bootloader for this model. TacetOS must decide this with a real build output, see section 27.

These numbers are separate from the model values of section 5. Section 5 identifies the iPod from its firmware, and this table identifies the file. A check passes only when both give the same target.

## 9. Sizes and alignment

- The sector size is a power of two. The padding arithmetic of the whole spec (round up to a whole sector) depends on it, so a sector size that is not a power of two stops everything.
- The data TacetOS holds in memory for one write (the firmware before the bootloader, the bootloader, and the padding) must fit in 8 MiB. This is enough for the Apple firmware of the models in section 5. The resources image of the 2nd generation nano is larger, and TacetOS never loads it. If the data does not fit, the install stops.
- The padded size of an image is its length rounded up to a whole sector.
- The `entryOffset` of a bootloader already installed is a multiple of the sector size, lower than `len`, and not lower than the length of Apple's firmware. Anything else is a broken install.

## 10. Reading the state of an iPod

After the recognition of section 6, and still read-only, the code builds one description of the iPod: the list of directory entries, the index of the OSOS entry, the start of the image area, and the state, which is one of:

- **Clean**: no bootloader.
- **Installed**: bootloader present (section 7.4 or 7.5).
- **Inconsistent**: the entries contradict section 9 or section 11. No write is allowed, and the message says what contradicts what.

The state is shown to the user before the first write, and it decides what the app offers (section 12): install on a clean iPod, update or remove on an installed one, nothing on an inconsistent one.

## 11. Checks before writing

Nothing is written until every check below passes. They run in this order, after the iPod has been recognised (sections 3 to 5), and again just before the first write. If one fails, nothing has been changed, the message names the check, and the install does not continue.

1. **Same disk as at the scan.** Read sector 0 and the first sector of the firmware partition again, and compare them with what the scan saw. If anything differs, or the disk is gone, refuse. This catches an iPod unplugged and another disk plugged in on the same name.
2. **The structure is valid.** The format version is 2 or 3. The directory lies fully inside the firmware partition. All its entries, plus one more for an install on the 2nd generation nano, lie in a single sector, so that every directory change is one sector write (section 13, rule 4); no iPod seen so far breaks this. The list holds an OSOS entry, exactly one, and at most one OSBK entry. On all models except the 2nd generation nano, the OSOS entry is the first one of the list and there is no OSBK entry (the existing tools assume it, and TacetOS refuses otherwise until a dump shows another order). On the 2nd generation nano, a clean iPod has OSOS first, and an installed one has OSBK first and OSOS last.
3. **The entries make sense.** For every entry, `devOffset` plus its padded size ends inside the firmware partition. No two entries overlap, and they are in increasing `devOffset` order. For the OSOS entry, the checks of section 9 on `entryOffset` also hold.
4. **The file is for this iPod.** The bootloader file has at least the 8 bytes of the header and at least one byte of program. Its model tag is the one of the table in section 8 for the recognised target. Its checksum matches. A file that fails the checksum is never written, however small the difference.
5. **There is room.** Compute the position where the end of the new data will fall: in the OSOS image for most models, after the last image for the 2nd generation nano. It has to be inside the firmware partition, and it must not run into the next image. If the next image is in the way, the following images are moved as section 14.1 describes, and the same check must then pass for their new positions. On the 2nd generation nano nothing is moved: the new data goes after the last image. Where the move would leave the partition, the install stops with "not enough room in the firmware partition". The existing tools do not check the size of the partition; TacetOS must.
6. **A backup exists.** Before the first write, the helper copies to a file, in the data folder of the app, the whole firmware partition (section 13, rule 2). It reads the copy back and compares it. The file name holds the model and the date. If the copy cannot be made or does not compare equal, nothing is written. Section 19 says how this file is used after a failure.
7. **The firmware checksum.** Compute the checksum of Apple's firmware as it is on the iPod (the bytes from the start of the OSOS image up to `entryOffset`, or up to `len` when there is no bootloader), and compare it with the directory. A difference is a warning that goes into the log and the install record, not a refusal, because the way Apple computes it has not been checked on a real dump. See section 27. On the 2nd generation nano the check is skipped: the sum in the directory is the one of the data before encryption (section 7.5), which cannot match the bytes on the disk.
8. **The right gate.** Only the helper writes to a disk. A development build refuses to write to a real iPod before the beta, so the checks above can run on a dev build, but the write after them is refused there. A disk image file (the test fixtures) may be written by any build.

A check never "fixes" anything by itself. It either passes, or it stops with a message. Nothing in the checks may guess: where the data has a shape this spec does not describe, it is a refusal.

## 12. The three operations

The third part describes what the helper does once the state of section 10 is known and the user has said yes. There are three operations, and each one is allowed only from one state:

| Operation | Allowed from | Ends in |
|---|---|---|
| **Install** | Clean | Installed |
| **Update** (a newer bootloader on an iPod that has one) | Installed | Installed |
| **Uninstall** | Installed | Clean |

Nothing is ever allowed from the Inconsistent state. The app also refuses to install on a Mac iPod: the TacetOS firmware cannot read an HFS+ music partition, so a bootloader on such an iPod would start a firmware that finds no files. The app offers the conversion of section 21 instead, and installs only after it.

Each operation is written for two layouts: the one of all models except the 2nd generation nano (the bootloader is appended inside the OSOS image, section 7.4), and the one of the 2nd generation nano (the bootloader is an image of its own, section 7.5).

## 13. Rules for every write

These rules come from our own design. The existing tools follow none of them except the reading of the file before writing, and they are the reason the steps of sections 14 to 17 are ordered the way they are.

1. **Recognise again, check again.** Every operation starts with sections 6 and 10 read again on the disk the user chose, then the checks of section 11. The state read now must be the one shown to the user. If it changed, stop.
2. **Back up first.** The backup of section 11 check 6 is a copy of the whole firmware partition (tens of MiB on these models, small enough to copy every time). It is read back and compared before the first write. The install record (section 18) points to it.
3. **The directory never points at bytes that are not on disk yet.** Image data is written first, then flushed to the disk (the OS cache is not enough: the helper asks the disk to write it through), then read back and compared, and only then is the directory sector written. At every moment between two writes, the firmware partition is either in its old state or in a new state that is coherent on its own.
4. **One sector per directory change.** The directory sector is read, changed in memory, written whole, flushed and read back. A change that needs two directory writes is split into two coherent steps, never one change spread over two writes.
5. **Write only what changes.** Apple's firmware is never written again. The existing tools write it back with the bootloader each time; TacetOS writes from `entryOffset` on.
6. **Zero padding.** Every gap between the end of written data and the next whole sector is filled with zero bytes, built in memory. Nothing left over in a buffer from a previous read may reach the disk.
7. **No cancel in the middle.** Once the first write has started, the cancel button is disabled until the operation ends or fails. The app says before the start "keep the iPod plugged in" and how long the operation takes.
8. **Verify at the end.** Section 18.

### 13.1 Opening the disk

The helper opens the whole disk, not a partition, once for reading during the recognition, and again for reading and writing only after the user has confirmed. What differs between systems:

| System | Disk name | Notes |
|---|---|---|
| Linux | `/dev/sdX` | Opened through udisks2, which asks for the user's permission, so the app never runs as root. The music partition can stay mounted during an install, update or uninstall, because they write only to the firmware partition, which is never mounted. |
| Windows | `\\.\PhysicalDriveN` | Every read and write must be whole sectors, at an offset that is a multiple of the sector size, from a buffer aligned on the sector size. Opening for writing needs the rights of an administrator, asked for by the helper only. |
| macOS | `/dev/rdiskN` (the raw form of the disk) | The system refuses to write to a disk that has a mounted volume. The helper unmounts the whole disk before writing, and the app waits for it to be mounted again at the end (section 18). |

On every system, reads and writes are done in whole sectors, even where the system would accept less. A partial sector is read whole, changed in memory, and written whole. This keeps the code the same on all three systems.

## 14. Install and update, appended bootloader

This covers every model of section 5 except the 2nd generation nano. In this section, "the OSOS entry" is the first entry of the directory (section 11 check 2), and positions inside the OSOS image are counted from its first byte, which is at the start of the image area plus its `devOffset` (section 7.3).

**Before any write**, in memory only:

1. Read the bootloader file, check it (section 11 check 4) and keep the program, without its 8-byte header. Call its size `blLen`.
2. Choose `entryOffset`. On a clean iPod, it is the `len` of the OSOS entry rounded up to a whole sector. On an update, it is the `entryOffset` already in the directory, which never moves (section 7.4).
3. Compute the padded size of the bootloader: `blLen` rounded up to a whole sector. The end of the new OSOS data is `entryOffset` plus that padded size. All of it must fit in the 8 MiB of section 9.
4. Read Apple's firmware from the iPod: the first `entryOffset` bytes of the OSOS image. These bytes include the padding between the end of Apple's firmware and `entryOffset`, exactly as it is on the disk.
5. Compute the new checksum: the byte sum of section 7.2 over the `entryOffset` bytes of step 4 followed by the `blLen` bytes of the program. The padding after the program is not counted.
6. Check the room (section 11 check 5). If the end of the new OSOS data would run into the next image, compute how far the following images must move (section 14.1). If they cannot move, stop here.

**The writes**, each one followed by a flush and a read-back (section 13):

7. **Only for an update:** first take the iPod back to a coherent clean state by writing the directory sector with the OSOS entry changed as in the uninstall (section 16, steps 2 and 3). From now on an update is a fresh install on top of a known clean iPod, with the `entryOffset` of step 2.
8. **Only when images must move:** move them (section 14.1), then write the directory sector with their new `devOffset` values and nothing else changed. The iPod is still clean and still starts Apple's firmware.
9. Write the program followed by its zero padding at position `entryOffset` of the OSOS image. The directory still says `entryOffset` 0 and the old `len`, so the iPod ignores these bytes for now.
10. Write the directory sector with four fields of the OSOS entry changed: `len` becomes `entryOffset` plus `blLen`, `entryOffset` gets the value of step 2, `checksum` gets the value of step 5, and the flash address field becomes `0xFFFFFFFF`. All other entries and all other bytes of the sector are left as they were read.

If any step fails, see section 19. After step 10, verify (section 18).

### 14.1 Moving the following images

Only needed when the new OSOS data would end past the `devOffset` of the second image. The existing tools move the images without any size check of the partition, and with one sector more than needed. TacetOS:

- Computes the distance to move, `delta`: how far the end of the new OSOS data goes past the start of the second image, rounded up to a whole sector.
- Moves all images after the OSOS one, together, as one block: from the start of the second image to the end of the last image (its `devOffset` plus its padded size).
- Refuses if the end of the block plus `delta` would pass the end of the firmware partition. Nothing has been written at that point, apart from the directory of step 7 on an update, which leaves the iPod clean.
- Copies the block in pieces of at most 8 MiB, **starting from the end** and going towards the start. The new place overlaps the old one, and copying from the end is the only order that never overwrites bytes not yet copied.
- After the copy, flushes and compares the whole block at its new place with the backup. Only then is the directory written with each moved `devOffset` increased by `delta`.

An interruption during the copy leaves the images half moved while the directory still points to their old place. This is the only step after which the iPod cannot be trusted without the backup (section 19).

## 15. Install and update, 2nd generation nano

Here the bootloader is a new OSOS image, and Apple's firmware is kept as an OSBK entry. The sector size of this model is 2048 bytes (`0x800`), which is also the size of the hash block that comes before the program (section 7.5).

**Before any write:**

1. Read and check the bootloader file (section 11 check 4). The program includes the `0x800` bytes of the hash block (section 27 has the open question on this). Call its size `blLen`.
2. Find the place of the new image. On a clean iPod: after the last image of the directory, that is its `devOffset` plus its `len`, rounded up to a whole sector, plus `0x800` for the hash block. This gives the `devOffset` of the new entry. The data is written `0x800` bytes earlier. On an update, the place is the `devOffset` of the current OSOS entry, which is our own bootloader.
3. Check that the directory has room for one more entry: fewer than 10 entries, and the new entry still inside the 400 bytes of section 7.2 and inside the directory sector. The existing tools do not check it.
4. Check that the end of the data (place of step 2 minus `0x800`, plus `blLen` rounded up to a sector) is inside the firmware partition.
5. Compute the checksum: the byte sum over all `blLen` bytes. The iPod does not check it (section 7.5), it is written to keep the directory coherent.

**The writes:**

6. **Only for an update:** write the directory sector as after an uninstall (section 17): the current OSOS entry removed, OSBK renamed back to OSOS. The iPod is clean again and starts Apple's firmware.
7. Write the `blLen` bytes and their zero padding at the place of step 2 minus `0x800`. No entry points there yet.
8. Write the directory sector with two changes at once: the OSOS entry is renamed OSBK, and a new entry is added right after the last one. The new entry starts as a copy of the OSBK entry, with the type tag `soso`, the `devOffset` of step 2, `len` equal to `blLen` minus `0x800`, and the checksum of step 5.

The existing tools rename first, in one directory write, and add the new entry in a second one. In between, the iPod has an OSBK entry and no OSOS entry, and may not start at all. TacetOS must make it one directory write (section 13, rule 4).

## 16. Uninstall, appended bootloader

Removing the bootloader is a directory change only. The bootloader's bytes stay in the firmware partition after Apple's firmware, where nothing points at them any more, and the moved images stay where they are.

1. Recognise and check as in section 13, rule 1. The state must be Installed.
2. If an install record (section 18) exists for this iPod, its saved copy of the OSOS entry before install is the target, but only if Apple's firmware on the disk still gives the checksum stored in that copy, and the `devOffset` matches. Otherwise:
3. Compute the target from the disk: `len` becomes the current `entryOffset` (Apple's firmware with its padding), `entryOffset` becomes 0, `checksum` is the byte sum of these `len` bytes. The flash address field is left as it is (section 27).
4. Write the directory sector with the OSOS entry changed to the target, all other bytes left as they were read. Flush, read back.
5. Verify (section 18): the state must now be Clean.

Step 3 is what the existing tools do. It gives a `len` up to one sector longer than Apple's original, made of the padding, which is zeros on the iPods seen so far and then does not change the sum. Step 2 is our addition: with a record, uninstall gives back the exact entry Apple wrote.

## 17. Uninstall, 2nd generation nano

1. Recognise and check as in section 13, rule 1. The state must be Installed (an OSBK entry exists).
2. Check that the OSOS entry is the last entry of the directory and that exactly one OSBK entry exists. A zero entry in the middle of the list would end it early (section 7.2) and hide the entries after it. If the OSOS entry is not last, the iPod is not in a state this spec describes: stop.
3. Write the directory sector with two changes at once: the OSOS entry is filled with 40 zero bytes, and the OSBK entry is renamed back to `soso`. Flush, read back.
4. Verify: the state must now be Clean.

The bytes of our bootloader stay after the last image, unused. The existing tools make step 3 two directory writes; the same reason as in section 15 makes it one.

## 18. Verifying, and the install record

After the last write of an operation, the helper closes nothing yet. It reads, from the disk and not from memory:

1. The directory again, and builds the state of section 10. It must be the state of the table in section 12.
2. For an install or update: the OSOS image (or, on the 2nd generation nano, the new image) for its whole `len`, and checks that the byte sum matches the checksum of the directory, and that the bytes from `entryOffset` (or from the start, on the 2nd generation nano) are the program of the bootloader file.
3. For an uninstall: the byte sum of Apple's firmware matches the checksum in the directory.

Only then does the app say "done". Then the helper closes the disk, and the app asks the user to eject the iPod the normal way before unplugging it. On macOS the disk is unmounted before the write (the system does not let a program write to a mounted disk) and the app waits for it to be mounted again before saying "done".

The **install record** is a small file in the data folder of the app, one per iPod and per operation, written after a successful verification. It holds: the date, the model and target, the serial number of the iPod if the OS gives one, the size of the disk, the 40 bytes of the OSOS entry before the operation, the version and checksum of the bootloader written, the `delta` of any move, and the name of the backup file. The record helps the uninstall (section 16) and the support on the Discord. It is never needed to read the state of an iPod: an iPod installed from another computer is still recognised by section 10 alone.

The existing installer writes a small log on the music partition of the iPod after an install. TacetOS keeps its record on the computer. What TacetOS writes on the music partition, if anything, belongs to the detection spec.

## 19. When a step fails

The helper stops at the first failed write, failed flush or read-back that does not compare equal, and does not try the next step. What the app does next depends on how far it went:

| Failed at | State of the iPod | What the app does |
|---|---|---|
| Before the first write | Unchanged | Shows the message of the failed check. Nothing else |
| Install, a write of data (section 14 step 9, section 15 step 7) | Clean: the directory does not point at the new bytes | Says so, keeps the backup, offers to try again |
| Update, after the first directory write (section 14 step 7, section 15 step 6) | Clean | Says that the bootloader has been removed and the install did not finish, offers to try again |
| A directory write (any) | Unknown: the read-back failed | Reads the state again. If it is Clean or Installed, says which. If it is Inconsistent, offers to write the backup back |
| The move of images (section 14.1) | Broken until restored | Writes the backup back right away, without waiting for the user, then reads the state again |
| Final verification (section 18) | Unknown | As for a directory write |

**Writing the backup back** goes through the same rules as any other write: the disk is recognised again (the sector 0 and the banner of the backup must match the disk), the whole firmware partition is written from the backup file, flushed, read back and compared. If that also fails, the app stops trying and says it plainly: the iPod may not start, Disk Mode is the way back (section 27), keep the backup file, ask on the Discord. The app never deletes a backup after a failure.

## 20. Formatting the music partition

The app offers "Erase the music partition" for a Windows iPod whose music partition cannot be read any more, or whose user wants to start again from an empty iPod. It writes a new, empty FAT32 file system in partition slot 2. It never touches the partition table, the firmware partition or the bootloader. The layout below is the public FAT32 format, with the values TacetOS chooses.

### 20.1 When it is allowed

All of this must be true, or the button is not shown:

1. The disk passes the recognition of section 6 and its state (section 10) is Clean or Installed.
2. The partition table is a DOS table, and slot 2 has the type `0x0B` or `0x0C`. On a Mac iPod, formatting is only done as the second half of a conversion (section 21). The existing tools let a user format the HFS+ partition of a Mac iPod while the map still says HFS: the result is a disk that no system reads correctly. TacetOS must not.
3. The sector size is 512 bytes (section 27).
4. Slot 2 is not mounted. The helper unmounts it first, and refuses if it cannot.
5. The user has confirmed as described in section 22.

### 20.2 Sizes

Let `total` be the sector count of slot 2.

- **Cluster size**, from the size of the partition. These are the usual default values for FAT32:

  | Partition size | Cluster size |
  |---|---|
  | up to 8 GiB | 4 KiB |
  | above 8 GiB, up to 16 GiB | 8 KiB |
  | above 16 GiB, up to 32 GiB | 16 KiB |
  | above 32 GiB | 32 KiB |

  The number of sectors per cluster is the cluster size divided by 512.
- **Reserved sectors**: 32. **Number of FATs**: 2.
- **FAT size** (`fatSize`, in sectors): the smallest value for which one FAT holds an entry of 4 bytes for every cluster, plus the 2 first entries that do not stand for a cluster. The number of clusters is `(total - 32 - 2 * fatSize) / sectorsPerCluster`, rounded down. Start from the estimate `4 * (total - 32)` divided by `512 * sectorsPerCluster + 8`, rounded up, then add 1 while the condition does not hold. The existing tools leave out the 2 first entries in their own check.
- **Limits**: the number of clusters must be at least 65 525 (below that, the format is FAT16 by definition, and other systems read it so), and at most `0x0FFFFFF5`. `total` must fit in 32 bits. Outside these limits, refuse.

### 20.3 What is written

All numbers little-endian. Offsets are in sectors from the start of slot 2.

**Boot sector**, sector 0, and an identical copy in sector 6:

| Offset (bytes) | Size | Value |
|---|---|---|
| 0 | 3 | A jump instruction. `EB 58 90` is the usual value |
| 3 | 8 | Name of the formatting system, `MSWIN4.1` (the value other systems expect) |
| 11 | 2 | Bytes per sector: 512 |
| 13 | 1 | Sectors per cluster (24.2) |
| 14 | 2 | Reserved sectors: 32 |
| 16 | 1 | Number of FATs: 2 |
| 17 | 2 | Root entries: 0 (always 0 in FAT32) |
| 19 | 2 | Total sectors, 16-bit form: 0 |
| 21 | 1 | Media: `0xF8` (fixed disk) |
| 22 | 2 | FAT size, 16-bit form: 0 |
| 24 | 2 | Sectors per track, as reported by the OS, else 63 |
| 26 | 2 | Number of heads, as reported by the OS, else 255 |
| 28 | 4 | Hidden sectors: the first sector of slot 2 on the disk, the **whole 32-bit value** |
| 32 | 4 | Total sectors: `total` |
| 36 | 4 | FAT size: `fatSize` |
| 40 | 2 | Flags: 0 (both FATs kept in step) |
| 42 | 2 | Version: 0 |
| 44 | 4 | First cluster of the root folder: 2 |
| 48 | 2 | Sector of the FSInfo block: 1 |
| 50 | 2 | Sector of the boot sector copy: 6 |
| 64 | 1 | Drive number: `0x80` |
| 66 | 1 | `0x29`, which says the 3 next fields are valid |
| 67 | 4 | Volume serial number, built from the date and time of the format |
| 71 | 11 | Volume label, upper case, padded with spaces. `IPOD` unless the user chose another |
| 82 | 8 | `FAT32` followed by 3 spaces |
| 510 | 2 | `0x55`, `0xAA` |

All other bytes are zero. The existing tools write the hidden sector count through a 16-bit conversion, which cuts it on any iPod whose music partition starts past sector 65 535, that is on every iPod with a firmware partition bigger than 32 MiB. They also always write a serial number of 0. TacetOS must do neither.

**FSInfo block**, sector 1, and a copy in sector 7: the signature `0x41615252` at byte 0, `0x61417272` at byte 484, the number of free clusters at byte 488 (all clusters minus the one used by the root folder), the next free cluster at byte 492 (3), and `0xAA550000` at byte 508. All other bytes are zero.

**Each FAT**, starting at sector 32 and at sector `32 + fatSize`: the first 3 entries are `0x0FFFFFF8`, `0x0FFFFFFF` and `0x0FFFFFFF` (the last one ends the chain of the root folder, which is one cluster long). Everything else in the FATs is zero.

**Root folder**: cluster 2, right after the second FAT, all zeros.

### 20.4 Order of the writes

1. Zero the reserved sectors, both FATs and the root cluster, in pieces of at most 8 MiB. From here on, the old content of the music partition is gone.
2. Write the FATs' first sectors, the FSInfo blocks and the copy of the boot sector (sector 6).
3. Write the boot sector (sector 0) last, flush and read everything back.

Until step 3, no system mounts the partition, so an interruption gives an unreadable partition, not a half-valid one that a system might "repair". Starting the format again from step 1 always works. Then the app checks the result by asking the OS to mount the partition and list its root folder, which must be empty.

## 21. Converting a Mac iPod to FAT32

A Mac iPod must become a Windows iPod before TacetOS can be installed (section 12). The conversion keeps both partitions where they are, replaces the partition map with a DOS table, and formats the music partition. **All music on the iPod is erased.** It is not lost for TacetOS users, because the library lives on the computer and is copied again after the conversion (see `library-rules.md`), but the app says so plainly before the start (section 22).

Allowed only when the disk is recognised as a Mac iPod (sections 2.2 and 6), its state is Clean (a bootloader cannot be on a Mac iPod installed by TacetOS, and an Installed or Inconsistent Mac iPod is refused), the sector size is 512, and the map gives exactly the two slots of section 2.2.

**Before any write:**

1. Back up the firmware partition and the first sectors of the disk up to the start of the firmware partition (they hold the map) to the data folder, as in section 13 rule 2. The music partition is not backed up.
2. Build in memory the new sector 0: all zeros, then 4 entries of the DOS table (section 2.1) from the slots of section 2.3. Entry 1: type `0`, the first sector and count of the firmware partition. Entry 2: type `0x0B`, the first sector and count of the music partition. Entries 3 and 4: zero. The boot flag and the old cylinder-head-sector fields are zero. Bytes 510 and 511 are `0x55` and `0xAA`.
3. Check the new table: entry 1 starts after sector 0, entry 2 starts after the end of entry 1, entry 2 ends inside the disk, and both counts fit in 32 bits.
4. Compute the FAT32 layout of section 20.2 for the size of the music partition, and check its limits.

**The writes:**

5. Unmount the music partition. Write the FAT32 file system into it (section 20.4).
6. Write the new sector 0, flush, read back. **This is the single moment when the disk turns into a Windows iPod.**
7. Run the recognition of section 6 again. It must now find a Windows iPod of the same model, with the firmware partition and its banner unchanged.

The existing tools write the new table first and format afterwards. An interruption between the two leaves a partition typed FAT32 that holds the remains of HFS+. TacetOS formats first and writes the table last: until step 6 the disk is still recognised as a Mac iPod (with an unreadable music partition), and the conversion can simply be started again.

The blocks of the old map after sector 0 stay on the disk. Nothing reads them once sector 0 holds a DOS table (section 2: the `0x55 0xAA` test comes first).

## 22. Confirmations before erasing

Formatting and conversion destroy the music on the iPod. Install, update and uninstall do not. So:

- **Install, update, uninstall**: one confirmation dialog that names the iPod (model and capacity), the operation and the state it ends in.
- **Format and conversion**: a dialog that says "All music on this iPod will be erased", gives the number of files and the space used as read from the music partition (or "cannot be read"), and only enables its button once the user has typed the name of the iPod shown in the dialog. The existing tools ask a yes or no question in a terminal.

No operation starts from a command line flag alone or from a setting. A dev build shows the dialogs, runs the checks, and refuses the write to a real iPod (section 11 check 8).

## 23. Error cases

Every failure gives one short message to the user, and the details go to the log. The table lists the cases this spec knows of, in the order they can happen:

| Case | Found at | What the user sees | What the app does |
|---|---|---|---|
| No disk passes the recognition | Section 6 | "No iPod found. Is it in Disk Mode?" | Keeps watching for disks |
| Every candidate refused for permission | Section 6 | "Permission denied" and how to fix it on this OS | Nothing else |
| More than one iPod | Section 6 | The list, to pick one | Waits |
| Unknown partition table, or banner missing | Sections 2 to 4 | Nothing: the disk is skipped | Logs it |
| Unknown generation | Section 5 | "An iPod TacetOS does not support" | Offers nothing |
| Bad directory (magic, version, overlap...) | Sections 7, 11 | "This iPod's firmware partition is not in a state TacetOS knows" | State Inconsistent, offers nothing |
| Mac iPod | Section 12 | "Formatted for Mac" | Offers the conversion only |
| Bootloader file for another model, or broken | Section 11 check 4 | "The bootloader file does not match this iPod" | Downloads it again once, then stops |
| Not enough room | Section 11 check 5, 14.1, 15 | "Not enough room in the firmware partition" | Stops before any write |
| Backup cannot be made | Section 11 check 6 | "Cannot save a backup in the data folder" and the free space needed | Stops before any write |
| The disk cannot be opened for writing | Section 13 | "The iPod is in use by another program" (or permission) | Stops before any write |
| Disk changed since the scan, or unplugged before writing | Section 11 check 1 | "The iPod was unplugged" | Scans again |
| Unplugged, or a write fails, during the operation | Section 19 | As in the table of section 19 | As in the table of section 19 |
| The music partition cannot be unmounted | Sections 20, 21 | "Close the programs using the iPod" | Stops before any write |
| Partition too small or too big for FAT32 | Section 20.2 | "This partition size is not supported" | Stops before any write |
| Mount check fails after a format | Section 20.4 | "The format did not give a readable partition" | Offers to format again |

## 24. What must never happen

These are absolute. A test exists for each one (section 26.4), and a code change that breaks one is refused.

1. Writing to a disk that has not passed sections 6 and 11 in the same operation, or to a disk that is not removable and on USB.
2. Writing to a real iPod from a development build before the beta.
3. Writing outside the firmware partition during an install, update or uninstall. Writing outside slot 2 during a format. Writing anything other than sector 0 and slot 2 during a conversion.
4. Changing a single byte of Apple's firmware. The only Apple bytes TacetOS ever writes are those of its own backup of the same iPod, written back to it after a failure. TacetOS never downloads, ships or installs an Apple firmware.
5. A directory entry that points at bytes not yet written, or a moment, between two writes, where the iPod has no OSOS entry.
6. Installing on a Mac iPod, or formatting the HFS+ partition of a Mac iPod outside the conversion.
7. Formatting or converting without the typed confirmation of section 22.
8. Deleting a backup automatically after a failure.
9. Guessing: a model, a sector size, an image order, an entry the spec does not describe. Each one is a refusal.
10. Two operations on the same iPod at the same time. The helper holds a lock on the disk for the whole operation.
11. Going on after the iPod was unplugged and plugged back in, even when it gets the same name. Everything starts again from section 6.

## 25. Constants at a glance

Every fixed value of this spec, in one place, with the section that explains it. The code keeps them in one file of constants, with the same names as this table.

| Name | Value | Section |
|---|---|---|
| DOS table signature | bytes 510, 511 = `0x55`, `0xAA` | 2 |
| DOS table entries | 4 entries of 16 bytes from byte `0x1BE` | 2.1 |
| DOS type, firmware partition | `0x00` | 2.1 |
| DOS type, FAT32 | `0x0B` (written), `0x0B` or `0x0C` (accepted) | 2.1 |
| Apple map header | `ER` at byte 0, block size at byte 2 | 2.2 |
| Apple map entry | `PM` at byte 0, type text at byte 48 | 2.2 |
| Apple map types kept | `Apple_MDFW`, `Apple_HFS` | 2.2 |
| Map blocks read, at most | 64 | 2.2 |
| Slots kept, at most | 4 | 2.2 |
| HFS value in the common description | `0xFFFF` | 2.3 |
| Banner | 256 bytes at the start of the firmware partition | 4 |
| Volume magic | `]ih[` at `0x100` | 7.1 |
| Directory position | value at `0x104`, plus `0x200` | 7.1 |
| Format version | at `0x10A`: `2` (3rd generation) or `3` | 7.1 |
| Directory entry size | 40 bytes | 7.2 |
| Directory, at most | 10 entries, 400 bytes | 7.2 |
| Device tags | `!ATA`, `DNAN` | 7.2 |
| Image type tags (as stored) | `soso`, `crsr`, `dpua`, `kbso`, `ebih` | 7.2 |
| Flash address when unused | `0xFFFFFFFF` | 7.2 |
| Hash block, 2nd generation nano | `0x800` bytes | 7.5 |
| Sector size, 2nd generation nano | 2048 bytes | 15 |
| Bootloader file header | 8 bytes: big-endian checksum, then a 4-letter model tag | 8 |
| Data held in memory for one write, at most | 8 MiB | 9 |
| Size of one piece when copying or zeroing | 8 MiB at most | 14.1, 20.4 |
| FAT32 reserved sectors | 32 | 20.2 |
| FAT32 number of FATs | 2 | 20.2 |
| FAT32 boot sector copy, FSInfo copy | sectors 6 and 7 | 20.3 |
| FAT32 clusters | at least 65 525, at most `0x0FFFFFF5` | 20.2 |
| FAT32 first entries | `0x0FFFFFF8`, `0x0FFFFFFF`, `0x0FFFFFFF` | 20.3 |
| FSInfo signatures | `0x41615252` at 0, `0x61417272` at 484, `0xAA550000` at 508 | 20.3 |
| Default volume label | `IPOD` | 20.3 |

The model values of section 5 and the model tags and numbers of section 8 are tables of their own and are not repeated here. They stay in step with `models/models.toml`.

## 26. What the tests need

All tests run on disk image files, never on a real iPod (section 11 check 8). Each group below adds to the fixtures of the one before, and the expected results are files kept next to the fixtures.

### 26.1 Finding the iPod (sections 2 to 6)

The fixtures are small files, no real iPod needed:

- A Windows iPod image with a DOS table, a firmware partition that starts with the banner, and a FAT32 partition.
- A Mac iPod image with an Apple Partition Map holding the map itself, a firmware entry and an HFS entry.
- Broken variants: no signature, `ER` header with an immediate non-`PM` block, a map with more than 4 kept entries, a type `Apple_HFSX`, a firmware partition without the banner, a table whose slot 1 is not type 0, a table with type `0x05`.
- Each model value of section 5, plus one unknown value.

The expected results are the slots of section 2.3, the verdict of sections 3 and 4, and the model of section 5. The same fixtures are used again by the groups below.

### 26.2 Firmware partition and checks (sections 7 to 11)

The fixtures of section 26.1 are extended with a directory:

- A firmware partition with the volume header and a directory holding one OSOS entry, plus variants with an extra RSRC entry, an AUPD entry and an entry with an unknown tag.
- The same with a bootloader already appended (`entryOffset` not 0), and a 2nd generation nano image with an OSBK entry and a directory that starts one sector late.
- Broken variants: a bad `]ih[` magic, a format version of 4, a directory pointing outside the partition, two OSOS entries, an OSOS entry that is not first, overlapping entries, an entry running past the end of the partition, an `entryOffset` that is not sector-aligned, an `entryOffset` larger than `len`, a sector size of 3000.
- Bootloader files: a good one for each target of section 8, and variants with one wrong byte in the program, a wrong tag, a file shorter than 8 bytes, and a file with a tag of another model and a matching checksum.
- Each check of section 11 has a fixture that fails only that check, and the expected message.

The expected results are the list of entries, the state of section 10, and the first check of section 11 that fails. Tests also confirm that a failing check leaves the fixture file byte-for-byte unchanged.

### 26.3 Install, update and uninstall (sections 12 to 19)

For this group:

- Install, update and uninstall on each fixture of section 26.2 that is not broken, for both layouts, with the expected directory after each one, and the bytes of the image area compared with an expected file.
- An install where the next image must move, and one where it cannot move because the partition is too small. In the second case, the fixture is byte-for-byte unchanged.
- An update with a larger bootloader than the installed one, and with a smaller one.
- A 2nd generation nano whose directory is full (10 entries).
- A test that stops the helper after each single write of each operation (a fault injected in the write function of the tests), and checks that reading the state then gives the state of the table in section 19, and that writing the backup back gives the original fixture byte for byte.
- Uninstall with and without an install record, and with a record whose checksum no longer matches the disk.
- Against the reference tool, byte for byte (install, update and uninstall of every partition family): on the 2nd generation nano the uninstall is compared with the clean image from before the install instead, because the reference tool's command line cannot uninstall on the 2nd generation nano. Only the bytes of our bootloader left after the last image (section 17) may differ.
- A fixture whose padding after Apple's firmware is not zero, to check the checksum of section 14 step 5 counts it as it is on the disk.

### 26.4 Formatting and conversion (sections 20 to 24)

- Format: images of a music partition for each row of the cluster size table, the smallest size that passes (65 525 clusters) and one that is one cluster short. The expected boot sector, FSInfo and FAT sectors are compared byte for byte, and the result is checked by a second, independent FAT reader in the tests (the one of the OS when the test machine allows it, else `fsck.fat` in the CI).
- A music partition that starts past sector 65 535, to check the hidden sector field.
- Conversion: a Mac iPod image (from section 26.1) converted, then recognised as a Windows iPod of the same model, with the firmware partition byte-for-byte unchanged. The same with a fault injected after the format and before the table: the disk must still be recognised as a Mac iPod, and a second conversion must succeed.
- Refusals: format of a Mac iPod, conversion of an Installed one, sector size of 2048, a map with three slots.
- One test per rule of section 24, each one running the operation on a fixture built to tempt the code into breaking the rule, and checking that the fixture is unchanged outside the allowed area.

## 27. Not decided yet

- A copy of the banner and of a real firmware partition header from a real older iPod (the rows of section 4 were checked by the reference partition tool, not on a device). A real Classic has no firmware partition: its dump of 2026-11-11 shows a single FAT32 partition in slot 1, with sectors of 4096 bytes (`detection.md` section 6.1).
- Whether a Mac iPod with 2048-byte sectors exists in the wild. If one does, section 2.3 needs a real example.
- Whether the app should warn when an iPod passes the tests but its firmware version is much older than the others of the same generation. The version field is read (section 7.2) but no list of known versions exists yet.
- The location of the image area for a 3rd generation iPod (section 7.3) and the version field of its OSOS entry. Needs a dump of a real 3rd generation iPod, or the spec of a trustworthy second source.
- The model tag of the 2nd generation nano (section 8), and whether its bootloader file carries the `0x800` extra bytes inside the program or as a separate block. Decided with the first real build output, in the CI.
- Whether the checksum in the directory for Apple's firmware is the same byte sum as the one used for the bootloader (section 11, check 7). Until then, a mismatch is only a warning.
- Whether the order of the images in a real directory can ever differ from "OSOS first". If a dump shows it can, check 2 must handle it.
- Whether a single-sector write of the directory can be torn by a power loss on these disks (section 13, rule 4). The order of the steps assumes it cannot. A test on real hardware cannot prove it, so the backup of section 13 stays the safety net either way.
- Whether Apple's loader reads the flash address field of the OSOS entry (offset 36) when the iPod has no bootloader. The install sets it to `0xFFFFFFFF`, and the uninstall without an install record cannot know the old value (section 16).
- On the 2nd generation nano, whether the lengths in a real directory are always whole sectors. Section 15 rounds the end of the last image up to a sector, the existing tools do not round. They agree when the lengths are whole sectors.
- Whether any model of section 5 reports a sector size other than 512 bytes in Disk Mode, apart from the 2nd generation nano. Formatting and conversion (sections 20 and 21) refuse anything but 512 until a real example shows otherwise.
- Whether Disk Mode of these models still works when the firmware partition is broken. It is widely reported that it does, because the mode does not come from the firmware partition. To be confirmed on a real iPod before section 19 promises it to the user.
