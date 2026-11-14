// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "fixtures.h"

#include <QFile>
#include <functional>

namespace tacetos::fakepod {

namespace {

// Sizes of the images of this library, written out as numbers (the tests
// compare them with the tables of the images they read back). 64 MiB, firmware
// partition of 8 MiB from sector 64.
constexpr quint32 kFwFirst = 64;
constexpr quint32 kFwSectors512 = 16384;
constexpr quint32 kMusicSectors512 = 114624;
constexpr quint32 kFwSectors2048 = 4096;
constexpr quint32 kMusicSectors2048 = 28608;
constexpr quint32 kHfs = 0xFFFF;

// A change of bytes on top of the base image.
struct Patch
{
    qint64 offset;
    QByteArray bytes;
};
using Mutation = std::function<QList<Patch>(QFile &, const FakePodResult &)>;

struct Row
{
    FixtureInfo info;
    FakePodSpec spec;
    Mutation mutation;
};

QByteArray le32(quint32 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char((v >> (8 * i)) & 0xFF);
    return b;
}

quint32 readLe32(QFile &f, qint64 at)
{
    f.seek(at);
    const QByteArray b = f.read(4);
    quint32 v = 0;
    for (int i = 3; i >= 0 && b.size() == 4; --i)
        v = (v << 8) | quint8(b.at(i));
    return v;
}

qint64 dirAt(const FakePodResult &r)
{
    return r.firmwareStart + r.directoryOffset;
}

// The entry `i` of the directory, and a field of it (section 7.2).
qint64 entryAt(const FakePodResult &r, int i, int field = 0)
{
    return dirAt(r) + 40 * i + field;
}

constexpr int kType = 4, kDevOffset = 12, kLen = 16, kEntryOffset = 24, kChecksum = 28,
              kVersion = 32;

QStringList tagsFor(const FakePodSpec &s, bool nano2g)
{
    QStringList t{QStringLiteral("OSOS")};
    if (s.withRsrc)
        t << QStringLiteral("RSRC");
    if (s.withAupd)
        t << QStringLiteral("AUPD");
    if (s.bootloaderPresent && nano2g) {
        t.first() = QStringLiteral("OSBK");
        t << QStringLiteral("OSOS");
    }
    return t;
}

// The result for an image built from scratch, from the numbers of the spec.
Expected expectFor(const FakePodSpec &s)
{
    const ModelRow *m = FakePod::model(s.modelId);
    const bool nano2g = m->sectorSize == 2048;
    const quint32 fw = nano2g ? kFwSectors2048 : kFwSectors512;
    const quint32 music = nano2g ? kMusicSectors2048 : kMusicSectors512;
    Expected e;
    if (!m->hasFirmware) {
        // A single FAT32 partition in slot 1, sectors of 4096 bytes: slot 1 is not of
        // type 0, so the partition test of section 3 fails. The install method of
        // this model is `dfu`, which no reader of the legacy layout is asked about.
        constexpr quint32 first = kFwFirst * 512 / 4096;
        e.slotList = {
            {0x0B, first, 64 * 1024 * 1024 / 4096 - first}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        e.verdict = Verdict::PartitionTestFailed;
        e.message = QStringLiteral("partition layout is not the one of an iPod");
        return e;
    }
    if (s.table == TableKind::Dos) {
        e.slotList = {{0, kFwFirst, fw}, {0x0B, kFwFirst + fw, music}, {0, 0, 0}, {0, 0, 0}};
    } else {
        e.slotList = {{0, kFwFirst, fw}, {kHfs, kFwFirst + fw, music}};
    }
    e.target = BootloaderFile::targetForOsValue(m->osValue);
    if (e.target.isEmpty()) {
        e.verdict = Verdict::UnknownGeneration;
        e.message = QStringLiteral("an iPod we do not support");
        return e;
    }
    e.verdict = Verdict::Recognised;
    e.state = s.bootloaderPresent ? State::Installed : State::Clean;
    e.entries = tagsFor(s, nano2g);
    // See `Expected::warningCheck`.
    if (s.bootloaderPresent && !nano2g)
        e.warningCheck = -1;
    return e;
}

QString msgCheck(int n, const char *text)
{
    return QStringLiteral("check %1: %2").arg(n).arg(QString::fromLatin1(text));
}

const char *const kBadFileTooShort = "the bootloader file is too short";
const char *const kBadFileTag = "the bootloader file is for another model";
const char *const kBadFileSum = "the checksum of the bootloader file does not match";

QList<Row> makeRows()
{
    QList<Row> rows(static_cast<int>(FixtureId::Count));
    auto at = [&rows](FixtureId id) -> Row & { return rows[int(id)]; };

    auto scratch = [&](FixtureId id, const char *name, const char *what, const char *model,
                       TableKind table, bool boot, bool late = false) -> Row & {
        Row &fx = at(id);
        fx.spec.modelId = QString::fromLatin1(model);
        fx.spec.table = table;
        fx.spec.bootloaderPresent = boot;
        fx.spec.directoryLate = late;
        fx.info.id = id;
        fx.info.name = name;
        fx.info.what = what;
        fx.info.expected = expectFor(fx.spec);
        return fx;
    };
    // A copy of `base` with `mut` patched in. The caller changes the result.
    auto derive = [&](FixtureId id, const char *name, const char *what, FixtureId base,
                      Mutation mut) -> Row & {
        Row &fx = at(id);
        fx = at(base);
        fx.info.id = id;
        fx.info.name = name;
        fx.info.what = what;
        fx.info.base = base;
        fx.mutation = std::move(mut);
        return fx;
    };
    auto fixed = [](QList<Patch> p) { return [p](QFile &, const FakePodResult &) { return p; }; };
    auto refuse = [](Row &fx, int check, const char *text) {
        fx.info.expected.state = State::Inconsistent;
        fx.info.expected.firstFailingCheck = check;
        fx.info.expected.message = msgCheck(check, text);
    };

    // ---- one image per model value ------------------------------------------
    struct M
    {
        FixtureId dos;
        FixtureId mac;
        const char *id;
        const char *dosName;
        const char *macName;
    };
    const M models[] = {
        {FixtureId::DosIpod1g2g, FixtureId::MacIpod1g2g, "ipod-1g2g", "dos-ipod-1g2g",
         "mac-ipod-1g2g"},
        {FixtureId::DosIpod3g, FixtureId::MacIpod3g, "ipod-3g", "dos-ipod-3g", "mac-ipod-3g"},
        {FixtureId::DosIpod4g, FixtureId::MacIpod4g, "ipod-4g", "dos-ipod-4g", "mac-ipod-4g"},
        {FixtureId::DosIpodColor, FixtureId::MacIpodColor, "ipod-color", "dos-ipod-color",
         "mac-ipod-color"},
        {FixtureId::DosMini1g, FixtureId::MacMini1g, "mini-1g", "dos-mini-1g", "mac-mini-1g"},
        {FixtureId::DosMini2g, FixtureId::MacMini2g, "mini-2g", "dos-mini-2g", "mac-mini-2g"},
        {FixtureId::DosIpodVideo, FixtureId::MacIpodVideo, "ipod-video", "dos-ipod-video",
         "mac-ipod-video"},
        {FixtureId::DosNano1g, FixtureId::MacNano1g, "nano-1g", "dos-nano-1g", "mac-nano-1g"},
    };
    for (const M &m : models) {
        scratch(m.dos, m.dosName, "Windows iPod, clean", m.id, TableKind::Dos, false);
        scratch(m.mac, m.macName, "Mac iPod, clean", m.id, TableKind::Apm, false);
    }
    scratch(FixtureId::DosNano2g, "dos-nano-2g", "Windows iPod, 2048-byte sectors, clean",
            "nano-2g", TableKind::Dos, false);
    scratch(FixtureId::DosClassic6g, "dos-classic-6g",
            "Classic: one FAT32 partition in slot 1, sectors of 4096 bytes, no firmware partition",
            "classic-6g", TableKind::Dos, false);

    {
        Row &fx = derive(FixtureId::DosUnknownValue, "dos-unknown-value",
                         "OSOS version 0x3305: a value that section 5 does not have",
                         FixtureId::DosIpodVideo, [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{entryAt(r, 0, kVersion), le32(0x3305)}};
                         });
        fx.info.expected.verdict = Verdict::UnknownGeneration;
        fx.info.expected.target.clear();
        fx.info.expected.state = State::NotRead;
        fx.info.expected.entries.clear();
        fx.info.expected.message = QStringLiteral("an iPod we do not support");
    }
    {
        Row &fx = derive(FixtureId::Dos3gVersionZero, "dos-3g-version-zero",
                         "3rd generation OSOS version 0: no model (section 7.3)",
                         FixtureId::DosIpod3g, [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{entryAt(r, 0, kVersion), le32(0)}};
                         });
        fx.info.expected.verdict = Verdict::UnknownGeneration;
        fx.info.expected.target.clear();
        fx.info.expected.state = State::NotRead;
        fx.info.expected.entries.clear();
        fx.info.expected.message = QStringLiteral("an iPod we do not support");
    }

