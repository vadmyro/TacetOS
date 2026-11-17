// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "crc32.h"

#include <array>

namespace tacetos::archive {

namespace {

// The polynomial 0x04C11DB7, its bits in the other order.
constexpr quint32 kPolynomial = 0xEDB88320u;

// The remainder of each byte value, computed once by the compiler.
constexpr std::array<quint32, 256> remainders()
{
    std::array<quint32, 256> table{};
    for (quint32 byte = 0; byte < 256; ++byte) {
        quint32 r = byte;
        for (int bit = 0; bit < 8; ++bit)
            r = (r >> 1) ^ ((r & 1u) ? kPolynomial : 0u);
        table[byte] = r;
    }
    return table;
}

constexpr std::array<quint32, 256> kRemainders = remainders();

} // namespace

void Crc32::add(const char *data, qint64 size)
{
    quint32 state = m_state;
    for (qint64 i = 0; i < size; ++i)
        state = kRemainders[(state ^ quint8(data[i])) & 0xFFu] ^ (state >> 8);
    m_state = state;
}

quint32 crc32(const QByteArray &bytes)
{
    Crc32 crc;
    crc.add(bytes);
    return crc.value();
}

} // namespace tacetos::archive
