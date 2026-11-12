// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "convert.h"
#include "fakepod.h"
#include "faultdevice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QList>
#include <QString>

// Helpers shared by the tests of the conversion and of the rules of section 24:
// iPod images whose music partition is big enough for FAT32, and hashes of a file
// outside the zones that an operation may write.

namespace support {

constexpr int kSector = 512;
// About 293 MiB: 75 000 clusters of 4 KiB, above the 65 525 that FAT32 needs.
constexpr qint64 kMusicTotal = 600000;

struct Range
{
    qint64 offset = 0;
    qint64 length = 0;
};

inline QByteArray readAll(const QString &path, qint64 offset = 0, qint64 length = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return length < 0 ? f.readAll() : f.read(length);
}

// The hash of the whole file except `allowed`, read in pieces (the file is sparse).
inline QByteArray hashOutside(const QString &path, QList<Range> allowed = {})
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    std::sort(allowed.begin(), allowed.end(),
              [](const Range &a, const Range &b) { return a.offset < b.offset; });
    QCryptographicHash h(QCryptographicHash::Sha256);
    qint64 at = 0;
    auto feed = [&](qint64 to) {
        while (at < to) {
            f.seek(at);
            const QByteArray part = f.read(qMin<qint64>(to - at, 4 * 1024 * 1024));
            if (part.isEmpty())
                break;
            h.addData(part);
            at += part.size();
        }
    };
    for (const Range &r : allowed) {
        feed(r.offset);
        at = qMax(at, r.offset + r.length);
    }
    feed(f.size());
    return h.result();
}

struct Img
{
    QString path;
    tacetos::fakepod::FakePodResult info;
    qint64 musicFirst = 0;
    qint64 musicTotal = 0;
    bool ok = false;
    Range firmware() const { return {info.firmwareStart, info.firmwareBytes}; }
    Range music() const { return {musicFirst * kSector, musicTotal * kSector}; }
};

inline void put32be(QByteArray &b, int at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((v >> (8 * (3 - i))) & 0xFF);
}

// An iPod image of `model` (512-byte sectors) whose music partition has
// `kMusicTotal` sectors, so that it can be formatted.
inline Img makeImg(const QString &dir, const QString &name, const QString &model,
                   tacetos::fakepod::TableKind table, bool installed = false, bool rsrc = false)
{
    namespace fp = tacetos::fakepod;
    Img i;
    fp::FakePodSpec spec;
    spec.modelId = model;
    spec.path = QDir(dir).filePath(name);
    spec.table = table;
    spec.bootloaderPresent = installed;
    spec.withRsrc = spec.withAupd = rsrc;
    i.path = spec.path;
    i.info = fp::FakePod::build(spec);
    if (!i.info.ok || i.info.sectorSize != kSector)
        return i;
    i.musicFirst = i.info.musicStart / kSector;
    i.musicTotal = kMusicTotal;
    QFile f(i.path);
    if (!f.open(QIODevice::ReadWrite) || !f.resize((i.musicFirst + i.musicTotal) * kSector))
        return i;
    QByteArray count(4, '\0');
    if (table == fp::TableKind::Apm) {
        put32be(count, 0, quint32(i.musicTotal));
        f.seek(1536 + 12);
    } else {
        for (int k = 0; k < 4; ++k)
            count[k] = char((quint32(i.musicTotal) >> (8 * k)) & 0xFF);
        f.seek(0x1BE + 16 + 12);
    }
    i.ok = f.write(count) == 4;
    return i;
}

inline tacetos::installer::ConvertRequest convertRequest(tacetos::installer::SectorDevice &dev,
                                                         const QString &backupDir)
{
    using namespace tacetos::installer;
    ConvertRequest r;
    r.ipodName = r.typedName = QStringLiteral("My iPod");
    r.slotUnmounted = true;
    r.allowRealWrites = false;
    r.scan = takeScanSnapshot(dev, recognise(dev));
    r.backupDir = backupDir;
    r.pieceBytes = 128 * 1024;
    return r;
}

} // namespace support
