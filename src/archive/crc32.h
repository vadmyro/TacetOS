// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QtGlobal>

// The CRC-32 of the zip format: the polynomial of IEEE 802.3, with the bits
// taken lowest first, started from all ones and turned over at the end.

namespace tacetos::archive {

class Crc32
{
public:
    void add(const char *data, qint64 size);
    void add(const QByteArray &bytes) { add(bytes.constData(), bytes.size()); }
    quint32 value() const { return ~m_state; }

private:
    quint32 m_state = 0xFFFFFFFFu;
};

quint32 crc32(const QByteArray &bytes);

} // namespace tacetos::archive
