// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "usblist.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace tacetos::device {

namespace {

// First line of a small sysfs file, or an empty string.
QString readAttribute(const QDir &dir, const QString &name)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(256)).section(QLatin1Char('\n'), 0, 0).trimmed();
}

bool isHexId(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{4}$"));
    return re.match(s).hasMatch();
}

} // namespace

UsbList readLinuxUsbList(const QString &root)
{
    UsbList list;
    const QDir rootDir(root);
    if (!rootDir.exists()) {
        list.error = QStringLiteral("cannot read %1").arg(root);
        return list;
    }
    const QStringList names = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QDir dir(rootDir.filePath(name));
        const QString vendor = readAttribute(dir, QStringLiteral("idVendor"));
        const QString product = readAttribute(dir, QStringLiteral("idProduct"));
        if (!isHexId(vendor) || !isHexId(product))
            continue;
        UsbEntry entry;
        entry.usbId = (vendor + QLatin1Char(':') + product).toLower();
        entry.portPath = name;
        entry.serial = readAttribute(dir, QStringLiteral("serial"));
        list.entries.append(entry);
    }
    return list;
}

QString usbIdFromWindowsHardwareId(const QString &hardwareId)
{
    static const QRegularExpression re(
        QStringLiteral("^USB\\\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})(&REV_[0-9A-F]{4})?$"));
    const QRegularExpressionMatch m = re.match(hardwareId.trimmed().toUpper());
    if (!m.hasMatch())
        return {};
    return (m.captured(1) + QLatin1Char(':') + m.captured(2)).toLower();
}

QList<UsbEntry> windowsUsbEntries(const QList<WindowsDevice> &devices)
{
    QList<UsbEntry> entries;
    for (const WindowsDevice &device : devices) {
        for (const QString &id : device.hardwareIds) {
            const QString usbId = usbIdFromWindowsHardwareId(id);
            if (usbId.isEmpty())
                continue;
            entries.append({usbId, device.locationPath, device.serial});
            break;
        }
    }
    return entries;
}

UsbClassification classifyUsb(const QList<UsbEntry> &list, const QList<Model> &models)
{
    UsbClassification result;
    const QString vendorPrefix = QString::fromLatin1(kUsbVendorApple) + QLatin1Char(':');

    // The last entry of a port path wins. Walk backwards to find them.
    QList<UsbEntry> kept;
    QSet<QString> seenPorts;
    for (auto it = list.crbegin(); it != list.crend(); ++it) {
        if (!it->portPath.isEmpty() && seenPorts.contains(it->portPath)) {
            result.log.prepend(QStringLiteral("port %1: %2 listed twice, kept the last")
                                   .arg(it->portPath, it->usbId));
            continue;
        }
        seenPorts.insert(it->portPath);
        kept.prepend(*it);
    }

    for (const UsbEntry &entry : std::as_const(kept)) {
        const QString usbId = entry.usbId.trimmed().toLower();
        if (!usbId.startsWith(vendorPrefix)) {
            ++result.otherVendors;
            continue;
        }
        const UsbMatch match = matchUsbId(models, usbId);
        if (match.candidates.isEmpty()) {
            result.log.append(
                QStringLiteral("port %1: Apple id %2 is in no model").arg(entry.portPath, usbId));
            continue;
        }
        UsbDevice device;
        device.usbId = usbId;
        device.mode = match.mode;
        device.candidates = match.candidates;
        device.portPath = entry.portPath;
        device.serial = entry.serial;
        result.devices.append(device);
    }
    return result;
}

} // namespace tacetos::device