    // ---- section 26.1, broken tables ----------------------------------------
    const QByteArray zero2(2, '\0');
    auto notIpod = [](Row &fx, const char *msg, Verdict v) {
        fx.info.expected.verdict = v;
        fx.info.expected.target.clear();
        fx.info.expected.state = State::NotRead;
        fx.info.expected.entries.clear();
        fx.info.expected.message = QString::fromLatin1(msg);
    };
    notIpod(derive(FixtureId::DosNoSignature, "dos-no-signature", "bytes 510 and 511 are zero",
                   FixtureId::DosIpodVideo, fixed({{510, zero2}})),
            "unknown partition table", Verdict::UnknownTable);
    at(FixtureId::DosNoSignature).info.expected.slotList.clear();

    notIpod(derive(FixtureId::ApmNoPartitionEntry, "apm-no-partition-entry",
                   "`ER` header, then a block that does not start with `PM`",
                   FixtureId::MacIpodVideo, fixed({{512, zero2}})),
            "partition layout is not the one of an iPod", Verdict::PartitionTestFailed);
    at(FixtureId::ApmNoPartitionEntry).info.expected.slotList.clear();

    {
        Row &fx = derive(FixtureId::ApmTooManyKept, "apm-too-many-kept",
                         "a map of 7 entries, 6 of them kept: only 4 slots are read",
                         FixtureId::MacIpodVideo, [](QFile &f, const FakePodResult &) {
                             // Entries 4 to 7 are copies of the HFS entry, and
                             // every entry says that the map has 7.
                             f.seek(1536);
                             QByteArray hfs = f.read(512);
                             QList<Patch> p;
                             for (int n = 1; n <= 3; ++n)
                                 p.append({512 * n + 4, QByteArray("\0\0\0\7", 4)});
                             hfs.replace(4, 4, QByteArray("\0\0\0\7", 4));
                             for (int n = 4; n <= 7; ++n) {
                                 QByteArray e = hfs;
                                 e.replace(16, 1, QByteArray(1, char('0' + n)));
                                 p.append({512 * n, e});
                             }
                             return p;
                         });
        const QList<SlotExpect> s = fx.info.expected.slotList;
        fx.info.expected.slotList = {s[0], s[1], s[1], s[1]};
    }
    {
        Row &fx =
            derive(FixtureId::ApmHfsx, "apm-hfsx", "the music partition has the type Apple_HFSX",
                   FixtureId::MacIpodVideo, fixed({{1536 + 48, QByteArray("Apple_HFSX", 10)}}));
        notIpod(fx, "partition layout is not the one of an iPod", Verdict::PartitionTestFailed);
        fx.info.expected.slotList.removeLast();
    }
    {
        Row &fx = derive(FixtureId::DosNoBanner, "dos-no-banner",
                         "the first byte of the banner is changed", FixtureId::DosIpodVideo,
                         [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{r.firmwareStart, QByteArray("X", 1)}};
                         });
        notIpod(fx, "firmware partition does not carry the expected banner", Verdict::NoBanner);
    }
    {
        Row &fx = derive(FixtureId::DosSlot1NotZero, "dos-slot1-not-zero",
                         "the firmware partition has the type 0x83", FixtureId::DosIpodVideo,
                         fixed({{0x1BE + 4, QByteArray(1, char(0x83))}}));
        notIpod(fx, "partition layout is not the one of an iPod", Verdict::PartitionTestFailed);
        fx.info.expected.slotList[0].type = 0x83;
    }
    {
        Row &fx = derive(FixtureId::DosExtendedType, "dos-extended-type",
                         "the music partition has the type 0x05", FixtureId::DosIpodVideo,
                         fixed({{0x1BE + 16 + 4, QByteArray(1, char(0x05))}}));
        notIpod(fx, "partition layout is not the one of an iPod", Verdict::PartitionTestFailed);
        fx.info.expected.slotList[1].type = 0x05;
    }
    {
        Row &fx =
            derive(FixtureId::DosFat32Lba, "dos-fat32-lba",
                   "the music partition has the type 0x0C, which is accepted",
                   FixtureId::DosIpodVideo, fixed({{0x1BE + 16 + 4, QByteArray(1, char(0x0C))}}));
        fx.info.expected.slotList[1].type = 0x0C;
    }

