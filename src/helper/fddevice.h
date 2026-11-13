// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "sectordevice.h"

// A disk seen through a file descriptor that the system gave (the file of
// `OpenDevice`, docs/specs/helper.md section 11.1). The helper never opens
// `/dev/sdX` by itself. Linux and macOS only.

namespace tacetos::helper {

#if !defined(Q_OS_WIN)
class FdSectorDevice : public installer::SectorDevice
{
public:
    // Takes the descriptor and closes it. The size of a disk that is not a file
    // is asked from the system; `sizeBytes` is used when it cannot answer.
    FdSectorDevice(int fd, qint64 sizeBytes, bool writable);
    ~FdSectorDevice() override;

    int sectorSize() const override { return m_sectorSize; }
    qint64 sectorCount() const override { return m_size / m_sectorSize; }
    bool isRealDisk() const override { return true; }
    installer::IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override;
    installer::IoResult writeSectors(qint64 first, const QByteArray &data) override;
    installer::IoResult flush() override;

private:
    int m_fd;
    qint64 m_size;
    int m_sectorSize = 512;
    bool m_writable;
};
#endif

} // namespace tacetos::helper
