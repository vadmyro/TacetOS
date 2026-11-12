// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "faultdevice.h"

namespace tacetos::installer {

IoResult FaultSectorDevice::writeSectors(qint64 first, const QByteArray &data)
{
    ++m_writes;
    const bool failsNow = (m_failAt > 0 && m_writes == m_failAt) || (m_failed && m_stays);
    if (!failsNow) {
        const IoResult r = m_inner.writeSectors(first, data);
        if (r)
            m_log.append({first, data.size() / sectorSize()});
        return r;
    }
    // A single sector is written whole or not at all: only a longer write can be
    // cut in the middle.
    const qint64 count = data.size() / sectorSize();
    if (!m_failed && m_torn && count > 1) {
        const qint64 half = count / 2;
        if (m_inner.writeSectors(first, data.left(int(half * sectorSize()))))
            m_log.append({first, half});
    }
    m_failed = true;
    return IoResult::failure(m_message);
}

IoResult FaultSectorDevice::flush()
{
    ++m_flushes;
    if (m_failed && m_stays)
        return IoResult::failure(m_message);
    return m_inner.flush();
}

} // namespace tacetos::installer