    // ---- section 26.2, directories ------------------------------------------
    auto withDir = [&](FixtureId id, const char *name, const char *what, bool rsrc, bool aupd,
                       bool boot) -> Row & {
        Row &fx = scratch(id, name, what, "ipod-video", TableKind::Dos, boot);
        fx.spec.withRsrc = rsrc;
        fx.spec.withAupd = aupd;
        fx.info.expected = expectFor(fx.spec);
        return fx;
    };
    withDir(FixtureId::DosVideoRsrc, "dos-video-rsrc", "Video with an RSRC entry", true, false,
            false);
    withDir(FixtureId::DosVideoAupd, "dos-video-aupd", "Video with an AUPD entry", false, true,
            false);
    withDir(FixtureId::DosVideoRsrcAupd, "dos-video-rsrc-aupd", "Video with RSRC and AUPD entries",
            true, true, false);
    {
        Row &fx = derive(FixtureId::DosVideoUnknownTag, "dos-video-unknown-tag",
                         "the RSRC entry has a tag that no row of 7.2 has", FixtureId::DosVideoRsrc,
                         [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{entryAt(r, 1, kType), QByteArray("zzzz", 4)}};
                         });
        fx.info.expected.entries = {QStringLiteral("OSOS"), QStringLiteral("other")};
    }

    // Bootloader already added.
    const struct
    {
        FixtureId id;
        const char *name;
        const char *model;
    } installed[] = {
        {FixtureId::DosIpod1g2gInstalled, "dos-ipod-1g2g-installed", "ipod-1g2g"},
        {FixtureId::DosIpod3gInstalled, "dos-ipod-3g-installed", "ipod-3g"},
        {FixtureId::DosIpod4gInstalled, "dos-ipod-4g-installed", "ipod-4g"},
        {FixtureId::DosIpodColorInstalled, "dos-ipod-color-installed", "ipod-color"},
        {FixtureId::DosMini1gInstalled, "dos-mini-1g-installed", "mini-1g"},
        {FixtureId::DosMini2gInstalled, "dos-mini-2g-installed", "mini-2g"},
        {FixtureId::DosIpodVideoInstalled, "dos-ipod-video-installed", "ipod-video"},
        {FixtureId::DosNano1gInstalled, "dos-nano-1g-installed", "nano-1g"},
    };
    for (const auto &i : installed)
        scratch(i.id, i.name, "bootloader already appended (section 7.4)", i.model, TableKind::Dos,
                true);
    scratch(FixtureId::MacIpodVideoInstalled, "mac-ipod-video-installed",
            "Mac iPod, bootloader already appended", "ipod-video", TableKind::Apm, true);
    {
        Row &fx = withDir(FixtureId::DosVideoRsrcAupdInstalled, "dos-video-rsrc-aupd-installed",
                          "Video with RSRC and AUPD after the OSOS image, bootloader appended",
                          true, true, true);
        (void)fx;
    }
    scratch(FixtureId::DosNano2gInstalled, "dos-nano-2g-installed",
            "2nd generation nano: OSBK entry, bootloader as a new OSOS image (section 7.5)",
            "nano-2g", TableKind::Dos, true);
    scratch(FixtureId::DosNano2gLateDirectory, "dos-nano-2g-late-directory",
            "2nd generation nano, clean, directory one sector late (section 7.2)", "nano-2g",
            TableKind::Dos, false, true);
    scratch(FixtureId::DosNano2gInstalledLate, "dos-nano-2g-installed-late",
            "2nd generation nano with OSBK, directory one sector late", "nano-2g", TableKind::Dos,
            true, true);

    // ---- section 26.2, broken directories -----------------------------------
    {
        Row &fx = derive(FixtureId::DosBadMagic, "dos-bad-magic", "`]ih]` instead of `]ih[`",
                         FixtureId::DosIpodVideo, [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{r.firmwareStart + 0x103, QByteArray("]", 1)}};
                         });
        notIpod(fx, "bad firmware directory", Verdict::BadDirectory);
    }
    {
        Row &fx = derive(FixtureId::DosFormatVersion4, "dos-format-version-4",
                         "format version 4 at 0x10A", FixtureId::DosIpodVideo,
                         [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{r.firmwareStart + 0x10A, QByteArray("\4\0", 2)}};
                         });
        refuse(fx, 2, "the format version is not 2 or 3");
    }
    {
        Row &fx = derive(FixtureId::DosDirectoryOutside, "dos-directory-outside",
                         "the directory pointer at 0x104 points past the partition",
                         FixtureId::DosIpodVideo, [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{r.firmwareStart + 0x104, le32(0x00900000)}};
                         });
        notIpod(fx, "bad firmware directory", Verdict::BadDirectory);
    }
    {
        Row &fx =
            derive(FixtureId::DosTwoOsos, "dos-two-osos", "a second OSOS entry after the first",
                   FixtureId::DosIpodVideo, [](QFile &, const FakePodResult &r) {
                       const quint32 start = quint32(FakePod::kOsosStart + 0x8200 - r.imageArea);
                       QByteArray e(40, '\0');
                       e.replace(0, 4, "!ATA");
                       e.replace(4, 4, "soso");
                       e.replace(12, 4, le32(start));
                       e.replace(16, 4, le32(1000));
                       e.replace(32, 4, le32((0xB0u << 8) | 5));
                       return QList<Patch>{{entryAt(r, 1), e}};
                   });
        fx.info.expected.entries = {QStringLiteral("OSOS"), QStringLiteral("OSOS")};
        refuse(fx, 2, "the directory holds more than one OSOS entry");
    }
    {
        Row &fx = derive(FixtureId::DosOsosNotFirst, "dos-osos-not-first",
                         "the tags of the first two entries are swapped", FixtureId::DosVideoRsrc,
                         [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{entryAt(r, 0, kType), QByteArray("crsr", 4)},
                                                 {entryAt(r, 1, kType), QByteArray("soso", 4)}};
                         });
        fx.info.expected.entries = {QStringLiteral("RSRC"), QStringLiteral("OSOS")};
        refuse(fx, 2, "the OSOS entry is not the first one");
    }
    {
        Row &fx = derive(FixtureId::DosOverlap, "dos-overlap",
                         "the RSRC image starts inside the OSOS image", FixtureId::DosVideoRsrc,
                         [](QFile &f, const FakePodResult &r) {
                             const quint32 osos = readLe32(f, entryAt(r, 0, kDevOffset));
                             return QList<Patch>{{entryAt(r, 1, kDevOffset), le32(osos + 0x1000)}};
                         });
        refuse(fx, 3, "entries overlap");
    }
    {
        Row &fx =
            derive(FixtureId::DosEntryPastEnd, "dos-entry-past-end",
                   "the OSOS entry is as long as the whole partition", FixtureId::DosIpodVideo,
                   [](QFile &, const FakePodResult &r) {
                       return QList<Patch>{{entryAt(r, 0, kLen), le32(quint32(r.firmwareBytes))}};
                   });
        refuse(fx, 3, "an entry ends outside the firmware partition");
    }
    {
        Row &fx = derive(FixtureId::DosEntryOffsetUnaligned, "dos-entry-offset-unaligned",
                         "entryOffset is one more than a multiple of the sector size",
                         FixtureId::DosIpodVideoInstalled, [](QFile &f, const FakePodResult &r) {
                             const quint32 eo = readLe32(f, entryAt(r, 0, kEntryOffset));
                             return QList<Patch>{{entryAt(r, 0, kEntryOffset), le32(eo + 1)}};
                         });
        refuse(fx, 3, "entryOffset is not a multiple of the sector size");
    }
    {
        Row &fx = derive(FixtureId::DosEntryOffsetBeyondLen, "dos-entry-offset-beyond-len",
                         "entryOffset is a multiple of the sector size but larger than len",
                         FixtureId::DosIpodVideoInstalled, [](QFile &f, const FakePodResult &r) {
                             const quint32 len = readLe32(f, entryAt(r, 0, kLen));
                             return QList<Patch>{{entryAt(r, 0, kEntryOffset), le32(len + 512)}};
                         });
        refuse(fx, 3, "entryOffset is not lower than len");
    }
    {
        Row &fx = derive(FixtureId::DosSectorSize3000, "dos-sector-size-3000",
                         "the image is the clean Video; the OS reports 3000 bytes per sector",
                         FixtureId::DosIpodVideo, nullptr);
        notIpod(fx, "the sector size is not a power of two", Verdict::BadSectorSize);
        fx.info.expected.reportedSectorSize = 3000;
        fx.info.expected.slotList.clear();
    }

    // ---- section 26.2, one check broken -------------------------------------
    {
        Row &fx = derive(FixtureId::Check1DiskChanged, "check1-disk-changed",
                         "one byte of the first firmware sector (unused) differs from the scan",
                         FixtureId::DosIpodVideo, [](QFile &, const FakePodResult &r) {
                             return QList<Patch>{{r.firmwareStart + 0x1F0, QByteArray(1, '\1')}};
                         });
        fx.info.expected.scannedAs = FixtureId::DosIpodVideo;
        fx.info.expected.firstFailingCheck = 1;
        fx.info.expected.message = msgCheck(1, "the disk is not the one that was scanned");
    }
    auto badFile = [&](FixtureId id, const char *name, const char *what, BootFileId file,
                       const char *msg) {
        Row &fx = derive(id, name, what, FixtureId::DosIpodVideo, nullptr);
        fx.info.bootFile = file;
        fx.info.expected.firstFailingCheck = 4;
        fx.info.expected.message = msgCheck(4, msg);
    };
    badFile(FixtureId::Check4WrongByte, "check4-wrong-byte",
            "clean Video, a file with one wrong byte in the program", BootFileId::WrongByte,
            kBadFileSum);
    badFile(FixtureId::Check4WrongTag, "check4-wrong-tag",
            "clean Video, a file whose tag is no model", BootFileId::WrongTag, kBadFileTag);
    badFile(FixtureId::Check4TooShort, "check4-too-short", "clean Video, a file of 5 bytes",
            BootFileId::TooShort, kBadFileTooShort);
    badFile(FixtureId::Check4HeaderOnly, "check4-header-only",
            "clean Video, a file with no program", BootFileId::HeaderOnly, kBadFileTooShort);
    badFile(FixtureId::Check4OtherModelTag, "check4-other-model-tag",
            "clean Video, a file with the tag of another model and a checksum that passes",
            BootFileId::OtherModelTag, kBadFileTag);
    {
        Row &fx = derive(
            FixtureId::Check5NoRoom, "check5-no-room",
            "the AUPD image sits 512 bytes before the end of the partition, so the "
            "images cannot move",
            FixtureId::DosVideoRsrcAupd, [](QFile &f, const FakePodResult &r) {
                const qint64 old =
                    r.firmwareStart + r.imageArea + readLe32(f, entryAt(r, 2, kDevOffset));
                f.seek(old);
                const QByteArray data = f.read(FakePod::kAupdLength);
                const qint64 padded = (FakePod::kAupdLength + 511) / 512 * 512;
                const qint64 fresh = r.firmwareBytes - padded - 512;
                return QList<Patch>{{entryAt(r, 2, kDevOffset), le32(quint32(fresh - r.imageArea))},
                                    {r.firmwareStart + fresh, data}};
            });
        fx.info.expected.firstFailingCheck = 5;
        fx.info.expected.message = msgCheck(5, "not enough room in the firmware partition");
    }
    {
        Row &fx = derive(FixtureId::Check7WrongChecksum, "check7-wrong-checksum",
                         "the checksum of the OSOS entry is one too high: a warning, no refusal",
                         FixtureId::DosIpodVideo, [](QFile &f, const FakePodResult &r) {
                             const quint32 sum = readLe32(f, entryAt(r, 0, kChecksum));
                             return QList<Patch>{{entryAt(r, 0, kChecksum), le32(sum + 1)}};
                         });
        fx.info.expected.warningCheck = 7;
        fx.info.expected.message = msgCheck(7, "the firmware checksum differs from the directory");
    }
    return rows;
}

