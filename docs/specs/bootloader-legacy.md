# Bootloader install, older iPods

How TacetOS finds an older iPod on a computer and decides whether the disk really is one, before anything is written. "Older" means the models whose install method is `partition` in `docs/models/`: the bootloader lives in a small firmware partition at the start of the disk, next to the music partition. The first part (sections 1 to 7) covers finding the iPod. The second part (sections 8 to 12) covers the layout of the firmware partition, the bootloader file and the checks before writing. Later parts cover the install and uninstall steps, formatting and error cases.

The numbers and layouts here are facts about the iPod disk, taken from public documentation and from a study of existing installers, and they are written in our own words. The parts that are our own design (what to accept, what to refuse, what to report) say "must".

## 1. Words used here

- **Disk**: the whole storage of the iPod as the computer sees it in Disk Mode, for example `/dev/sdb`, not one partition of it.
- **Sector**: the unit the disk is read in. The OS reports its size. When it cannot, assume 512 bytes.
- **LBA**: the number of a sector, counted from 0 at the start of the disk.
- **Firmware partition**: the small first partition, holding Apple's firmware and, after install, the TacetOS bootloader next to it.
- **Music partition**: the large second partition, holding the files.
- **Windows iPod**: an iPod formatted by iTunes on Windows. It has a DOS partition table and a FAT32 music partition.
- **Mac iPod**: an iPod formatted by iTunes on a Mac. It has an Apple Partition Map and an HFS+ music partition.

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

The test decides what the disk is. It never decides that something is safe to write. Writing needs the extra checks of a later part.

## 4. Firmware partition check

The first sector of the firmware partition must carry Apple's banner. It is a block of exactly 256 bytes, made of 16 rows of 16 characters, then a zero byte at the end:

- 7 rows draw a stop sign. Each of these rows begins with the 4 characters `{{~~`, and the sign contains the words `S T O P`.
- The next 3 rows hold the text `Copyright(C) 2001 Apple Computer, Inc.` continuing across the row boundaries, then dashes up to the end of the row.
- The remaining rows are dashes. The banner is 255 visible characters followed by the zero byte.

The test compares these 256 bytes with a constant stored in our code. The exact bytes of the constant come from a dump of the firmware partition of a real iPod and are kept in the test fixtures, not retyped from memory. If the banner does not match, the disk is not an iPod we handle: stop, and report "firmware partition does not carry the expected banner". Do not try to repair it.

## 5. Model of the iPod

The model is not read from the USB id, because several generations share one. It is read from the firmware itself: the firmware directory of the partition (section 8) holds an entry for the main firmware, and the version stored there tells the generation. Only the upper part of the version matters: shift the number right by 8 bits, then look it up:

| Value | Generation | Target name in TacetOS |
|---|---|---|
| `0x01` | 1st or 2nd generation | `ipod1g2g` |
| `0x02` | 3rd generation | `ipod3g` |
| `0x40` | 1st generation mini | `ipodmini1g` |
| `0x50` | 4th generation (grayscale) | `ipod4gray` |
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

All reads in this part are read-only. Nothing in this part may write to a disk. A dev build reads a real iPod the same way, because reading is allowed before the beta; only the write steps in the later parts are refused.

## 7. What the tests need

The fixtures are small files, no real iPod needed:

- A Windows iPod image with a DOS table, a firmware partition that starts with the banner, and a FAT32 partition.
- A Mac iPod image with an Apple Partition Map holding the map itself, a firmware entry and an HFS entry.
- Broken variants: no signature, `ER` header with an immediate non-`PM` block, a map with more than 4 kept entries, a type `Apple_HFSX`, a firmware partition without the banner, a table whose slot 1 is not type 0, a table with type `0x05`.
- Each model value of section 5, plus one unknown value.

The expected results are the slots of section 2.3, the verdict of sections 3 and 4, and the model of section 5. The same fixtures are used again by the later parts.

## 8. Layout of the firmware partition

All offsets in this section are in bytes from the start of the firmware partition, unless said otherwise. All numbers are little-endian. The partition holds a small header, a directory of images, and the images themselves.

### 8.1 Volume header

The first sector holds the banner of section 4 in its first 256 bytes, then:

| Offset | Size | Meaning |
|---|---|---|
| `0x100` | 4 bytes | The characters `]ih[`. Anything else means "bad firmware directory" |
| `0x104` | 4 bytes | Where the directory is, counted from offset `0x200`. The directory starts at this value plus `0x200` |
| `0x10A` | 2 bytes | Format version of the partition: `2` for the 3rd generation, `3` for the later ones |

The bytes in between (`0x108`, a pointer to an extended header) are not used. The directory is not always aligned on a sector. Keep the remainder of its offset divided by the sector size, because the directory is read and written as one whole sector and the entries start at that remainder inside it.

A version other than 2 or 3 is not an error for reading, so the app can tell the user what it saw, but it must stop any write (section 12, check 2).

### 8.2 Directory

The directory is a list of entries of 40 bytes each, one after the other, with no count stored anywhere. The list ends at the first entry whose first 4 bytes are not `!ATA` (some players write `DNAN`; accept both). At most 10 entries are read, and none may start more than 400 bytes after the start of the directory.

