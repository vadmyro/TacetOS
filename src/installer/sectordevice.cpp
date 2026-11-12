// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "sectordevice.h"

namespace tacetos::installer {

IoResult readBytes(SectorDevice &dev, qint64 offset, qint64 length, QByteArray &out)
{
    out.clear();
    const qint64 sector = dev.sectorSize();
    if (sector <= 0 || offset < 0 || length < 0 || offset + length > dev.sizeBytes())
        return IoResult::failure(QStringLiteral("read outside the disk"));
    if (length == 0)
        return IoResult::success();
    const qint64 first = offset / sector;
    const qint64 last = (offset + length - 1) / sector;
    QByteArray raw;
    const IoResult r = dev.readSectors(first, last - first + 1, raw);
    if (!r)
        return r;
    out = raw.mid(int(offset - first * sector), int(length));
    return IoResult::success();
}

FileSectorDevice::FileSectorDevice(const QString &path, int sectorSize, Mode mode)
    : m_file(path), m_sectorSize(sectorSize), m_mode(mode)
{
}

IoResult FileSectorDevice::open()
{
    if (m_sectorSize <= 0)
        return IoResult::failure(QStringLiteral("bad sector size"));
    const auto flags = m_mode == Mode::ReadWrite ? QIODevice::ReadWrite : QIODevice::ReadOnly;
    if (!m_file.open(flags))
        return IoResult::failure(m_file.errorString());
    return IoResult::success();
}

qint64 FileSectorDevice::sectorCount() const
{
    return m_file.isOpen() ? m_file.size() / m_sectorSize : 0;
}

IoResult FileSectorDevice::readSectors(qint64 first, qint64 count, QByteArray &out)
{
    out.clear();
    if (!m_file.isOpen())
        return IoResult::failure(QStringLiteral("the disk is not open"));
    if (first < 0 || count < 0 || first + count > sectorCount())
        return IoResult::failure(QStringLiteral("read outside the disk"));
    if (!m_file.seek(first * m_sectorSize))
        return IoResult::failure(m_file.errorString());
    const qint64 bytes = count * m_sectorSize;
    QByteArray data = m_file.read(bytes);
    if (data.size() != bytes)
        return IoResult::failure(QStringLiteral("short read: %1").arg(m_file.errorString()));
    out = data;
    return IoResult::success();
}

IoResult FileSectorDevice::writeSectors(qint64 first, const QByteArray &data)
{
    if (!m_file.isOpen())
        return IoResult::failure(QStringLiteral("the disk is not open"));
    if (m_mode != Mode::ReadWrite)
        return IoResult::failure(QStringLiteral("the disk is open read-only"));
    if (data.size() % m_sectorSize != 0)
        return IoResult::failure(QStringLiteral("a write must be whole sectors"));
    const qint64 count = data.size() / m_sectorSize;
    if (first < 0 || first + count > sectorCount())
        return IoResult::failure(QStringLiteral("write outside the disk"));
    if (!m_file.seek(first * m_sectorSize) || m_file.write(data) != data.size())
        return IoResult::failure(m_file.errorString());
    return IoResult::success();
}

IoResult FileSectorDevice::flush()
{
    if (!m_file.isOpen())
        return IoResult::failure(QStringLiteral("the disk is not open"));
    if (m_mode == Mode::ReadWrite && !m_file.flush())
        return IoResult::failure(m_file.errorString());
    return IoResult::success();
}

} // namespace tacetos::installer
