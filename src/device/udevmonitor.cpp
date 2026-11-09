// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "udevmonitor.h"

#include <libudev.h>

namespace tacetos::device {

UdevMonitor::UdevMonitor(QString group, QObject *parent)
    : EventSource(parent), group_(std::move(group))
{
}

UdevMonitor::~UdevMonitor()
{
    delete notifier_;
    if (monitor_)
        udev_monitor_unref(monitor_);
    if (udev_)
        udev_unref(udev_);
}

bool UdevMonitor::open()
{
    if (monitor_)
        return true;

    udev_ = udev_new();
    if (!udev_)
        return false;
    monitor_ = udev_monitor_new_from_netlink(udev_, group_.toUtf8().constData());

    bool ok = monitor_ != nullptr;
    if (ok)
        ok = udev_monitor_filter_add_match_subsystem_devtype(monitor_, "usb", "usb_device") >= 0 &&
             udev_monitor_filter_add_match_subsystem_devtype(monitor_, "block", nullptr) >= 0 &&
             udev_monitor_enable_receiving(monitor_) >= 0;
    const int fd = ok ? udev_monitor_get_fd(monitor_) : -1;
    if (fd < 0) {
        if (monitor_)
            udev_monitor_unref(monitor_);
        monitor_ = nullptr;
        udev_unref(udev_);
        udev_ = nullptr;
        return false;
    }

    notifier_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { onReadable(); });
    return true;
}

UdevMonitor *UdevMonitor::attach(LiveDriver *driver, const QString &group)
{
    auto *monitor = new UdevMonitor(group, driver);
    if (!monitor->open()) {
        delete monitor;
        return nullptr;
    }
    driver->addEventSource(monitor);
    return monitor;
}

UdevMonitor::Hint UdevMonitor::classify(const QString &subsystem, const QString &devtype)
{
    if (subsystem == QLatin1String("usb"))
        return devtype == QLatin1String("usb_device") ? Hint::Usb : Hint::None;
    if (subsystem == QLatin1String("block"))
        return Hint::Disks;
    return Hint::None;
}

void UdevMonitor::onReadable()
{
    // Read everything that is waiting, then send one hint of each kind: an
    // event is never data, so a burst is the same as one (section 9.1).
    bool usb = false;
    bool disks = false;
    while (udev_device *device = udev_monitor_receive_device(monitor_)) {
        const char *subsystem = udev_device_get_subsystem(device);
        const char *devtype = udev_device_get_devtype(device);
        switch (classify(QString::fromUtf8(subsystem ? subsystem : ""),
                         QString::fromUtf8(devtype ? devtype : ""))) {
        case Hint::Usb:
            usb = true;
            break;
        case Hint::Disks:
            disks = true;
            break;
        case Hint::None:
            break;
        }
        udev_device_unref(device);
    }
    if (usb)
        emit usbChanged();
    if (disks)
        emit disksChanged();
}

} // namespace tacetos::device
