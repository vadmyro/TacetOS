// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "udisks.h"

#include <QDBusConnection>

// The real UdisksBackend: calls to the disk service of the desktop over D-Bus.
// The calls wait for the answer, and a permission request of the system can
// make them last until the user answers. Call them from a worker thread, not
// from the thread of the user interface.

namespace tacetos::device {

inline constexpr char kUdisksService[] = "org.freedesktop.UDisks2";

class UdisksDBusBackend : public UdisksBackend
{
public:
    // By default the system bus and the real service. A test gives the
    // connection of a private bus and the name of a fake service.
    explicit UdisksDBusBackend(QDBusConnection bus = QDBusConnection::systemBus(),
                               QString service = QString::fromLatin1(kUdisksService));

    std::optional<UdisksObjects> managedObjects(UdisksReply *error) override;
    UdisksReply mount(const QString &blockPath, bool readOnly) override;
    UdisksReply unmount(const QString &blockPath) override;
    UdisksReply eject(const QString &drivePath) override;
    UdisksReply powerOff(const QString &drivePath) override;

private:
    UdisksReply call(const QString &path, const QString &interface, const QString &method,
                     const QVariantMap &options);

    QDBusConnection bus_;
    QString service_;
};

} // namespace tacetos::device
