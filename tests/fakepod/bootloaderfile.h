// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

// Synthetic bootloader files for the tests of the installer
// (docs/specs/bootloader-legacy.md, section 8): an 8-byte header (checksum,
// big-endian, then a 4-letter model tag) followed by a program. The program is
// made of fake bytes; nothing here is a real bootloader.

namespace tacetos::fakepod {

// One row of the table of section 8.
struct BootTarget
{
    const char *target;
    // The model tag of the file header.
    const char *tag;
    // The model number added to the checksum.
    uint32_t number;
    // The value of section 5 (OSOS version >> 8) of the iPod this file is for.
    uint32_t osValue;
};

class BootloaderFile
{
public:
    static QList<BootTarget> targets();
    // Null when the name is not one of the nine.
    static const BootTarget *target(const QString &name);
    // The target of a value of section 5. Empty for a value with no row.
    static QString targetForOsValue(uint32_t osValue);

    // The program alone, `length` bytes (4000 when 0). For the 2nd generation
    // nano a hash block of 0x800 bytes comes first, then the program (7.5).
    static QByteArray program(const QString &target, int length = 0);

    // Section 8: the number of the model plus the byte sum of the program,
    // modulo 2^32.
    static quint32 checksum(quint32 modelNumber, const QByteArray &program);

    // A header followed by the program, with any checksum and tag.
    static QByteArray assemble(quint32 checksum, const QByteArray &tag, const QByteArray &program);

    // A good file for the target: right checksum, right tag. Empty for an
    // unknown target.
    static QByteArray build(const QString &target);

    static constexpr int kDefaultProgramLength = 4000;
    static constexpr int kHashBlock = 0x800;
};

} // namespace tacetos::fakepod
