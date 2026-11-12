// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "bootloaderfile.h"

#include "fakepod.h"

namespace tacetos::fakepod {

namespace {

// Section 8, with the value of section 5 of the iPod each file is for. The tag
// of the 2nd generation nano is `nn2g` (the one the tool that makes the files
// uses); section 27 leaves the choice open until a real build output exists.
const BootTarget kTargets[] = {
    {"ipod1g2g", "1g2g", 19, 0x01},    {"ipod3g", "ip3g", 7, 0x02},
    {"ipodmini1g", "mini", 9, 0x40},   {"ipod4g", "ip4g", 8, 0x50},
    {"ipodcolor", "ipco", 3, 0x60},    {"ipodmini2g", "mn2g", 11, 0x70},
    {"ipodnano1g", "nano", 4, 0xC0},   {"ipodvideo", "ipvd", 5, 0xB0},
    {"ipodnano2g", "nn2g", 62, 0x100},
};

QByteArray fake(int length, const char *tag, int seed)
{
    QByteArray b(length, '\0');
    for (int i = 0; i < length; ++i)
        b[i] = char((i * 197 + seed * 29 + (i >> 7)) & 0xFF);
    b.replace(0, int(qstrlen(tag)), tag);
    return b;
}

} // namespace

QList<BootTarget> BootloaderFile::targets()
{
    QList<BootTarget> l;
    for (const BootTarget &t : kTargets)
        l.append(t);
    return l;
}

const BootTarget *BootloaderFile::target(const QString &name)
{
    for (const BootTarget &t : kTargets) {
        if (name == QLatin1String(t.target))
            return &t;
    }
    return nullptr;
}

QString BootloaderFile::targetForOsValue(uint32_t osValue)
{
    for (const BootTarget &t : kTargets) {
        if (t.osValue == osValue)
            return QString::fromLatin1(t.target);
    }
    return QString();
}

QByteArray BootloaderFile::program(const QString &targetName, int length)
{
    const BootTarget *t = target(targetName);
    if (!t)
        return QByteArray();
    const int n = length > 0 ? length : kDefaultProgramLength;
    QByteArray p = fake(n, "FAKEPROG", int(t->number));
    if (t->osValue == 0x100)
        p.prepend(fake(kHashBlock, "FAKEHASH", int(t->number)));
    return p;
}

quint32 BootloaderFile::checksum(quint32 modelNumber, const QByteArray &program)
{
    return modelNumber + byteSum(program);
}

QByteArray BootloaderFile::assemble(quint32 sum, const QByteArray &tag, const QByteArray &program)
{
    QByteArray b(8, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char((sum >> (8 * (3 - i))) & 0xFF);
    b.replace(4, 4, tag.leftJustified(4, '\0', true));
    return b + program;
}

QByteArray BootloaderFile::build(const QString &targetName)
{
    const BootTarget *t = target(targetName);
    if (!t)
        return QByteArray();
    const QByteArray p = program(targetName);
    return assemble(checksum(t->number, p), QByteArray(t->tag), p);
}

} // namespace tacetos::fakepod
