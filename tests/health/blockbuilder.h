// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Builds identify and SMART blocks from the public layout of
// docs/specs/health.md section 5.1, for the tests. Every value is invented.

namespace testblocks {

struct Entry
{
    int id = 0;
    int current = 100;
    int worst = 100;
    quint64 raw = 0;
};

inline int byteSum(const QByteArray &bytes)
{
    int total = 0;
    for (const char c : bytes)
        total += quint8(c);
    return total % 256;
}

// Sets the byte at `at` so that the block adds up to 0 modulo 256.
inline void fixChecksum(QByteArray &block, int at)
{
    block[at] = 0;
    block[at] = char((256 - byteSum(block)) % 256);
}

// Writes `text` in `words` words from `first`, the high byte of each word
// first, padded with `pad`.
inline void putString(QByteArray &block, int first, int words, const QByteArray &text,
                      char pad = ' ')
{
    QByteArray raw = text.leftJustified(2 * words, pad, true);
    for (int i = 0; i < words; ++i) {
        block[2 * (first + i)] = raw.at(2 * i + 1);
        block[2 * (first + i) + 1] = raw.at(2 * i);
    }
}

enum class Checksum { Right, Wrong, Absent };

inline QByteArray identify(const QByteArray &model, const QByteArray &serial,
                           const QByteArray &revision, Checksum checksum = Checksum::Right)
{
    QByteArray block(512, '\0');
    putString(block, 10, 10, serial);
    putString(block, 23, 4, revision);
    putString(block, 27, 20, model);
    if (checksum != Checksum::Absent) {
        block[510] = char(0xa5);
        fixChecksum(block, 511);
        if (checksum == Checksum::Wrong)
            block[511] = char(quint8(block.at(511)) + 1);
    }
    return block;
}

// The entries in slots 0, 1, ...; an entry with id 0 is an empty slot.
inline QByteArray smart(const QList<Entry> &entries, bool rightChecksum = true)
{
    QByteArray block(512, '\0');
    block[0] = 16;
    for (int i = 0; i < entries.size(); ++i) {
        const Entry &e = entries.at(i);
        const int at = 2 + 12 * i;
        block[at] = char(e.id);
        block[at + 1] = 0x32;
        block[at + 3] = char(e.current);
        block[at + 4] = char(e.worst);
        for (int b = 0; b < 6; ++b)
            block[at + 5 + b] = char((e.raw >> (8 * b)) & 0xff);
    }
    fixChecksum(block, 511);
    if (!rightChecksum)
        block[511] = char(quint8(block.at(511)) + 1);
    return block;
}

inline QString hex(const QByteArray &block)
{
    return QString::fromLatin1(block.toHex());
}

} // namespace testblocks
