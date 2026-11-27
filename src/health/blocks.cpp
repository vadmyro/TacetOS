// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "blocks.h"

namespace tacetos::health {

namespace {

constexpr int kBlockBytes = 512;
constexpr int kSmartEntries = 30;
constexpr int kSmartEntryBytes = 12;

int sum(const QByteArray &bytes)
{
    int total = 0;
    for (const char c : bytes)
        total += quint8(c);
    return total % 256;
}

quint16 word(const QByteArray &bytes, int index)
{
    return quint16(quint8(bytes.at(2 * index)) | (quint8(bytes.at(2 * index + 1)) << 8));
}

// Words `first` to `last`, the high byte of each word first.
QString identifyString(const QByteArray &bytes, int first, int last)
{
    QByteArray text;
    for (int i = first; i <= last; ++i) {
        const quint16 w = word(bytes, i);
        text.append(char(w >> 8));
        text.append(char(w & 0xff));
    }
    qsizetype start = 0;
    qsizetype end = text.size();
    while (start < end && (text.at(start) == ' ' || text.at(start) == '\0'))
        ++start;
    while (end > start && (text.at(end - 1) == ' ' || text.at(end - 1) == '\0'))
        --end;
    return QString::fromLatin1(text.mid(start, end - start));
}

QString badBlock(const QString &hex)
{
    if (hex.size() != 2 * kBlockBytes)
        return QStringLiteral("%1 digits instead of %2").arg(hex.size()).arg(2 * kBlockBytes);
    return QStringLiteral("a character that is not a hexadecimal digit");
}

} // namespace

QByteArray blockBytes(const QString &hex)
{
    if (hex.size() != 2 * kBlockBytes)
        return QByteArray();
    for (const QChar c : hex) {
        const char16_t u = c.unicode();
        const bool digit =
            (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
        if (!digit)
            return QByteArray();
    }
    return QByteArray::fromHex(hex.toLatin1());
}

IdentifyBlock decodeIdentify(const QString &hex)
{
    IdentifyBlock block;
    const QByteArray bytes = blockBytes(hex);
    if (bytes.isEmpty()) {
        block.error = badBlock(hex);
        return block;
    }
    // The low byte of word 255.
    if (quint8(bytes.at(510)) == 0xa5) {
        if (sum(bytes) != 0) {
            block.error = QStringLiteral("the checksum is wrong");
            return block;
        }
        block.checksummed = true;
    }
    block.serial = identifyString(bytes, 10, 19);
    block.revision = identifyString(bytes, 23, 26);
    block.model = identifyString(bytes, 27, 46);
    block.ok = true;
    return block;
}

const SmartAttribute *SmartBlock::attribute(int id) const
{
    for (const SmartAttribute &a : attributes) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

std::optional<quint32> SmartBlock::raw(int id) const
{
    if (const SmartAttribute *a = attribute(id))
        return a->raw32();
    return std::nullopt;
}

std::optional<int> SmartBlock::temperature() const
{
    const SmartAttribute *a = attribute(kTemperatureId);
    if (!a)
        return std::nullopt;
    const int celsius = int(a->raw & 0xff);
    if (celsius < 1 || celsius > 99)
        return std::nullopt;
    return celsius;
}

SmartBlock decodeSmart(const QString &hex)
{
    SmartBlock block;
    const QByteArray bytes = blockBytes(hex);
    if (bytes.isEmpty()) {
        block.error = badBlock(hex);
        return block;
    }
    block.revision = word(bytes, 0);
    for (int i = 0; i < kSmartEntries; ++i) {
        const int at = 2 + i * kSmartEntryBytes;
        SmartAttribute a;
        a.id = quint8(bytes.at(at));
        if (a.id == 0)
            continue;
        a.flags = quint16(quint8(bytes.at(at + 1)) | (quint8(bytes.at(at + 2)) << 8));
        a.current = quint8(bytes.at(at + 3));
        a.worst = quint8(bytes.at(at + 4));
        for (int b = 5; b >= 0; --b)
            a.raw = (a.raw << 8) | quint8(bytes.at(at + 5 + b));
        block.attributes.append(a);
    }
    if (block.attributes.isEmpty()) {
        block.error = QStringLiteral("no attribute");
        return block;
    }
    block.checksumOk = sum(bytes) == 0;
    block.ok = true;
    return block;
}

} // namespace tacetos::health
