// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "livedriver.h"

#include <QSocketNotifier>
#include <QString>

// The udev monitor (docs/specs/detection.md section 9.1): kernel events on the
// usb and block subsystems, read through libudev. It only exists on Linux when
// libudev was found at configure time (TACETOS_HAVE_UDEV). Where it cannot be
// opened, the driver has no event source and polls.

struct udev;
struct udev_monitor;

namespace tacetos::device {

class UdevMonitor : public EventSource
{
    Q_OBJECT

public:
    // What an event of udev means for the live part.
    enum class Hint { None, Usb, Disks };

    // `group` is the netlink group given to libudev: "udev" for the events
    // after the rules ran, "kernel" for the raw ones. Anything else cannot be
    // opened (a test uses it to see the fallback).
    explicit UdevMonitor(QString group = QStringLiteral("udev"), QObject *parent = nullptr);
    ~UdevMonitor() override;

    // Opens the monitor and starts listening. False when libudev cannot give
    // one (a sandbox, no netlink socket): nothing is left half open, and the
    // caller polls instead.
    bool open();
    bool isOpen() const { return monitor_ != nullptr; }

    // Opens a monitor and gives it to the driver as its event source. Returns
    // null when it does not open, and the driver is left polling. The monitor
    // belongs to `driver`.
    static UdevMonitor *attach(LiveDriver *driver, const QString &group = QStringLiteral("udev"));

    // Section 9.1: a USB device (not its interfaces) is a hint to read the USB
    // list; a block device, disk or partition, is a hint to read the disks.
    static Hint classify(const QString &subsystem, const QString &devtype);

private:
    void onReadable();

    QString group_;
    udev *udev_ = nullptr;
    udev_monitor *monitor_ = nullptr;
    QSocketNotifier *notifier_ = nullptr;
};

} // namespace tacetos::device