One special case: on the 2nd generation nano the first byte at the computed position is zero. The real directory then starts at the beginning of the next sector. This is the only way to tell that layout, so the code must apply it only when the first byte is exactly zero.

Fields of one entry:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 bytes | Device tag, `!ATA` (or `DNAN`) |
| 4 | 4 bytes | Image type, a 4-letter tag stored backwards (see below) |
| 8 | 4 bytes | Image number, usually 0 |
| 12 | 4 bytes | `devOffset`: where the image starts, counted from the **image area** (section 8.3) |
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
| `kbso` | OSBK | A backup of the main firmware (used on the 2nd generation nano, see 8.5) |
| `ebih` | HIBE | Hibernation image |

An unknown tag does not stop the reading: the entry is kept as "other", never modified, and still counts for the overlap check of section 12. The model of section 5 comes from the version field of the OSOS entry.

### 8.3 Where the images are

The **image area** starts one sector after the start of the partition, except in one case: a 3rd generation partition (format version 2) with more than one image in its directory has the image area at the very start of the partition. This rule is taken from how existing tools behave and has not been checked against a dump of a 3rd generation iPod, see section 14.

The bytes of an image are at: start of partition, plus the image area start, plus `devOffset`, for `len` bytes. Images are padded with zero bytes up to a whole sector. On a normal iPod the OSOS image comes first, and the others follow it with their `devOffset` growing.

A 3rd generation OSOS entry may carry a version of 0. Another tool invents the value 3 in memory in that case, which maps to no model in section 5. TacetOS must not copy this: a 3rd generation iPod whose version field gives no model is reported as an unknown generation until a real dump shows what the field holds.

### 8.4 Where the bootloader sits (all models except the 2nd generation nano)

The bootloader is not a new image. It is appended inside the OSOS image, after Apple's firmware, and the entry's `entryOffset` tells the iPod where to start running:

- Without the bootloader: `entryOffset` is `0`, `len` is the size of Apple's firmware.
- With the bootloader: Apple's firmware is padded with zeros up to a whole sector, the bootloader is stored right after it, `entryOffset` is the position of the bootloader (a multiple of the sector size), `len` is `entryOffset` plus the bootloader size, `checksum` covers all `len` bytes, and the flash address field is set to `0xFFFFFFFF`.

So the test "this iPod has the bootloader" is `entryOffset` different from 0 in the OSOS entry. Apple's own firmware is still there, in front. The bootloader starts it when the user asks for it (the double boot of the project rules).

When the bootloader is already there, an update keeps the same `entryOffset`. It never moves it.

### 8.5 The 2nd generation nano

This model is different: its firmware is encrypted and checked by the hardware, so the bootloader cannot be appended.

