// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>

// Disk images that look like an older iPod, written as sparse files, for the
// tests of the installer (docs/specs/bootloader-legacy.md). Nothing here is
// real firmware: the banner, the images and the bootloader are synthetic.
//
// Layout of an image, in sectors (the sector size is 512, or 2048 for the
// 2nd generation nano):
//
//   0                  partition table (DOS, or Apple Partition Map)
//   kFirmwareStart     firmware partition, kFirmwareBytes long
//   after it           music partition, up to the end of the image
//
// The Classic is the exception: sectors of 4096 bytes, a DOS table with one
// FAT32 partition in slot 1 (from the same byte offset as the firmware
// partition of the others), and no firmware partition.
//
// The numbers of the spec that the tests check are written again in the tests
// themselves, not read from here.

namespace tacetos::fakepod {

enum class TableKind {
    Dos, // Windows iPod: DOS table, FAT32 music partition
    Apm, // Mac iPod: Apple Partition Map, HFS+ music partition
};

// What to build. Only `modelId` and `path` are required.
struct FakePodSpec
{
    // One of the ids of models/models.toml.
    QString modelId;
    // Where the image is written. An existing file is replaced.
    QString path;
    TableKind table = TableKind::Dos;
    // The bootloader is already installed (spec section 7.4, and 7.5 for the
    // 2nd generation nano).
    bool bootloaderPresent = false;
    // Extra entries after the OSOS one: resources and updater (section 7.2).
    bool withRsrc = false;
    bool withAupd = false;
    // The program of the bootloader, when it is present. Empty: a synthetic
    // one (defaultBootloader()). For the 2nd generation nano the program does
    // not include the hash block, which the builder adds in front (7.5).
    QByteArray bootloader;
    // The directory is one sector late (section 7.2, the 2nd generation nano
    // case): the first byte at the computed position is zero and the real
    // directory starts at the next sector.
    bool directoryLate = false;
};

// What a model needs. Facts of the spec, one row per id of models.toml.
struct ModelRow
{
    const char *id;
    // The value of section 5: the version of the OSOS entry shifted right by
    // 8 bits. 0xFF for the Classic, which is in no row of that table.
    uint32_t osValue;
    // Format version at 0x10A of the partition (section 7.1).
    uint16_t formatVersion;
    int sectorSize;
    // A Mac version exists (an Apple Partition Map and HFS+).
    bool hasMac;
    // A firmware partition in slot 1, then the music partition in slot 2. A
    // real Classic has neither: a single FAT32 partition in slot 1 (see
    // detection.md section 6.1), so its image has no banner and no directory.
    bool hasFirmware = true;
};

struct FakePodResult
{
    bool ok = false;
    QString error;
    // Where things are, so that a caller can read the image back. All in bytes
    // from the start of the file, except the entries.
    int sectorSize = 0;
    qint64 fileSize = 0;
    qint64 firmwareStart = 0;
    qint64 firmwareBytes = 0;
    qint64 musicStart = 0;
    qint64 musicBytes = 0;
    // Position of the directory entries inside the firmware partition (after
    // the 0x104 pointer and the late-directory shift), and of the image area.
    qint64 directoryOffset = 0;
    qint64 imageArea = 0;
};

class FakePod
{
public:
    // Writes the image described by `spec`. Never throws; look at `ok`.
    static FakePodResult build(const FakePodSpec &spec);

    static QStringList modelIds();
    // Null when the id is not one of the ten.
    static const ModelRow *model(const QString &id);

    // The synthetic program put in an image whose bootloader is present, when
    // the spec gives none.
    static QByteArray defaultBootloader();

    // The 256 bytes of the banner of section 4, built from its description.
    static QByteArray banner();

    // Fixed sizes of this library.
    static constexpr qint64 kImageBytes = 64 * 1024 * 1024;
    static constexpr qint64 kFirmwareStartSector = 64;
    static constexpr qint64 kFirmwareBytes = 8 * 1024 * 1024;
    // The sizes of the synthetic images (not multiples of a sector, on purpose).
    static constexpr int kOsosLength = 0x8064;
    static constexpr int kRsrcLength = 5000;
    static constexpr int kAupdLength = 3000;
    // Where the OSOS image starts, in bytes from the start of the firmware
    // partition.
    static constexpr int kOsosStart = 0x4000;
};

// The byte sum of section 7.2: unsigned 8-bit values, modulo 2^32.
uint32_t byteSum(const QByteArray &data);

} // namespace tacetos::fakepod
