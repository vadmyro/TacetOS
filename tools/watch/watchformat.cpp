// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "watchformat.h"

#include "messages.h"

#include <QStringList>

namespace tacetos::watch {

using namespace tacetos::device;

namespace {

QString modeName(UsbMode mode)
{
    switch (mode) {
    case UsbMode::Normal:
        return QStringLiteral("normal");
    case UsbMode::Dfu:
        return QStringLiteral("dfu");
    case UsbMode::Other:
        return QStringLiteral("other");
    case UsbMode::None:
        break;
    }
    return QStringLiteral("none");
}

} // namespace

QString describe(const DetectedIpod &ipod, const QList<Model> &models)
{
    QStringList parts;
    parts << (ipod.model.isEmpty() ? QStringLiteral("model ?") : ipod.model);
    if (ipod.model.isEmpty() && !ipod.candidates.isEmpty())
        parts << QStringLiteral("(%1)").arg(ipod.candidates.join(QLatin1Char('|')));
    parts << ipod.usbId << modeName(ipod.mode);
    if (!ipod.serial.isEmpty())
        parts << QStringLiteral("serial %1").arg(ipod.serial);
    parts << QStringLiteral("\"%1\"").arg(stateMessage(ipod, models));
    if (ipod.volume) {
        // The card hides an empty line; the log keeps a word for it.
        const QString firmware = firmwareMessage(ipod);
        parts << QStringLiteral("\"%1\"").arg(firmware.isEmpty() ? QStringLiteral("unknown")
                                                                 : firmware);
    }
    if (ipod.disk)
        parts << ipod.disk->node;
    if (ipod.volume)
        parts << ipod.volume->mountPoint;
    return parts.join(QLatin1Char(' '));
}

void ChangeFilter::shown(const QString &portPath, const QString &text)
{
    last_.insert(portPath, text);
}

bool ChangeFilter::isVisibleChange(const QString &portPath, const QString &text)
{
    const auto it = last_.constFind(portPath);
    const bool visible = it == last_.constEnd() || *it != text;
    last_.insert(portPath, text);
    return visible;
}

void ChangeFilter::removed(const QString &portPath)
{
    last_.remove(portPath);
}

} // namespace tacetos::watch
