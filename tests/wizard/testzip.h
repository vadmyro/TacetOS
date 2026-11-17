// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "crc32.h"

#include <QByteArray>
#include <QList>
#include <QPair>

// A zip of stored entries, made by the tests of the wizard for their packages.

namespace tacetos::wizard::testing {

inline void putLittle(QByteArray &out, quint64 value, int size)
{
    for (int i = 0; i < size; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

// Each entry is a name and its bytes. A name that ends with `/` is a folder.
inline QByteArray storedZip(const QList<QPair<QByteArray, QByteArray>> &entries)
{
    QByteArray out;
    QByteArray directory;
    for (const auto &[name, data] : entries) {
        const quint32 crc = archive::crc32(data);
        const quint64 offset = quint64(out.size());
        for (QByteArray *record : {&out, &directory}) {
            const bool local = record == &out;
            putLittle(*record, local ? 0x04034b50 : 0x02014b50, 4);
            if (!local)
                putLittle(*record, 20, 2);
            putLittle(*record, 20, 2);
            putLittle(*record, 0, 2);
            putLittle(*record, 0, 2);
            putLittle(*record, 0, 2);
            putLittle(*record, 0x21, 2);
            putLittle(*record, crc, 4);
            putLittle(*record, quint64(data.size()), 4);
            putLittle(*record, quint64(data.size()), 4);
            putLittle(*record, quint64(name.size()), 2);
            putLittle(*record, 0, 2);
            if (!local) {
                putLittle(*record, 0, 2);
                putLittle(*record, 0, 2);
                putLittle(*record, 0, 2);
                putLittle(*record, 0, 4);
                putLittle(*record, offset, 4);
            }
            *record += name;
            if (local)
                *record += data;
        }
    }
    const quint64 directoryOffset = quint64(out.size());
    out += directory;
    putLittle(out, 0x06054b50, 4);
    putLittle(out, 0, 4);
    putLittle(out, quint64(entries.size()), 2);
    putLittle(out, quint64(entries.size()), 2);
    putLittle(out, quint64(directory.size()), 4);
    putLittle(out, directoryOffset, 4);
    putLittle(out, 0, 2);
    return out;
}

} // namespace tacetos::wizard::testing
