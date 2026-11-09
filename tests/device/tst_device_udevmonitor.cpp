// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The udev monitor (docs/specs/detection.md section 9.1). No udev event is
// made here: the test checks what an event means, and that a monitor which
// does not open leaves the driver polling. Whether the real monitor opens
// depends on the machine (a sandbox may refuse the netlink socket), so both
// results are accepted and checked.

#include "livedriver.h"
#include "udevmonitor.h"

#include <QTest>

using namespace tacetos::device;

namespace {

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return 0; }
};

class EmptyWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override { return QList<UsbDevice>(); }
    PortReading readPort(const UsbDevice &) override { return PortReading(); }
};

struct Rig
{
    Rig() : tracker(QList<Model>(), &clock, &world), driver(&tracker, &clock) {}

    FakeClock clock;
    EmptyWorld world;
    LiveTracker tracker;
    LiveDriver driver;
};

} // namespace

class TstDeviceUdevMonitor : public QObject
{
    Q_OBJECT

private slots:
    void classifyUsbDevicesAndBlocks();
    void unknownGroupDoesNotOpen();
    void failedAttachLeavesPolling();
    void realMonitorOpensOrFallsBack();
    void closedMonitorStaysSilent();
};

void TstDeviceUdevMonitor::classifyUsbDevicesAndBlocks()
{
    using Hint = UdevMonitor::Hint;
    QCOMPARE(UdevMonitor::classify(QStringLiteral("usb"), QStringLiteral("usb_device")), Hint::Usb);
    // The interfaces of a device are not a change of the list.
    QCOMPARE(UdevMonitor::classify(QStringLiteral("usb"), QStringLiteral("usb_interface")),
             Hint::None);
    QCOMPARE(UdevMonitor::classify(QStringLiteral("usb"), QString()), Hint::None);
    QCOMPARE(UdevMonitor::classify(QStringLiteral("block"), QStringLiteral("disk")), Hint::Disks);
    QCOMPARE(UdevMonitor::classify(QStringLiteral("block"), QStringLiteral("partition")),
             Hint::Disks);
    QCOMPARE(UdevMonitor::classify(QStringLiteral("block"), QString()), Hint::Disks);
    QCOMPARE(UdevMonitor::classify(QStringLiteral("net"), QString()), Hint::None);
}

void TstDeviceUdevMonitor::unknownGroupDoesNotOpen()
{
    UdevMonitor monitor(QStringLiteral("no-such-group"));
    QVERIFY(!monitor.isOpen());
    QVERIFY(!monitor.open());
    QVERIFY(!monitor.isOpen());
}

// Section 9.1: no monitor, no error: the driver polls.
void TstDeviceUdevMonitor::failedAttachLeavesPolling()
{
    Rig r;
    QVERIFY(UdevMonitor::attach(&r.driver, QStringLiteral("no-such-group")) == nullptr);
    QVERIFY(r.driver.polling());
    QVERIFY(r.driver.findChildren<UdevMonitor *>().isEmpty());

    r.driver.start();
    QVERIFY(r.driver.isRunning());
    QVERIFY(r.driver.scheduledDelayMs().has_value());
}

void TstDeviceUdevMonitor::realMonitorOpensOrFallsBack()
{
    Rig r;
    UdevMonitor *monitor = UdevMonitor::attach(&r.driver);
    if (!monitor) {
        QVERIFY(r.driver.polling());
        QSKIP("The udev monitor cannot be opened here: the polling fallback was checked.");
    }
    QVERIFY(monitor->isOpen());
    QVERIFY(!r.driver.polling());

    // Opening twice is harmless.
    QVERIFY(monitor->open());

    r.driver.start();
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
}

void TstDeviceUdevMonitor::closedMonitorStaysSilent()
{
    UdevMonitor monitor(QStringLiteral("no-such-group"));
    int hints = 0;
    connect(&monitor, &EventSource::usbChanged, this, [&] { ++hints; });
    connect(&monitor, &EventSource::disksChanged, this, [&] { ++hints; });
    QVERIFY(!monitor.open());
    QCoreApplication::processEvents();
    QCOMPARE(hints, 0);
}

QTEST_GUILESS_MAIN(TstDeviceUdevMonitor)
#include "tst_device_udevmonitor.moc"
