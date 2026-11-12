// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "bootfile.h"

#include "constants.h"

namespace tacetos::installer {

namespace {

// Section 8. The tag of the 2nd generation nano is the one of the tool that makes
// the files; section 27 leaves the choice open until a real build output exists.
const BootFileModel kModels[] = {
    {"ipod1g2g", "1g2g", 19},  {"ipod3g", "ip3g", 7},    {"ipodmini1g", "mini", 9},
    {"ipod4g", "ip4g", 8},     {"ipodcolor", "ipco", 3}, {"ipodmini2g", "mn2g", 11},
    {"ipodnano1g", "nano", 4}, {"ipodvideo", "ipvd", 5}, {"ipodnano2g", "nn2g", 62},
};

BootFileCheck refuse(BootFileCheck c, BootFileStatus status, const char *text)
{
    c.status = status;
    c.message = QStringLiteral("check 4: %1").arg(QString::fromLatin1(text));
    return c;
}

} // namespace

const BootFileModel *bootFileModel(const QString &target)
{
    for (const BootFileModel &m : kModels) {
        if (target == QLatin1String(m.target))
            return &m;
    }
    return nullptr;
}

quint32 bootFileChecksum(quint32 modelNumber, const QByteArray &program)
{
    quint32 sum = modelNumber;
    for (char c : program)
        sum += quint8(c);
    return sum;
}

BootFileCheck checkBootloaderFile(const QByteArray &file, const QString &target)
{
    BootFileCheck c;
    const BootFileModel *model = bootFileModel(target);
    if (!model)
        return refuse(c, BootFileStatus::UnknownTarget,
                      "there is no bootloader file for this model of iPod");
    if (file.size() < kBootFileHeaderBytes + 1)
        return refuse(c, BootFileStatus::TooShort, "the bootloader file is too short");

    // The checksum is the only big-endian number of this part.
    for (int i = 0; i < 4; ++i)
        c.headerChecksum = (c.headerChecksum << 8) | quint8(file.at(i));
    c.tag = file.mid(4, 4);
    const QByteArray program = file.mid(kBootFileHeaderBytes);

    if (c.tag != QByteArray(model->tag, 4))
        return refuse(c, BootFileStatus::WrongModel, "the bootloader file is for another model");
    if (c.headerChecksum != bootFileChecksum(model->number, program))
        return refuse(c, BootFileStatus::BadChecksum,
                      "the checksum of the bootloader file does not match");
    c.status = BootFileStatus::Ok;
    c.program = program;
    return c;
}

} // namespace tacetos::installer
