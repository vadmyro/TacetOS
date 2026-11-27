// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

// The identify block and the SMART block of a disk, as the health file gives
// them (docs/specs/health.md section 5.1): 512 bytes in 1024 hexadecimal
// digits, in the public layout of these blocks.

namespace tacetos::health {

// Section 5.2: the ids that are shown.
inline constexpr int kReallocatedId = 5;
inline constexpr int kPendingId = 197;
inline constexpr int kUncorrectableId = 198;
inline constexpr int kSpinRetriesId = 10;
inline constexpr int kTemperatureId = 194;
inline constexpr int kPowerOnHoursId = 9;
inline constexpr int kPowerCyclesId = 12;
inline constexpr int kHeadLoadsId = 193;
inline constexpr int kDropEventsId = 191;

// The bytes of a block, or an empty array when the text is not 1024
// hexadecimal digits.
QByteArray blockBytes(const QString &hex);

struct IdentifyBlock
{
    bool ok = false;
    QString model;
    QString serial;
    QString revision;
    // The block carries a checksum (low byte of word 255 is 0xA5), and it is
    // right.
    bool checksummed = false;
    // Why the block is unusable. Empty when ok.
    QString error;
};

// Words 27 to 46, 10 to 19 and 23 to 26, the high byte of each word first,
// spaces and NUL characters removed at both ends.
IdentifyBlock decodeIdentify(const QString &hex);

struct SmartAttribute
{
    int id = 0;
    quint16 flags = 0;
    int current = 0;
    int worst = 0;
    // The 6 bytes of the raw field.
    quint64 raw = 0;

    // The value used for every id but the temperature.
    quint32 raw32() const { return quint32(raw & 0xffffffffu); }
};

struct SmartBlock
{
    bool ok = false;
    int revision = 0;
    // The entries that are not empty, in the order of the block.
    QList<SmartAttribute> attributes;
    // A wrong checksum leaves the block usable: the caller logs it.
    bool checksumOk = false;
    QString error;

    // The first entry with this id, or null.
    const SmartAttribute *attribute(int id) const;
    // The low 32 bits of the raw field of `id`, when the disk reports it.
    std::optional<quint32> raw(int id) const;
    // Section 5.2: the low byte of the raw field of id 194, only from 1 to 99.
    std::optional<int> temperature() const;
};

// 30 entries of 12 bytes from byte 2. A block with no entry at all is
// unusable.
SmartBlock decodeSmart(const QString &hex);

} // namespace tacetos::health
