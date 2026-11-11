// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "models.h"

#include <QList>
#include <QString>
#include <QStringList>

// Layer 1 of docs/specs/detection.md: the USB list. The readers (sections 3.1)
// turn what one system shows into UsbEntry values, and classifyUsb (section 4)
// decides, with no system access.

namespace tacetos::device {

// The vendor id of Apple, as in section 11.
inline constexpr char kUsbVendorApple[] = "05ac";

struct UsbEntry
{
    // "vendor:product", four lowercase hexadecimal digits each.
    QString usbId;
    QString portPath;
    // Empty when the system does not give one.
    QString serial;
    // The name strings, empty when the system does not keep them (section 3.1).
    QString manufacturer;
    QString product;
};

struct UsbList
{
    QList<UsbEntry> entries;
    // Empty when the list was read without a problem.
    QString error;
};

// An Apple device of a known id, with what the models say about the id.
struct UsbDevice
{
    QString usbId;
    UsbMode mode = UsbMode::None;
    QStringList candidates;
    QString portPath;
    QString serial;
    // The name strings of the entry, empty when unknown (section 3.1).
    QString manufacturer;
    QString product;
};

struct UsbClassification
{
    QList<UsbDevice> devices;
    // Devices of other vendors, dropped without a trace beyond this count.
    int otherVendors = 0;
    // One line per dropped Apple device or repeated port path.
    QStringList log;
};

// Linux (section 3.1): reads the folders of `root`, which is
// /sys/bus/usb/devices on a real system. A folder is a device when it holds
// valid idVendor and idProduct files; the others (interfaces, odd folders) are
// skipped. The port path is the folder name. Sorted by folder name.
UsbList readLinuxUsbList(const QString &root = QStringLiteral("/sys/bus/usb/devices"));

// Windows (section 3.1): the usb id of one hardware id string such as
// `USB\VID_05AC&PID_1261&REV_0001`. Case does not matter (old versions write
// `Vid_` and `Pid_`) and `&REV_` is optional. Empty when the string is not the
// hardware id of a whole USB device (interface ids with `&MI_` are not).
QString usbIdFromWindowsHardwareId(const QString &hardwareId);

// What the SetupAPI shows for one present device.
struct WindowsDevice
{
    // The "hardware id" property: a list of strings.
    QStringList hardwareIds;
    // The "location path" property.
    QString locationPath;
    QString serial;
};

// Keeps the devices that have a USB hardware id, the first that parses.
QList<UsbEntry> windowsUsbEntries(const QList<WindowsDevice> &devices);

// Section 4 steps 2 and 3. Drops other vendors, keeps the last of two entries
// with the same port path, and keeps the Apple devices that are in a model.
UsbClassification classifyUsb(const QList<UsbEntry> &list, const QList<Model> &models);

} // namespace tacetos::device
