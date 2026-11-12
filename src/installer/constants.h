// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QLatin1String>
#include <cstdint>

// Every fixed value of docs/specs/bootloader-legacy.md section 25, in one
// file, with the names of the table. The names that the bootloader looks for
// on the iPod are not here: they live in src/device/constants.h.

namespace tacetos::installer {

// Section 2 and 2.1: the DOS partition table.
inline constexpr int kDosSignatureOffset = 510;
inline constexpr uint8_t kDosSignature0 = 0x55;
inline constexpr uint8_t kDosSignature1 = 0xAA;
inline constexpr int kDosTableOffset = 0x1BE;
inline constexpr int kDosTableEntries = 4;
inline constexpr int kDosEntrySize = 16;
inline constexpr uint8_t kDosTypeFirmware = 0x00;
inline constexpr uint8_t kDosTypeFat32 = 0x0B;
inline constexpr uint8_t kDosTypeFat32Lba = 0x0C;

// Section 2.2: the Apple Partition Map.
inline constexpr QLatin1String kApmHeaderMagic("ER");
inline constexpr QLatin1String kApmEntryMagic("PM");
inline constexpr int kApmBlockSizeOffset = 2;
inline constexpr int kApmEntryCountOffset = 4;
inline constexpr int kApmFirstBlockOffset = 8;
inline constexpr int kApmBlockCountOffset = 12;
inline constexpr int kApmTypeOffset = 48;
inline constexpr int kApmTypeLength = 32;
inline constexpr int kApmEntryMinBytes = 80;
inline constexpr QLatin1String kApmTypeFirmware("Apple_MDFW");
inline constexpr QLatin1String kApmTypeHfs("Apple_HFS");
inline constexpr int kApmMaxBlocks = 64;
inline constexpr int kMaxSlots = 4;

// Section 2.3: the type of the HFS partition in the common description.
inline constexpr uint32_t kTypeHfs = 0xFFFF;

// Section 4: the banner, 16 rows (the last one has 15 characters), then a zero
// byte. 256 bytes at the start of the firmware partition.
inline constexpr int kBannerRows = 16;
inline constexpr int kBannerLength = 256;
inline constexpr const char *kBannerRowText[kBannerRows] = {
    "{{~~  /-----\\   ", "{{~~ /       \\  ", "{{~~|         | ",  "{{~~| S T O P | ",
    "{{~~|         | ",  "{{~~ \\       /  ", "{{~~  \\-----/   ", "Copyright(C) 200",
    "1 Apple Computer",  ", Inc.----------",  "----------------",  "----------------",
    "----------------",  "----------------",  "----------------",  "---------------",
};

// Section 7.1: the volume header.
inline constexpr int kVolumeMagicOffset = 0x100;
inline constexpr QLatin1String kVolumeMagic("]ih[");
inline constexpr int kDirectoryPointerOffset = 0x104;
inline constexpr int kDirectoryPointerBase = 0x200;
inline constexpr int kFormatVersionOffset = 0x10A;

// Section 7.2: the directory.
inline constexpr int kDirectoryEntrySize = 40;
inline constexpr int kDirectoryMaxEntries = 10;
inline constexpr int kDirectoryMaxBytes = 400;
inline constexpr QLatin1String kDeviceTagAta("!ATA");
inline constexpr QLatin1String kDeviceTagNan("DNAN");
inline constexpr QLatin1String kTagOsos("soso");
inline constexpr QLatin1String kTagRsrc("crsr");
inline constexpr QLatin1String kTagAupd("dpua");
inline constexpr QLatin1String kTagOsbk("kbso");
inline constexpr QLatin1String kTagHibe("ebih");
inline constexpr int kEntryTypeOffset = 4;
inline constexpr int kEntryDevOffsetOffset = 12;
inline constexpr int kEntryLengthOffset = 16;
inline constexpr int kEntryEntryOffsetOffset = 24;
inline constexpr int kEntryChecksumOffset = 28;
inline constexpr int kEntryVersionOffset = 32;
inline constexpr int kEntryFlashOffset = 36;
inline constexpr uint32_t kFlashAddressUnused = 0xFFFFFFFF;

// Sections 7.5 and 15.
inline constexpr int kHashBlockBytes = 0x800;
inline constexpr int kSectorSizeNano2g = 2048;

// Section 8: the header of a bootloader file.
inline constexpr int kBootFileHeaderBytes = 8;

// docs/specs/bootloader-classic.md, sections 8 and 9: the DFU image of the iPod
// Classic. The two fixed blocks and the two programs for the iPod are not here:
// they are generated outside the repository (dfuimage.h).
inline constexpr int kDfuMaxImageBytes = 0x20000;
inline constexpr int kDfuHeaderBytes = 0x50;
inline constexpr int kDfuCertificateBytes = 698;
inline constexpr int kDfuSignatureBytes = 16;
inline constexpr int kDfuPrefixBytes = 0x30C;
inline constexpr int kDfuEntryAddressOffset = 0x306;
inline constexpr uint32_t kDfuEntryAddress = 0x2200030C;
inline constexpr QLatin1String kDfuChipName("8702");
inline constexpr QLatin1String kDfuVersion("1.0");
inline constexpr int kDfuVersionOffset = 4;
inline constexpr int kDfuEncryptionOffset = 7;
inline constexpr uint8_t kDfuEncryptionRom = 3;
inline constexpr int kDfuDataSizeOffset = 0x0C;
inline constexpr int kDfuSignatureOffsetField = 0x10;
inline constexpr uint32_t kDfuSignatureOffsetValue = 0x300;
inline constexpr int kDfuCertificateOffsetField = 0x14;
inline constexpr uint32_t kDfuCertificateOffsetValue = 0xFFFFF850;
inline constexpr int kDfuCertificateSizeField = 0x18;
inline constexpr int kDfuSignatureField = 0x40;
// The header the installer program signs and writes to the NOR (section 8.3).
inline constexpr uint8_t kBootHeaderEncryption = 2;
inline constexpr int kBootHeaderSingleBootByte = 0x40;
// The body starts this far after the start of the bootloader header.
inline constexpr int kBootBodyDistance = 0x800;
inline constexpr int kBodyAlignment = 16;
// Section 14.2.
inline constexpr int kUninstallerMaxPayloadBytes = 16 * 1024;
// Section 9: the bootloader file of the Classic.
inline constexpr QLatin1String kClassicBootTag("ip6g");
inline constexpr uint32_t kClassicBootModelNumber = 71;
// The files of the folder of generated parts.
inline constexpr QLatin1String kDfuPartSignature("header-signature.bin");
inline constexpr QLatin1String kDfuPartCertificate("certificate.bin");
inline constexpr QLatin1String kDfuPartInstaller("installer-payload.bin");
inline constexpr QLatin1String kDfuPartUninstaller("uninstaller-payload.bin");

// Sections 2 and 4 to 7: the DFU protocol of the iPod Classic.
inline constexpr uint16_t kAppleVendorId = 0x05AC;
inline constexpr uint16_t kClassicDfuProductId = 0x1223;
// The iPod in normal mode: never a target of a DFU transfer.
inline constexpr uint16_t kClassicNormalProductId = 0x1261;
// The second step of a restore by Apple's software, one id per revision.
inline constexpr uint16_t kClassicWtfProductIds[] = {0x1241, 0x1245, 0x1247, 0x1250};
inline constexpr int kDfuBlockBytes = 2048;
inline constexpr uint8_t kDfuTypeOut = 0x21;
inline constexpr uint8_t kDfuTypeIn = 0xA1;
inline constexpr uint8_t kDfuDnload = 1;
inline constexpr uint8_t kDfuGetStatus = 3;
inline constexpr uint8_t kDfuClrStatus = 4;
inline constexpr uint8_t kDfuGetState = 5;
inline constexpr uint8_t kDfuAbort = 6;
inline constexpr int kDfuStatusBytes = 6;
inline constexpr int kDfuStateIdle = 2;
inline constexpr int kDfuStateDnBusy = 4;
inline constexpr int kDfuStateDnloadIdle = 5;
inline constexpr int kDfuStateManifest = 7;
inline constexpr int kDfuStateWaitReset = 8;
inline constexpr int kDfuStateError = 10;
inline constexpr int kDfuStatusErrVerify = 7;
inline constexpr int kDfuStatusErrFirmware = 10;
inline constexpr int kDfuRequestTimeoutMs = 500;
inline constexpr int kDfuBusyMaxStatusRequests = 20;
inline constexpr int kDfuPollMinMs = 1;
inline constexpr int kDfuPollMaxMs = 5000;

// Sections 15.1, 16.1 and 5 (steps 1 and 8): the waits around the transfer.
inline constexpr int kEjectRetryMs = 3000;
inline constexpr int kEjectMaxAttempts = 100;
inline constexpr int kEjectListedPollMs = 1000;
inline constexpr int kEjectListedTimeoutMs = 30000;
inline constexpr int kSpinDownMs = 18000;
inline constexpr int kDfuAppearPollMs = 1000;
inline constexpr int kDfuAppearTimeoutMs = 60000;
inline constexpr int kReleaseButtonsSeconds = 10;
inline constexpr int kMaxDfuResets = 3;
inline constexpr int kNormalModeWaitMs = 60000;
inline constexpr int kNormalModePollMs = 1000;
// Section 16.3.
inline constexpr int kMaxOneToneErrors = 2;

// Section 9, 14.1 and 20.4: memory held for one write, size of one piece.
inline constexpr qint64 kMaxWriteBytes = 8 * 1024 * 1024;
inline constexpr qint64 kPieceBytes = 8 * 1024 * 1024;

// Section 20: FAT32.
inline constexpr int kFatReservedSectors = 32;
inline constexpr int kFatCount = 2;
inline constexpr int kFatBootCopySector = 6;
inline constexpr int kFatFsInfoCopySector = 7;
inline constexpr uint32_t kFatMinClusters = 65525;
inline constexpr uint32_t kFatMaxClusters = 0x0FFFFFF5;
inline constexpr uint32_t kFatFirstEntry0 = 0x0FFFFFF8;
inline constexpr uint32_t kFatFirstEntry1 = 0x0FFFFFFF;
inline constexpr uint32_t kFatFirstEntry2 = 0x0FFFFFFF;
inline constexpr uint32_t kFsInfoSignature0 = 0x41615252;
inline constexpr uint32_t kFsInfoSignature1 = 0x61417272;
inline constexpr uint32_t kFsInfoSignature2 = 0xAA550000;
inline constexpr QLatin1String kDefaultVolumeLabel("IPOD");

// The banner as 256 bytes, built from its rows.
inline QByteArray bannerBytes()
{
    QByteArray b;
    for (const char *row : kBannerRowText)
        b.append(row);
    b.append('\0');
    return b;
}

} // namespace tacetos::installer
