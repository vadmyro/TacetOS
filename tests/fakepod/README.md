# fakepod

Generated disk images that look like an older iPod (firmware partition, then a music partition), one per model id of `models/models.toml`, for the tests of the installer. Nothing here is real firmware: the banner, the images and the bootloader are synthetic, and no music is ever in an image.

- `tacet_fakepod`: the library. `FakePod::build(const FakePodSpec &)` writes one image and returns where things are.
- `tacet-fakepod <id> <file> [--mac] [--bootloader] [--rsrc] [--aupd] [--late-directory]`: the same, from the command line, to try an image by hand.
- `tst_fixtures`: builds every named fixture and bootloader file, compares each variant with its base image, and checks the expected results of the table against the bytes of the image.
- `tst_fakepod`: reads every image back and checks it byte by byte at the offsets of `docs/specs/bootloader-legacy.md`. The reader in the test finds the partitions from the table in the image, it does not use the builder's layout.

## What an image holds

The file is sparse and 64 MiB long. Sectors are 512 bytes, 2048 for the 2nd generation nano (sections 7.5 and 15), or 4096 for the Classic.

| Where | Content |
|---|---|
| Sector 0 | DOS table (section 2.1), or the Apple Partition Map header and 3 entries (section 2.2) |
| Sector 64, 8 MiB | Firmware partition: banner (section 4), `]ih[` header (7.1), directory at `0x200` (7.2), OSOS image at `0x4000` (7.3) |
| After it | Music partition: an empty FAT32 (section 20.3), or a bare HFS+ volume header at 1024 bytes (Mac) |

The OSOS entry carries the model value of section 5 in its version (`value << 8 | 5`). Windows and Mac images exist for the 8 models with 512-byte sectors that use the partition method: all but `classic-6g` and `nano-2g`. The Classic image is different, like a real Classic (dump of 2026-11-11): sectors of 4096 bytes, a DOS table with one FAT32 partition in slot 1 (from sector 8, up to the end), no firmware partition, no banner. A reader of the legacy layout stops at the partition test of section 3, which is right for a DFU model. The 2nd generation nano is Windows only, because section 27 leaves open whether a Mac one exists.

## Options of `FakePodSpec`

- `table`: `Dos` (Windows iPod) or `Apm` (Mac iPod).
- `bootloaderPresent`: the bootloader is already installed. Appended in the OSOS image (section 7.4), or an OSBK entry plus a new OSOS image with its hash block (section 7.5, 2nd generation nano). `bootloader` gives the program, else a synthetic one of 4000 bytes.
- `withRsrc`, `withAupd`: extra entries after OSOS (section 7.2).
- `directoryLate`: the directory starts one sector after the computed position, whose first byte is zero (section 7.2).

## Choices that the spec does not make

- **FAT32 size.** Section 20.2 gives 4 KiB clusters, which cannot reach the 65 525 clusters of a FAT32 in 64 MiB. The cluster is the largest one that still gives that many. A 2048-byte-sector image cannot reach it at all (one sector per cluster, fewer clusters), and section 20.1 only formats 512-byte sectors, so no rule exists for it.
- **HFS+.** Only the volume header is written. It is not a volume that a system can mount.
- **Fixed serial number** `0x54414345`, so that two builds give the same bytes.
- **Flash address field** of the OSOS entry: `0` without bootloader, `0xFFFFFFFF` with it (section 7.4). Extra entries use `0xFFFFFFFF`.
- **3rd generation** (format version 2): the image area is at the start of the partition when there is more than one entry (section 7.3).

## Named fixtures (P4.1b)

`fixtures.h` lists every fixture of sections 26.1 and 26.2 of the spec under a stable `FixtureId`, with its text name, its base image and the result that a reader must give (`Expected`: slots, verdict, target, state, directory entries, first failing check, warning, message). The tests of the installer (P4.2) use this table as it is. `Fixtures::build(id, path)` writes the image and returns the byte ranges that differ from the base image.

- **Images from scratch:** one Windows image per model id, one Mac image per model that has one, the installed variants (bootloader appended, or OSBK on the 2nd generation nano, with or without a late directory) and the variants with RSRC and AUPD entries.
- **Variants:** a base image plus a few patched bytes. The test builds both and requires that every differing byte lies in a declared zone, and that every zone changes something. Two variants change no byte of the file: `dos-sector-size-3000` (the sector size comes from the OS, `Expected::reportedSectorSize`) and the `check4-*` ones (the bootloader file is the variable part, `FixtureInfo::bootFile`).
- **Bootloader files:** `BootloaderFile` writes the 8-byte header (big-endian checksum, model tag) and a synthetic program. `Fixtures::bootFile(BootFileId)` gives a good file for each of the 9 targets, and the variants of section 26.2: a wrong byte, a wrong tag, a file of 5 bytes, a header with no program, and the tag of another model with a checksum that passes.
- **One check broken:** `Fixtures::singleCheck(n)` names the fixture that fails only check `n` of section 11, for 1, 2, 3, 4, 5 and 7 (check 7 is a warning, `warningCheck`). Check 6 (a backup that cannot be made) and check 8 (a real disk) are not about the content of an image: the tests of the checks set them up themselves.

Choices that the spec does not make, all in the table and easy to change:

- A DOS table gives its 4 entries by position (empty ones all zero). An Apple Partition Map gives the kept entries, at most 4.
- A bad `]ih[` magic, or a directory pointer outside the partition, is a verdict of its own (`BadDirectory`): the model cannot be read, so no check runs.
- A sector size of 3000 is a verdict of its own (`BadSectorSize`), with no slots.
- For an installed iPod, the spec does not say what check 7 gives (the directory sum covers the bootloader, check 7 says up to `entryOffset`). `warningCheck` is -1 there, meaning not decided.
- The messages of the table are the ones the installer must give. A failing check says `check N: ...`.
- The tag of the 2nd generation nano file is `nn2g`, as in section 27 the choice is still open.
