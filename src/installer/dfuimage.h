// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QString>

// The DFU image of the iPod Classic and the bootloader file it carries
// (docs/specs/bootloader-classic.md, sections 8 and 9). Nothing here talks to USB.

namespace tacetos::installer {

// The inputs that are not ours to write: two fixed blocks and the two programs
// that run on the iPod. They are built by CI from the pinned base sources, never
// stored in the repository, and loaded from a folder.
struct DfuParts
{
    // 16 bytes, at offset 0x40 of the header (section 8.2).
    QByteArray headerSignature;
    // 698 bytes, at offset 0x50 (section 8.1).
    QByteArray certificate;
    // The installer program; its last 0x50 bytes are the bootloader header.
    QByteArray installerPayload;
    QByteArray uninstallerPayload;
};

struct DfuPartsLoad
{
    bool ok = false;
    // In English, when `ok` is false.
    QString message;
    DfuParts parts;
};

// The folder given to CMake as TACETOS_DFU_PARTS_DIR, empty when there is none.
QString builtInDfuPartsDir();

// Reads the four files of `dir` and checks their sizes. An empty `dir` gives
// "Classic install is not available in this build".
DfuPartsLoad loadDfuParts(const QString &dir);

enum class ClassicBootStatus {
    Ok,
    TooShort,    // check 1
    WrongModel,  // check 2
    BadChecksum, // check 3
    TooBig,      // check 4
};

struct ClassicBootCheck
{
    ClassicBootStatus status = ClassicBootStatus::TooShort;
    QString message;
    // The body, without the 8-byte prefix; set when the status is `Ok`.
    QByteArray body;

    bool ok() const { return status == ClassicBootStatus::Ok; }
};

// The size of the installer image for a payload and a body of these lengths
// (section 8.3). The body is padded to a multiple of 16. 64-bit, so that a huge
// body cannot wrap around.
qint64 installerImageSize(qint64 payloadLength, qint64 bodyLength);

// Section 9: the four checks, in order, stopping at the first failure. The
// fourth one is the size of the image for an installer payload of
// `payloadLength` bytes.
ClassicBootCheck checkClassicBootloaderFile(const QByteArray &file, qint64 payloadLength);

// Reads a bootloader file once. A file larger than the image can ever be is
// refused from its size, before its content is read. `message` is set on failure.
ClassicBootCheck readClassicBootloaderFile(const QString &path, qint64 payloadLength);

struct DfuImage
{
    bool ok = false;
    QString message;
    // Without the 4 checksum bytes of section 8.5, which are added when sending.
    QByteArray image;
};

// Sections 8.1 to 8.3. The byte of the single boot is always 0; there is no way
// to ask for another value (rule 5). `file` is the content of the file, read once.
DfuImage buildInstallerImage(const DfuParts &parts, const QByteArray &bootloaderFile);

// Section 8.4: the first 0x30C bytes and the uninstaller program, nothing else.
DfuImage buildUninstallerImage(const DfuParts &parts);

// The same two, from the folder of CMake. Without it: "Classic install is not
// available in this build".
DfuImage buildClassicInstallerImage(const QByteArray &bootloaderFile,
                                    const QString &partsDir = builtInDfuPartsDir());
DfuImage buildClassicUninstallerImage(const QString &partsDir = builtInDfuPartsDir());

struct DfuSendCheck
{
    bool ok = false;
    QString message;
};

// What the helper runs on an installer image just before sending it (rule 5):
// the size, the tag, the bootloader header of section 8.3 read again in the image
// itself, and its single boot byte, which must be 0. `payloadLength` is the
// length of the installer program of the same parts.
DfuSendCheck checkInstallerImageReadyToSend(const QByteArray &image, qint64 payloadLength);

} // namespace tacetos::installer