- The OSOS entry is renamed to OSBK (a backup of Apple's firmware) and the TacetOS bootloader is written as a new OSOS image after the last image.
- "This iPod has the bootloader" means an OSBK entry exists.
- The sector before the firmware holds hashes that the iPod needs. A bootloader file for this model starts with those `0x800` bytes (2048), and the data is written `0x800` bytes before `devOffset`. The `len` field does not count them.
- The `checksum` field is computed over the unencrypted data and the iPod does not check it. It is still written, to keep the directory coherent.

## 9. The bootloader file

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
| `ipod4gray` | `ip4g` | 8 |
| `ipodcolor` | `ipco` | 3 |
| `ipodmini2g` | `mn2g` | 11 |
| `ipodnano1g` | `nano` | 4 |
| `ipodvideo` | `ipvd` | 5 |
| `ipodnano2g` | see below | 62 |

Both columns were read in two places (the tool that makes the files and the tool that checks them) and they agree, except for one thing: the 2nd generation nano is tagged `nn2g` by the one that makes the files and `nn2x` by the one that checks them. The existing installer avoids the problem by carrying its own copy of the bootloader for this model. TacetOS must decide this with a real build output, see section 14.

These numbers are separate from the model values of section 5. Section 5 identifies the iPod from its firmware, and this table identifies the file. A check passes only when both give the same target.

## 10. Sizes and alignment

- The sector size is a power of two. The padding arithmetic of the whole spec (round up to a whole sector) depends on it, so a sector size that is not a power of two stops everything.
- The data TacetOS holds in memory for one write (the firmware before the bootloader, the bootloader, and the padding) must fit in 8 MiB. This is enough for the Apple firmware of the models in section 5. The resources image of the 2nd generation nano is larger, and TacetOS never loads it. If the data does not fit, the install stops.
- The padded size of an image is its length rounded up to a whole sector.
- The `entryOffset` of a bootloader already installed is a multiple of the sector size, lower than `len`, and not lower than the length of Apple's firmware. Anything else is a broken install.

## 11. Reading the state of an iPod

After the recognition of section 6, and still read-only, the code builds one description of the iPod: the list of directory entries, the index of the OSOS entry, the start of the image area, and the state, which is one of:

- **Clean**: no bootloader.
- **Installed**: bootloader present (section 8.4 or 8.5).
- **Inconsistent**: the entries contradict section 10 or section 12. No write is allowed, and the message says what contradicts what.

The state is shown to the user before the first write, and it decides what the later steps offer: install on a clean iPod, update or remove on an installed one, nothing on an inconsistent one.

## 12. Checks before writing

Nothing is written until every check below passes. They run in this order, after the iPod has been recognised (sections 3 to 5), and again just before the first write. If one fails, nothing has been changed, the message names the check, and the install does not continue.

1. **Same disk as at the scan.** Read sector 0 and the first sector of the firmware partition again, and compare them with what the scan saw. If anything differs, or the disk is gone, refuse. This catches an iPod unplugged and another disk plugged in on the same name.
2. **The structure is valid.** The format version is 2 or 3. The directory lies fully inside the firmware partition. The list holds an OSOS entry, exactly one. The OSOS entry is the first one of the list (the existing tools assume it, and TacetOS refuses otherwise until a dump shows another order).
3. **The entries make sense.** For every entry, `devOffset` plus its padded size ends inside the firmware partition. No two entries overlap, and they are in increasing `devOffset` order. For the OSOS entry, the checks of section 10 on `entryOffset` also hold.
4. **The file is for this iPod.** The bootloader file has at least the 8 bytes of the header and at least one byte of program. Its model tag is the one of the table in section 9 for the recognised target. Its checksum matches. A file that fails the checksum is never written, however small the difference.
5. **There is room.** Compute the position where the end of the new data will fall: in the OSOS image for most models, after the last image for the 2nd generation nano. It has to be inside the firmware partition, and it must not run into the next image. If the next image is in the way, the code may only move the following images forward if the later part of this spec says how and the same check then passes for the new positions. Where no such move is allowed, the install stops with "not enough room in the firmware partition". The existing tools do not check the size of the partition; TacetOS must.
6. **A backup exists.** Before the first write, the helper copies to a file, in the data folder of the app, every byte it is going to change or move: the sector with the directory, and the part of the firmware partition that will be written. It reads the copy back and compares it. The file name holds the model and the date. If the copy cannot be made or does not compare equal, nothing is written. The use of this file in a recovery belongs to a later part.
7. **The firmware checksum.** Compute the checksum of Apple's firmware as it is on the iPod (the bytes from the start of the OSOS image up to `entryOffset`, or up to `len` when there is no bootloader), and compare it with the directory. A difference is a warning that goes into the log and the backup file, not a refusal, because the way Apple computes it has not been checked on a real dump. See section 14.
8. **The right gate.** Only the helper writes to a disk. A development build refuses to write to a real iPod before the beta, so the checks above can run on a dev build, but the write after them is refused there. A disk image file (the test fixtures) may be written by any build.

A check never "fixes" anything by itself. It either passes, or it stops with a message. Nothing in the checks may guess: where the data has a shape this spec does not describe, it is a refusal.

## 13. What the tests need for the second part

The fixtures of section 7 are extended with a directory:

- A firmware partition with the volume header and a directory holding one OSOS entry, plus variants with an extra RSRC entry, an AUPD entry and an entry with an unknown tag.
- The same with a bootloader already appended (`entryOffset` not 0), and a 2nd generation nano image with an OSBK entry and a directory that starts one sector late.
- Broken variants: a bad `]ih[` magic, a format version of 4, a directory pointing outside the partition, two OSOS entries, an OSOS entry that is not first, overlapping entries, an entry running past the end of the partition, an `entryOffset` that is not sector-aligned, an `entryOffset` larger than `len`, a sector size of 3000.
- Bootloader files: a good one for each target of section 9, and variants with one wrong byte in the program, a wrong tag, a file shorter than 8 bytes, and a file with a tag of another model and a matching checksum.
- Each check of section 12 has a fixture that fails only that check, and the expected message.

The expected results are the list of entries, the state of section 11, and the first check of section 12 that fails. Tests also confirm that a failing check leaves the fixture file byte-for-byte unchanged.

## 14. Not decided yet

- The exact bytes of the banner and of a real firmware partition header are taken from a real device dump. Until the first test on real hardware, the fixtures use a banner built from the description in section 4.
- Whether a Mac iPod with 2048-byte sectors exists in the wild. If one does, section 2.3 needs a real example.
- Whether the app should warn when an iPod passes the tests but its firmware version is much older than the others of the same generation. The version field is read (section 8.2) but no list of known versions exists yet.
- The location of the image area for a 3rd generation iPod (section 8.3) and the version field of its OSOS entry. Needs a dump of a real 3rd generation iPod, or the spec of a trustworthy second source.
- The model tag of the 2nd generation nano (section 9), and whether its bootloader file carries the `0x800` extra bytes inside the program or as a separate block. Decided with the first real build output, in the CI.
- Whether the checksum in the directory for Apple's firmware is the same byte sum as the one used for the bootloader (section 12, check 7). Until then, a mismatch is only a warning.
- Whether the order of the images in a real directory can ever differ from "OSOS first". If a dump shows it can, check 2 must handle it.