const QList<Row> &rows()
{
    static const QList<Row> r = makeRows();
    return r;
}

// ---- bootloader files ------------------------------------------------------

const char *const kGoodNames[] = {"good-ipod1g2g",   "good-ipod3g",    "good-ipodmini1g",
                                  "good-ipod4g",     "good-ipodcolor", "good-ipodmini2g",
                                  "good-ipodnano1g", "good-ipodvideo", "good-ipodnano2g"};
const char *const kGoodTargets[] = {"ipod1g2g",   "ipod3g",    "ipodmini1g",
                                    "ipod4g",     "ipodcolor", "ipodmini2g",
                                    "ipodnano1g", "ipodvideo", "ipodnano2g"};

QList<BootFileInfo> makeBootFiles()
{
    QList<BootFileInfo> l;
    for (int i = 0; i < 9; ++i)
        l.append({BootFileId(i), kGoodNames[i], kGoodTargets[i], true, QString()});
    l.append({BootFileId::WrongByte, "wrong-byte", "ipodvideo", false, msgCheck(4, kBadFileSum)});
    l.append({BootFileId::WrongTag, "wrong-tag", "ipodvideo", false, msgCheck(4, kBadFileTag)});
    l.append(
        {BootFileId::TooShort, "too-short", "ipodvideo", false, msgCheck(4, kBadFileTooShort)});
    l.append(
        {BootFileId::HeaderOnly, "header-only", "ipodvideo", false, msgCheck(4, kBadFileTooShort)});
    l.append({BootFileId::OtherModelTag, "other-model-tag", "ipodvideo", false,
              msgCheck(4, kBadFileTag)});
    return l;
}

} // namespace

