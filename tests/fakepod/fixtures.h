// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "bootloaderfile.h"
#include "fakepod.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// The named fixtures of docs/specs/bootloader-legacy.md sections 26.1 and 26.2:
// disk images (good and broken), bootloader files, and for each one the result
// that a reader of the legacy layout must give. The tests of the installer use
// the table as it is (P4.2a, P4.2b, P4.2c): a fixture is built by its id, and
// what to expect comes from `Fixtures::info(id).expected`.
//
// A broken fixture is a base image plus a few patched bytes. `FixtureInfo::base`
// names the base and `FixtureBuild::zones` the byte ranges that were patched,
// so that a test can check that nothing else differs.

namespace tacetos::fakepod {

// Stable names: never renumber or rename, add at the end of a group only.
enum class FixtureId : int {
    // One Windows image per model id of models.toml (section 26.1, "each model
    // value of section 5"). The Classic has no firmware partition: one FAT32 partition.
    DosIpod1g2g,
    DosIpod3g,
    DosIpod4g,
    DosIpodColor,
    DosMini1g,
    DosMini2g,
    DosIpodVideo,
    DosNano1g,
    DosNano2g,
    DosClassic6g,
    // One Mac image per model that has one.
    MacIpod1g2g,
    MacIpod3g,
    MacIpod4g,
    MacIpodColor,
    MacMini1g,
    MacMini2g,
    MacIpodVideo,
    MacNano1g,
    // Unknown values of section 5 (7.3 for the 3rd generation).
    DosUnknownValue,
    Dos3gVersionZero,
    // Section 26.1, broken or unusual tables. Base: DosIpodVideo or MacIpodVideo.
    DosNoSignature,
    ApmNoPartitionEntry,
    ApmTooManyKept,
    ApmHfsx,
    DosNoBanner,
    DosSlot1NotZero,
    DosExtendedType,
    DosFat32Lba,
    // Section 26.2, directories. Base: DosIpodVideo.
    DosVideoRsrc,
    DosVideoAupd,
    DosVideoRsrcAupd,
    DosVideoUnknownTag,
    // Section 26.2, bootloader already added.
    DosIpod1g2gInstalled,
    DosIpod3gInstalled,
    DosIpod4gInstalled,
    DosIpodColorInstalled,
    DosMini1gInstalled,
    DosMini2gInstalled,
    DosIpodVideoInstalled,
    DosNano1gInstalled,
    MacIpodVideoInstalled,
    DosVideoRsrcAupdInstalled,
    // The 2nd generation nano: OSBK entry, and directories one sector late.
    DosNano2gInstalled,
    DosNano2gLateDirectory,
    DosNano2gInstalledLate,
    // Section 26.2, broken directories.
    DosBadMagic,
    DosFormatVersion4,
    DosDirectoryOutside,
    DosTwoOsos,
    DosOsosNotFirst,
    DosOverlap,
    DosEntryPastEnd,
    DosEntryOffsetUnaligned,
    DosEntryOffsetBeyondLen,
    DosSectorSize3000,
    // Section 26.2, one fixture that breaks only one check of section 11.
    Check1DiskChanged,
    Check4WrongByte,
    Check4WrongTag,
    Check4TooShort,
    Check4HeaderOnly,
    Check4OtherModelTag,
    Check5NoRoom,
    Check7WrongChecksum,
    Count
};

enum class BootFileId : int {
    // A good file for each target of section 8.
    GoodIpod1g2g,
    GoodIpod3g,
    GoodMini1g,
    GoodIpod4gray,
    GoodIpodColor,
    GoodMini2g,
    GoodNano1g,
    GoodIpodVideo,
    GoodNano2g,
    // Variants of the file of the Video, section 26.2.
    WrongByte,     // one byte of the program changed, header untouched
    WrongTag,      // a tag that is no model, checksum right
    TooShort,      // 5 bytes
    HeaderOnly,    // 8 bytes, no program
    OtherModelTag, // the tag of the 4th generation, the checksum right for the Video
    Count
};

// A slot of section 2.3. `type` is 0xFFFF for HFS (a Mac iPod).
struct SlotExpect
{
    quint32 type = 0;
    quint32 firstSector = 0;
    quint32 sectorCount = 0;
    bool operator==(const SlotExpect &o) const
    {
        return type == o.type && firstSector == o.firstSector && sectorCount == o.sectorCount;
    }
};

// How far the reading of sections 2 to 6 goes.
enum class Verdict {
    UnknownTable,        // section 2: no DOS signature, no `ER`
    PartitionTestFailed, // section 3
    NoBanner,            // section 4
    BadDirectory,        // section 7.1: no `]ih[`, or a directory outside the partition
    UnknownGeneration,   // section 5: looks like an iPod, not one we handle
    BadSectorSize,       // section 9: not a power of two
    Recognised,
};

// Section 10. `NotRead` when the verdict is not Recognised.
enum class State { NotRead, Clean, Installed, Inconsistent };

// What a reader of the legacy layout must find. For the first failing check,
// a bootloader file is assumed: the one of the fixture (`bootFile`), else the
// good file for the recognised target.
struct Expected
{
    // Section 2.3. A DOS table gives its 4 entries by position (empty ones all
    // zero); an Apple Partition Map gives the kept entries, at most 4, in order.
    QList<SlotExpect> slotList;
    Verdict verdict = Verdict::Recognised;
    // Section 5 target name, empty when no model is read.
    QString target;
    State state = State::NotRead;
    // The directory entries, as `OSOS`, `RSRC`, `AUPD`, `OSBK`, or `other`;
    // empty when the verdict is not Recognised.
    QStringList entries;
    // The first check of section 11 that fails, 1 to 8, or 0 when none does.
    int firstFailingCheck = 0;
    // A check that only warns (section 11 check 7), or 0. -1: not decided, the
    // spec does not say what check 7 gives on an installed iPod (the directory
    // sum of section 14 step 5 covers the bootloader, check 7 says up to
    // `entryOffset`).
    int warningCheck = 0;
    // The sector size the OS reports, when it is not the one of the model.
    int reportedSectorSize = 0;
    // The message, in English, that the installer must give for the verdict, the
    // failing check or the warning; empty when there is none.
    QString message;
    // Check 1 compares the disk with what the scan saw: the fixture that was
    // scanned, when it is not this one.
    std::optional<FixtureId> scannedAs;
};

struct FixtureInfo
{
    FixtureId id = FixtureId::Count;
    // Stable text name, for the logs and for `tacet-fakepod --fixture`.
    const char *name = "";
    const char *what = "";
    // The image this one is patched from. None for an image built from scratch.
    std::optional<FixtureId> base;
    // The bootloader file to offer with this image (checks 4 and 5).
    std::optional<BootFileId> bootFile;
    Expected expected;
};

struct BootFileInfo
{
    BootFileId id = BootFileId::Count;
    const char *name = "";
    // The target the file is offered for.
    const char *target = "";
    // Check 4 passes for `target`.
    bool ok = false;
    // The message of check 4 when it fails.
    QString message;
};

struct ByteRange
{
    qint64 offset = 0;
    qint64 length = 0;
};

struct FixtureBuild
{
    bool ok = false;
    QString error;
    FakePodResult pod;
    // The ranges patched on top of the base image, in bytes from the start of
    // the file. Empty for an image built from scratch, and for a variant that
    // changes nothing in the file (a sector size reported by the OS, or a
    // bootloader file offered with the image).
    QList<ByteRange> zones;
};

class Fixtures
{
public:
    static const QList<FixtureInfo> &all();
    static const FixtureInfo &info(FixtureId id);
    static std::optional<FixtureId> byName(const QString &name);

    // Writes the image, as FakePod::build does. Never throws.
    static FixtureBuild build(FixtureId id, const QString &path);

    static const QList<BootFileInfo> &bootFiles();
    static const BootFileInfo &bootFileInfo(BootFileId id);
    // The bytes of the file.
    static QByteArray bootFile(BootFileId id);

    // The fixture that fails only check `check` of section 11, or none. Checks 6
    // (a backup that cannot be made) and 8 (a real disk) are not about the
    // content of an image, so the tests of the checks set them up themselves.
    static std::optional<FixtureId> singleCheck(int check);
};

} // namespace tacetos::fakepod
