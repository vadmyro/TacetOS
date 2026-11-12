// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "dfuaround.h"
#include "fakedfudevice.h"

#include <QList>
#include <QString>
#include <QStringList>

// The system under the helper, played from a script (docs/specs/bootloader-classic.md
// sections 15 and 16): the USB bus over time, the unmount of the iPod, and the
// programs of Apple that can be paused. The clock only moves when the flow sleeps.

namespace tacetos::installer {

// From `fromMs` on, until the next step, these devices are on the bus.
struct FakeBusStep
{
    int fromMs = 0;
    QList<UsbDeviceInfo> devices;
};

class FakeSystemLayer : public ClassicSystemLayer
{
public:
    HostSystem hostSystem() const override { return system; }
    QList<UsbDeviceInfo> listUsbDevices() override;
    std::unique_ptr<UsbDfuLink> makeLink(const UsbDeviceInfo &device) override;
    bool unmountIpod(QString *error) override;
    bool ipodListed() override;
    QList<SystemProcess> findAppleProcesses() override { return running; }
    bool pauseProcess(const SystemProcess &process, QString *error) override;
    bool resumeProcess(const SystemProcess &process, QString *error) override;

    // The options of the flow: `sleep` moves the clock and keeps the list of waits.
    AroundOptions options();

    HostSystem system = HostSystem::Linux;
    QList<FakeBusStep> bus;
    // The unmount fails this many times, then works.
    int unmountFailures = 0;
    // The system lists the iPod as a disk for this many calls after the unmount.
    int listedPolls = 0;
    QList<SystemProcess> running;
    QList<qint64> pauseFails;
    QList<qint64> resumeFails;
    // The device `makeLink` hands out (not owned).
    FakeDfuDevice *device = nullptr;
    // The flow is cancelled once the clock reaches this time; -1 for never.
    int cancelAtMs = -1;

    int now = 0;
    int unmountCalls = 0;
    QList<int> sleeps;
    QStringList notices;
    // "pause 12", "resume 12", in order.
    QStringList events;
    // The time at which each call of `makeLink` was made.
    QList<int> linkTimes;
};

} // namespace tacetos::installer