const QList<FixtureInfo> &Fixtures::all()
{
    static const QList<FixtureInfo> l = [] {
        QList<FixtureInfo> out;
        for (const Row &r : rows())
            out.append(r.info);
        return out;
    }();
    return l;
}

const FixtureInfo &Fixtures::info(FixtureId id)
{
    return rows().at(int(id)).info;
}

std::optional<FixtureId> Fixtures::byName(const QString &name)
{
    for (const Row &r : rows()) {
        if (name == QLatin1String(r.info.name))
            return r.info.id;
    }
    return std::nullopt;
}

FixtureBuild Fixtures::build(FixtureId id, const QString &path)
{
    FixtureBuild out;
    if (int(id) < 0 || int(id) >= int(FixtureId::Count)) {
        out.error = QStringLiteral("no such fixture");
        return out;
    }
    const Row &row = rows().at(int(id));
    FakePodSpec spec = row.spec;
    spec.path = path;
    out.pod = FakePod::build(spec);
    if (!out.pod.ok) {
        out.error = out.pod.error;
        return out;
    }
    if (row.mutation) {
        QFile f(path);
        if (!f.open(QIODevice::ReadWrite)) {
            out.error = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
            return out;
        }
        const QList<Patch> patches = row.mutation(f, out.pod);
        for (const Patch &p : patches) {
            if (!f.seek(p.offset) || f.write(p.bytes) != p.bytes.size()) {
                out.error = QStringLiteral("cannot patch %1: %2").arg(path, f.errorString());
                return out;
            }
            out.zones.append({p.offset, p.bytes.size()});
        }
        if (!f.flush()) {
            out.error = QStringLiteral("write error on %1").arg(path);
            return out;
        }
    }
    out.ok = true;
    return out;
}

