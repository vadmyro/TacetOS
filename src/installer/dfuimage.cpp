// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "dfuimage.h"

#include "bootfile.h"
#include "constants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tacetos::installer {

namespace {

void putLe32(QByteArray &b, int offset, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[offset + i] = char((v >> (8 * i)) & 0xFF);
}

quint32 getLe32(const QByteArray &b, int offset)
{
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | quint8(b.at(offset + i));
    return v;
}

qint64 padded(qint64 length)
{
    return (length + kBodyAlignment - 1) / kBodyAlignment * kBodyAlignment;
}

ClassicBootCheck refuse(ClassicBootStatus status, const char *text)
{
    ClassicBootCheck c;
    c.status = status;
    c.message = QString::fromLatin1(text);
    return c;
}

DfuImage failure(const QString &message)
{
    DfuImage r;
    r.message = message;
    return r;
}

// Section 8.1: the first 0x30C bytes, the same in every image.
QByteArray imagePrefix(const DfuParts &parts)
{
    QByteArray b(kDfuPrefixBytes, '\0');
    b.replace(0, 4, QByteArray(kDfuChipName.data(), 4));
    b.replace(kDfuVersionOffset, 3, QByteArray(kDfuVersion.data(), 3));
    b[kDfuEncryptionOffset] = char(kDfuEncryptionRom);
    putLe32(b, kDfuSignatureOffsetField, kDfuSignatureOffsetValue);
    putLe32(b, kDfuCertificateOffsetField, kDfuCertificateOffsetValue);
    putLe32(b, kDfuCertificateSizeField, kDfuCertificateBytes);
    b.replace(kDfuSignatureField, kDfuSignatureBytes, parts.headerSignature);
    b.replace(kDfuHeaderBytes, kDfuCertificateBytes, parts.certificate);
    putLe32(b, kDfuEntryAddressOffset, kDfuEntryAddress);
    return b;
}

QString notAvailable()
{
    return QStringLiteral("Classic install is not available in this build");
}

bool readPart(const QDir &dir, QLatin1String name, QByteArray &into)
{
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    into = f.readAll();
    return true;
}

} // namespace

QString builtInDfuPartsDir()
{
    return QString::fromUtf8(TACETOS_DFU_PARTS_DIR);
}

DfuPartsLoad loadDfuParts(const QString &dir)
{
    DfuPartsLoad r;
    if (dir.isEmpty()) {
        r.message = notAvailable();
        return r;
    }
    const QDir d(dir);
    DfuParts p;
    if (!readPart(d, kDfuPartSignature, p.headerSignature) ||
        !readPart(d, kDfuPartCertificate, p.certificate) ||
        !readPart(d, kDfuPartInstaller, p.installerPayload) ||
        !readPart(d, kDfuPartUninstaller, p.uninstallerPayload)) {
        r.message = QStringLiteral("The generated parts of the Classic installer are missing");
        return r;
    }
    if (p.headerSignature.size() != kDfuSignatureBytes ||
        p.certificate.size() != kDfuCertificateBytes ||
        p.installerPayload.size() < kDfuHeaderBytes || p.installerPayload.size() % 4 != 0 ||
        p.uninstallerPayload.isEmpty()) {
        r.message =
            QStringLiteral("The generated parts of the Classic installer have a wrong size");
        return r;
    }
    r.ok = true;
    r.parts = p;
    return r;
}

qint64 installerImageSize(qint64 payloadLength, qint64 bodyLength)
{
    return kDfuPrefixBytes + payloadLength + (kBootBodyDistance - kDfuHeaderBytes) +
           padded(bodyLength);
}

ClassicBootCheck checkClassicBootloaderFile(const QByteArray &file, qint64 payloadLength)
{
    // Check 1: a prefix and at least one byte of body.
    if (file.size() < kBootFileHeaderBytes + 1)
        return refuse(ClassicBootStatus::TooShort, "The bootloader file is damaged.");
    // Check 2.
    if (file.mid(4, 4) != QByteArray(kClassicBootTag.data(), 4))
        return refuse(ClassicBootStatus::WrongModel,
                      "This file is a bootloader for another model, or not a bootloader.");
    // Check 3: the checksum is big-endian, the only such number of this part.
    quint32 header = 0;
    for (int i = 0; i < 4; ++i)
        header = (header << 8) | quint8(file.at(i));
    const QByteArray body = file.mid(kBootFileHeaderBytes);
    if (header != bootFileChecksum(kClassicBootModelNumber, body))
        return refuse(ClassicBootStatus::BadChecksum, "The bootloader file is damaged.");
    // Check 4.
    if (installerImageSize(payloadLength, body.size()) > kDfuMaxImageBytes)
        return refuse(ClassicBootStatus::TooBig, "The bootloader is too big for the iPod.");
    ClassicBootCheck c;
    c.status = ClassicBootStatus::Ok;
    c.body = body;
    return c;
}

