// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "fddevice.h"

#if !defined(Q_OS_WIN)
#include <cerrno>
#include <cstring>
#include <unistd.h>
#if defined(Q_OS_LINUX)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

namespace tacetos::helper {

using installer::IoResult;

FdSectorDevice::FdSectorDevice(int fd, qint64 sizeBytes, bool writable)
    : m_fd(fd), m_size(sizeBytes), m_writable(writable)
{
#if defined(Q_OS_LINUX)
    // A file that is not a disk answers none of these: the given values stay.
    int logical = 0;
    if (ioctl(m_fd, BLKSSZGET, &logical) == 0 && logical > 0)
        m_sectorSize = logical;
    unsigned long long bytes = 0;
    if (ioctl(m_fd, BLKGETSIZE64, &bytes) == 0 && bytes > 0)
        m_size = qint64(bytes);
#endif
}

FdSectorDevice::~FdSectorDevice()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

IoResult FdSectorDevice::readSectors(qint64 first, qint64 count, QByteArray &out)
{
    out.clear();
    if (first < 0 || count < 0 || first + count > sectorCount())
        return IoResult::failure(QStringLiteral("read outside the disk"));
    QByteArray buffer(int(count * m_sectorSize), '\0');
    qint64 done = 0;
    while (done < buffer.size()) {
        const ssize_t n = ::pread(m_fd, buffer.data() + done, size_t(buffer.size() - done),
                                  off_t(first * m_sectorSize + done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return IoResult::failure(
                QStringLiteral("read error: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        done += n;
    }
    out = buffer;
    return IoResult::success();
}

IoResult FdSectorDevice::writeSectors(qint64 first, const QByteArray &data)
{
    if (!m_writable)
        return IoResult::failure(QStringLiteral("the disk was opened for reading only"));
    if (data.size() % m_sectorSize != 0 || first < 0 ||
        first + data.size() / m_sectorSize > sectorCount())
        return IoResult::failure(QStringLiteral("write outside the disk"));
    qint64 done = 0;
    while (done < data.size()) {
        const ssize_t n = ::pwrite(m_fd, data.constData() + done, size_t(data.size() - done),
                                   off_t(first * m_sectorSize + done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return IoResult::failure(QStringLiteral("write error: %1")
                                         .arg(QString::fromLocal8Bit(std::strerror(errno))));
        done += n;
    }
    return IoResult::success();
}

IoResult FdSectorDevice::flush()
{
    if (m_writable && ::fsync(m_fd) != 0 && errno != EINVAL)
        return IoResult::failure(
            QStringLiteral("flush error: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    return IoResult::success();
}

} // namespace tacetos::helper
#endif