const QList<BootFileInfo> &Fixtures::bootFiles()
{
    static const QList<BootFileInfo> l = makeBootFiles();
    return l;
}

const BootFileInfo &Fixtures::bootFileInfo(BootFileId id)
{
    return bootFiles().at(int(id));
}

QByteArray Fixtures::bootFile(BootFileId id)
{
    const BootFileInfo &i = bootFileInfo(id);
    const QString target = QString::fromLatin1(i.target);
    const BootTarget *t = BootloaderFile::target(target);
    QByteArray good = BootloaderFile::build(target);
    switch (id) {
    case BootFileId::WrongByte:
        good[8 + 100] = char(good.at(8 + 100) ^ 0x01);
        return good;
    case BootFileId::WrongTag:
        return BootloaderFile::assemble(
            BootloaderFile::checksum(t->number, BootloaderFile::program(target)), "xxxx",
            BootloaderFile::program(target));
    case BootFileId::TooShort:
        return good.left(5);
    case BootFileId::HeaderOnly:
        return BootloaderFile::assemble(BootloaderFile::checksum(t->number, QByteArray()),
                                        QByteArray(t->tag), QByteArray());
    case BootFileId::OtherModelTag:
        return BootloaderFile::assemble(
            BootloaderFile::checksum(t->number, BootloaderFile::program(target)),
            QByteArray(BootloaderFile::target(QStringLiteral("ipod4g"))->tag),
            BootloaderFile::program(target));
    default:
        return good;
    }
}

std::optional<FixtureId> Fixtures::singleCheck(int check)
{
    switch (check) {
    case 1:
        return FixtureId::Check1DiskChanged;
    case 2:
        return FixtureId::DosFormatVersion4;
    case 3:
        return FixtureId::DosOverlap;
    case 4:
        return FixtureId::Check4WrongByte;
    case 5:
        return FixtureId::Check5NoRoom;
    case 7:
        return FixtureId::Check7WrongChecksum;
    default:
        return std::nullopt;
    }
}

} // namespace tacetos::fakepod