ClassicBootCheck readClassicBootloaderFile(const QString &path, qint64 payloadLength)
{
    // No image can hold a file longer than the image itself.
    if (QFileInfo(path).size() > kDfuMaxImageBytes)
        return refuse(ClassicBootStatus::TooBig, "The bootloader is too big for the iPod.");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return refuse(ClassicBootStatus::TooShort, "The bootloader file cannot be read.");
    // Read once; everything after this works on these bytes.
    return checkClassicBootloaderFile(f.readAll(), payloadLength);
}

DfuImage buildInstallerImage(const DfuParts &parts, const QByteArray &bootloaderFile)
{
    if (parts.headerSignature.size() != kDfuSignatureBytes ||
        parts.certificate.size() != kDfuCertificateBytes ||
        parts.installerPayload.size() < kDfuHeaderBytes)
        return failure(
            QStringLiteral("The generated parts of the Classic installer have a wrong size"));
    const qint64 payloadLength = parts.installerPayload.size();

    // The size comes first, from the length of the file only.
    if (bootloaderFile.size() > kBootFileHeaderBytes &&
        installerImageSize(payloadLength, bootloaderFile.size() - kBootFileHeaderBytes) >
            kDfuMaxImageBytes)
        return failure(QStringLiteral("The bootloader is too big for the iPod."));
    const ClassicBootCheck c = checkClassicBootloaderFile(bootloaderFile, payloadLength);
    if (!c.ok())
        return failure(c.message);

    const qint64 paddedBody = padded(c.body.size());
    QByteArray payload = parts.installerPayload;
    // The bootloader header: the last 0x50 bytes of the program. These fields and
    // no others. The single boot byte is forced to 0 and has no parameter.
    const int h = payload.size() - kDfuHeaderBytes;
    payload.replace(h, 4, QByteArray(kDfuChipName.data(), 4));
    payload.replace(h + kDfuVersionOffset, 3, QByteArray(kDfuVersion.data(), 3));
    payload[h + kDfuEncryptionOffset] = char(kBootHeaderEncryption);
    putLe32(payload, h + kDfuDataSizeOffset, quint32(paddedBody));
    payload[h + kBootHeaderSingleBootByte] = 0;

    DfuImage r;
    r.image = imagePrefix(parts);
    r.image.append(payload);
    r.image.append(QByteArray(kBootBodyDistance - kDfuHeaderBytes, '\0'));
    r.image.append(c.body);
    r.image.append(QByteArray(paddedBody - c.body.size(), '\0'));
    r.ok = true;
    return r;
}

DfuImage buildUninstallerImage(const DfuParts &parts)
{
    if (parts.headerSignature.size() != kDfuSignatureBytes ||
        parts.certificate.size() != kDfuCertificateBytes || parts.uninstallerPayload.isEmpty())
        return failure(
            QStringLiteral("The generated parts of the Classic installer have a wrong size"));
    DfuImage r;
    r.image = imagePrefix(parts) + parts.uninstallerPayload;
    if (r.image.size() > kDfuPrefixBytes + kUninstallerMaxPayloadBytes)
        return failure(QStringLiteral("The uninstaller program is too big"));
    r.ok = true;
    return r;
}

DfuImage buildClassicInstallerImage(const QByteArray &bootloaderFile, const QString &partsDir)
{
    const DfuPartsLoad p = loadDfuParts(partsDir);
    if (!p.ok)
        return failure(p.message);
    return buildInstallerImage(p.parts, bootloaderFile);
}

DfuImage buildClassicUninstallerImage(const QString &partsDir)
{
    const DfuPartsLoad p = loadDfuParts(partsDir);
    if (!p.ok)
        return failure(p.message);
    return buildUninstallerImage(p.parts);
}

DfuSendCheck checkInstallerImageReadyToSend(const QByteArray &image, qint64 payloadLength)
{
    DfuSendCheck r;
    auto refuseSend = [&r](const char *text) {
        r.message = QString::fromLatin1(text);
        return r;
    };
    const qint64 headerAt = kDfuPrefixBytes + payloadLength - kDfuHeaderBytes;
    const qint64 bodyAt = headerAt + kBootBodyDistance;
    if (payloadLength < kDfuHeaderBytes || image.size() > kDfuMaxImageBytes ||
        image.size() <= bodyAt)
        return refuseSend("This is not an installer image of the right size.");
    if (image.left(4) != QByteArray(kDfuChipName.data(), 4))
        return refuseSend("This is not an installer image.");
    // The header the iPod will sign, read again in the image that goes out.
    const int h = int(headerAt);
    if (image.mid(h, 4) != QByteArray(kDfuChipName.data(), 4) ||
        quint8(image.at(h + kDfuEncryptionOffset)) != kBootHeaderEncryption ||
        qint64(getLe32(image, h + kDfuDataSizeOffset)) != image.size() - bodyAt ||
        (image.size() - bodyAt) % kBodyAlignment != 0)
        return refuseSend("This is not an installer image of the right layout.");
    if (image.at(h + kBootHeaderSingleBootByte) != 0)
        return refuseSend("This image would install without the double boot, so it is not sent.");
    r.ok = true;
    return r;
}

} // namespace tacetos::installer
