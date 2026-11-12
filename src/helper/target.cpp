// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "target.h"

#include "helperconstants.h"
#include "protocol.h"

#include <QFileInfo>

namespace tacetos::helper {

static ResolvedTarget bad(const QString &detail)
{
    ResolvedTarget t;
    t.code = err::badTarget;
    t.detail = detail;
    return t;
}

ResolvedTarget resolveTarget(const QJsonObject &target)
{
    if (target.value(QStringLiteral("kind")).toString() != QLatin1String("image"))
        return bad(QStringLiteral("a real disk is not supported by this build yet"));

    const QString given = target.value(QStringLiteral("path")).toString();
    qint64 sectorSize = 0;
    isInteger(target.value(QStringLiteral("sectorSize")), &sectorSize);
    bool sizeListed = false;
    for (const int s : kImageSectorSizes)
        sizeListed = sizeListed || s == sectorSize;
    if (!sizeListed)
        return bad(QStringLiteral("the sector size of an image is 512, 2048 or 4096"));

    // Links are followed; what they lead to must be a regular file.
    const QFileInfo info(given);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return bad(QStringLiteral("the image file does not exist: %1").arg(given));
    const QFileInfo real(canonical);
    if (!real.isFile())
        return bad(QStringLiteral("the image is not a regular file: %1").arg(canonical));
#if !defined(Q_OS_WIN)
    if (canonical.startsWith(QLatin1String("/dev/")))
        return bad(QStringLiteral("a file under /dev is not an image: %1").arg(canonical));
#endif
    const qint64 size = real.size();
    if (size == 0 || size % sectorSize != 0)
        return bad(QStringLiteral("the size of the image is not a whole number of sectors"));

    ResolvedTarget t;
    t.ok = true;
    t.isImage = true;
    t.path = canonical;
    t.sectorSize = int(sectorSize);
    t.lockKey = QStringLiteral("image:") + canonical;
    return t;
}

} // namespace tacetos::helper
