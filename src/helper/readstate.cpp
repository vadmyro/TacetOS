// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "readstate.h"

#include "checks.h"
#include "constants.h"
#include "firmware.h"
#include "helperconstants.h"
#include "recognise.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace tacetos::helper {

using namespace tacetos::installer;

QString makeScanToken(qint64 sizeBytes, int sectorSize, const QByteArray &sector0,
                      const QByteArray &firmwareSector, int usbBus, int usbDevice)
{
    auto hex = [](const QByteArray &b) {
        return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
    };
    const QString text = QStringLiteral("scan1|%1|%2|%3|%4|%5|%6")
                             .arg(sizeBytes)
                             .arg(sectorSize)
                             .arg(hex(sector0), hex(firmwareSector))
                             .arg(usbBus)
                             .arg(usbDevice);
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString scanTokenFor(SectorDevice &dev, const Recognition &, const ScanSnapshot &snap,
                     QString *error, int usbBus, int usbDevice)
{
    QByteArray sector0;
    if (!snap.valid) {
        const IoResult r = dev.readSectors(0, 1, sector0);
        if (!r) {
            if (error)
                *error = r.error;
            return {};
        }
    }
    return makeScanToken(dev.sizeBytes(), dev.sectorSize(), snap.valid ? snap.sector0 : sector0,
                         snap.valid ? snap.firmwareSector : QByteArray(), usbBus, usbDevice);
}

static QString tableName(const PartitionTable &t)
{
    if (t.status != TableStatus::Ok)
        return QStringLiteral("none");
    return t.kind == TableKind::Apm ? QStringLiteral("apple") : QStringLiteral("dos");
}

// A Classic: a DOS table with one FAT32 partition in the first slot, and no
// firmware partition.
static bool looksLikeClassic(const PartitionTable &t)
{
    if (t.status != TableStatus::Ok || t.kind != TableKind::Dos || t.slotList.isEmpty())
        return false;
    const Slot &first = t.slotList.first();
    if (first.type != kDosTypeFat32 && first.type != kDosTypeFat32Lba)
        return false;
    for (int i = 1; i < t.slotList.size(); ++i) {
        if (!t.slotList.at(i).isEmpty())
            return false;
    }
    return true;
}

static CommandOutcome failure(const QString &code, const QString &detail)
{
    CommandOutcome o;
    o.error = code;
    o.detail = detail;
    return o;
}

CommandOutcome readState(SectorDevice &dev, const LogFn &log, const StageFn &stage, int usbBus,
                         int usbDevice)
{
    stage(QStringLiteral("read"));
    const int sectorSize = dev.sectorSize();
    const Recognition rec = recognise(dev, true);

    if (rec.verdict == Verdict::ReadError)
        return failure(err::openFailed, rec.message);
    if (rec.verdict == Verdict::UnknownGeneration)
        return failure(err::unsupportedModel, rec.message);

    QJsonObject data;
    data.insert(QStringLiteral("sectorSize"), sectorSize);
    data.insert(QStringLiteral("sizeBytes"), double(dev.sizeBytes()));
    data.insert(QStringLiteral("table"), tableName(rec.table));

    QJsonArray slotArray;
    if (rec.table.status == TableStatus::Ok) {
        for (const Slot &s : rec.table.slotList) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), double(s.type));
            o.insert(QStringLiteral("start"), double(s.startBytes(sectorSize)));
            o.insert(QStringLiteral("size"), double(s.sizeBytes(sectorSize)));
            slotArray.append(o);
        }
    }
    data.insert(QStringLiteral("slots"), slotArray);

    const ScanSnapshot snap = takeScanSnapshot(dev, rec);
    QString tokenError;
    const QString token = scanTokenFor(dev, rec, snap, &tokenError, usbBus, usbDevice);
    if (token.isEmpty())
        return failure(err::openFailed, tokenError);
    data.insert(QStringLiteral("scanToken"), token);

    if (rec.verdict == Verdict::Recognised) {
        const FirmwareState fw = readFirmware(dev, rec);
        data.insert(QStringLiteral("method"), QStringLiteral("partition"));
        data.insert(QStringLiteral("target"), rec.target);
        QString state;
        switch (fw.state) {
        case State::Clean:
            state = QStringLiteral("clean");
            break;
        case State::Installed:
            state = QStringLiteral("installed");
            break;
        case State::Inconsistent:
            state = QStringLiteral("inconsistent");
            log(QStringLiteral("warning"), fw.message);
            break;
        case State::NotRead:
            return failure(err::openFailed, fw.message);
        }
        data.insert(QStringLiteral("state"), state);
        QJsonArray entries;
        for (const DirectoryEntry &e : fw.entryList) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), entryKindName(e.kind));
            o.insert(QStringLiteral("devOffset"), double(e.devOffset));
            o.insert(QStringLiteral("len"), double(e.length));
            o.insert(QStringLiteral("entryOffset"), double(e.entryOffset));
            entries.append(o);
        }
        data.insert(QStringLiteral("entries"), entries);
        return {true, data, {}, {}};
    }

    // Not an older iPod: a Classic, or a disk that is not recognised, which is still
    // a success (section 6.2).
    if (looksLikeClassic(rec.table)) {
        data.insert(QStringLiteral("method"), QStringLiteral("dfu"));
    } else {
        data.insert(QStringLiteral("method"), QStringLiteral("unknown"));
        log(QStringLiteral("info"), QStringLiteral("not recognised as an iPod: ") + rec.message);
    }
    return {true, data, {}, {}};
}

} // namespace tacetos::helper
