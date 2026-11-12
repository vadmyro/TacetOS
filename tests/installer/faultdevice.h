// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "sectordevice.h"

#include <QList>

// A SectorDevice for the tests: it passes everything to another device, and
// can fail at the n-th write (a cable pulled out, a full disk). The tests of
// the writes use it to cut an operation after every write (P4.2f, P4.5).

namespace tacetos::installer {

class FaultSectorDevice : public SectorDevice
{
public:
    // `inner` is not owned and must outlive this object.
    explicit FaultSectorDevice(SectorDevice &inner) : m_inner(inner) {}

    // The `n`-th write from now (1 is the next one) fails with `message`, and
    // writes nothing. 0 means never.
    void failAtWrite(int n, const QString &message = QStringLiteral("the disk is gone"))
    {
        m_failAt = n;
        m_message = message;
        m_writes = 0;
    }
    // When set, the failing write puts the first half of its sectors on the disk
    // (at least one) before it fails: a write cut in the middle.
    void setTornWrite(bool torn) { m_torn = torn; }
    // When set (the default), every write and flush after the failing one fails
    // too, as with a disk that is unplugged.
    void setStaysFailed(bool stays) { m_stays = stays; }
    // What `isRealDisk()` answers, for the tests of the gate of section 11 check 8.
    void setRealDisk(bool real) { m_real = real; }

    // Writes that reached the inner device, including a torn one, as
    // (first sector, sector count).
    struct WriteRecord
    {
        qint64 first = 0;
        qint64 count = 0;
    };
    const QList<WriteRecord> &writeLog() const { return m_log; }
    // Writes asked for since `failAtWrite`, the failing one included.
    int writeCount() const { return m_writes; }
    int flushCount() const { return m_flushes; }
    bool hasFailed() const { return m_failed; }

    int sectorSize() const override { return m_inner.sectorSize(); }
    qint64 sectorCount() const override { return m_inner.sectorCount(); }
    bool isRealDisk() const override { return m_real; }
    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override
    {
        return m_inner.readSectors(first, count, out);
    }
    IoResult writeSectors(qint64 first, const QByteArray &data) override;
    IoResult flush() override;

private:
    SectorDevice &m_inner;
    int m_failAt = 0;
    int m_writes = 0;
    int m_flushes = 0;
    bool m_failed = false;
    bool m_torn = false;
    bool m_stays = true;
    bool m_real = false;
    QString m_message;
    QList<WriteRecord> m_log;
};

} // namespace tacetos::installer
