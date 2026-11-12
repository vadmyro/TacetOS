// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

// The disk as the installer sees it (docs/specs/bootloader-legacy.md, section
// 13.1): whole sectors in, whole sectors out, whatever the system. A disk
// image file and a real disk answer the same way; only `isRealDisk()` tells
// them apart, and the gate of section 11 check 8 looks at it.

namespace tacetos::installer {

struct IoResult
{
    bool ok = true;
    QString error;
    explicit operator bool() const { return ok; }
    static IoResult success() { return {}; }
    static IoResult failure(const QString &why)
    {
        IoResult r;
        r.ok = false;
        r.error = why;
        return r;
    }
};

class SectorDevice
{
public:
    virtual ~SectorDevice() = default;

    // The sector size the system reports (512 when it cannot say).
    virtual int sectorSize() const = 0;
    virtual qint64 sectorCount() const = 0;
    // True for a real disk, false for a disk image file.
    virtual bool isRealDisk() const = 0;

    // `count` whole sectors from sector `first`, into `out`. Past the end of the
    // disk is an error, never a short read.
    virtual IoResult readSectors(qint64 first, qint64 count, QByteArray &out) = 0;
    // `data` holds a whole number of sectors, written from sector `first`.
    virtual IoResult writeSectors(qint64 first, const QByteArray &data) = 0;
    // Asks the disk to store what was written, not only the cache of the system.
    virtual IoResult flush() = 0;

    qint64 sizeBytes() const { return sectorCount() * sectorSize(); }
};

// Bytes `[offset, offset + length)` of the disk, read through whole sectors
// (the sectors around the range are read and cut). An error when the range is
// not inside the disk.
IoResult readBytes(SectorDevice &dev, qint64 offset, qint64 length, QByteArray &out);

// A disk image file. The sector size is given by the caller: a file has none.
class FileSectorDevice : public SectorDevice
{
public:
    enum class Mode { ReadOnly, ReadWrite };

    FileSectorDevice(const QString &path, int sectorSize, Mode mode = Mode::ReadOnly);

    // Opens the file. The device is unusable until this has succeeded.
    IoResult open();

    int sectorSize() const override { return m_sectorSize; }
    qint64 sectorCount() const override;
    bool isRealDisk() const override { return false; }
    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override;
    IoResult writeSectors(qint64 first, const QByteArray &data) override;
    IoResult flush() override;

private:
    QFile m_file;
    int m_sectorSize;
    Mode m_mode;
};

} // namespace tacetos::installer
