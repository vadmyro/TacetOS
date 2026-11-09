// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The driver of the live part (docs/specs/detection.md section 9.1): a fake
// clock, a scripted world and a fake source of events, with no sleeping. The
// timer is never waited for: the test sets the clock and calls wake().

#include "constants.h"
#include "livedriver.h"
#include "models.h"

#include <QSignalSpy>
#include <QTest>

using namespace tacetos::device;

namespace {

QList<Model> g_models;

const QString kPort = QStringLiteral("1-2");
const QString kClassic = QStringLiteral("05ac:1261");

UsbDevice usbDevice(const QString &usbId, const QString &port)
{
    const UsbMatch match = matchUsbId(g_models, usbId);
    UsbDevice device;
    device.usbId = usbId;
    device.mode = match.mode;
    device.candidates = match.candidates;
    device.portPath = port;
    return device;
}

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return now; }
    qint64 now = 0;
};

class FakeWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override
    {
        ++usbReads;
        return usb;
    }

    PortReading readPort(const UsbDevice &) override
    {
        ++portReads;
        return PortReading();
    }

    void plug(const QString &usbId, const QString &port) { usb << usbDevice(usbId, port); }
    void unplugAll() { usb.clear(); }

    QList<UsbDevice> usb;
    int usbReads = 0;
    int portReads = 0;
};

class FakeEvents : public EventSource
{
public:
    void sendUsb() { emit usbChanged(); }
    void sendDisks() { emit disksChanged(); }
};

struct Rig
{
    Rig() : tracker(g_models, &clock, &world), driver(&tracker, &clock) {}

    FakeClock clock;
    FakeWorld world;
    LiveTracker tracker;
    LiveDriver driver;
};

} // namespace

class TstDeviceLiveDriver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void idleBeforeStart();
    void pollingWithoutSource();
    void pollingSeesAPlug();
    void eventSourceStopsPolling();
    void eventsAreHintsToReadNow();
    void backupReadingSeesASilentPlug();
    void noTimerWhenNothingWaits();
    void diskWaitEndsOnTheTimer();
    void heldIpodLeavesAfterTheWindow();
    void stoppedDriverIgnoresEverything();
    void lostSourceGoesBackToPolling();
};

void TstDeviceLiveDriver::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

void TstDeviceLiveDriver::idleBeforeStart()
{
    Rig r;
    QVERIFY(!r.driver.isRunning());
    QVERIFY(r.driver.polling());
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
    QCOMPARE(r.world.usbReads, 0);
}

// Section 9.1: no source, so 500 ms for the USB list and 1000 ms for the disks,
// and the timer is set to the nearer one.
void TstDeviceLiveDriver::pollingWithoutSource()
{
    Rig r;
    r.driver.start();
    // The first reading is due now.
    QCOMPARE(r.driver.scheduledDelayMs(), 0);

    r.driver.wake();
    QCOMPARE(r.driver.scheduledDelayMs(), static_cast<int>(kPollUsbMs));
    const int afterFirst = r.world.usbReads;

    // Too early: nothing is read, the timer is set for what is left.
    r.clock.now = 200;
    r.driver.wake();
    QCOMPARE(r.world.usbReads, afterFirst);
    QCOMPARE(r.driver.scheduledDelayMs(), 300);

    r.clock.now = 500;
    r.driver.wake();
    QCOMPARE(r.world.usbReads, afterFirst + 1);
    QCOMPARE(r.driver.scheduledDelayMs(), 500);

    r.clock.now = 1000;
    r.driver.wake();
    QCOMPARE(r.world.usbReads, afterFirst + 2);
}

void TstDeviceLiveDriver::pollingSeesAPlug()
{
    Rig r;
    QSignalSpy added(&r.tracker, &LiveTracker::ipodAdded);
    r.driver.start();
    r.driver.wake();

    r.world.plug(kClassic, kPort);
    r.clock.now = 400;
    r.driver.wake();
    QCOMPARE(added.size(), 0);
    r.clock.now = 500;
    r.driver.wake();
    QCOMPARE(added.size(), 1);
    QCOMPARE(r.tracker.ipods().first().portPath, kPort);
}

void TstDeviceLiveDriver::eventSourceStopsPolling()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    QVERIFY(!r.driver.polling());
    r.driver.start();

    // The start reads everything once, and then only the backup reading runs.
    QVERIFY(r.driver.backupRunning());
    const int reads = r.world.usbReads;
    QVERIFY(reads >= 1);
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
    r.clock.now = 60000;
    r.driver.wake();
    QCOMPARE(r.world.usbReads, reads);
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
}

void TstDeviceLiveDriver::eventsAreHintsToReadNow()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    QSignalSpy added(&r.tracker, &LiveTracker::ipodAdded);
    QSignalSpy changed(&r.tracker, &LiveTracker::ipodChanged);
    r.driver.start();

    r.clock.now = 1000;
    r.world.plug(kClassic, kPort);
    events.sendUsb();
    QCOMPARE(added.size(), 1);
    QCOMPARE(r.tracker.ipods().first().state, IpodState::Waiting);

    // A disk event reads the disks, and the portPath stays the same iPod.
    const int portReads = r.world.portReads;
    r.clock.now = 2000;
    events.sendDisks();
    QVERIFY(r.world.portReads > portReads);
    QCOMPARE(added.size(), 1);
    QCOMPARE(r.tracker.ipods().size(), 1);
}

void TstDeviceLiveDriver::backupReadingSeesASilentPlug()
{
    // A source that opens and never speaks, as udev in a sandbox: the backup
    // reading still sees the iPod come and go.
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    QVERIFY(!r.driver.backupRunning());
    QSignalSpy added(&r.tracker, &LiveTracker::ipodAdded);
    r.driver.start();
    QVERIFY(r.driver.backupRunning());

    r.clock.now = 1000;
    r.world.plug(kClassic, kPort);
    QCOMPARE(added.size(), 0);
    r.driver.backupTick();
    QCOMPARE(added.size(), 1);

    r.driver.stop();
    QVERIFY(!r.driver.backupRunning());
}

void TstDeviceLiveDriver::noTimerWhenNothingWaits()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    r.driver.start();
    QVERIFY(!r.driver.scheduledDelayMs().has_value());

    // An iPod in DFU waits for no disk: still no timer.
    r.world.plug(QStringLiteral("05ac:1223"), kPort);
    events.sendUsb();
    QCOMPARE(r.tracker.ipods().size(), 1);
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
}

// Section 9.2 step 4: the timer is set for the end of the disk wait, and
// nothing is set once it is over.
void TstDeviceLiveDriver::diskWaitEndsOnTheTimer()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    r.driver.start();

    r.clock.now = 1000;
    r.world.plug(kClassic, kPort);
    events.sendUsb();
    QCOMPARE(r.driver.scheduledDelayMs(), static_cast<int>(kDiskWaitMs));

    r.clock.now = 3000;
    QCOMPARE(r.driver.scheduledDelayMs(), static_cast<int>(kDiskWaitMs));
    r.driver.wake();
    QCOMPARE(r.tracker.ipods().first().state, IpodState::Waiting);
    QCOMPARE(r.driver.scheduledDelayMs(), static_cast<int>(kDiskWaitMs - 2000));

    r.clock.now = 1000 + kDiskWaitMs;
    r.driver.wake();
    QCOMPARE(r.tracker.ipods().first().state, IpodState::NoDisk);
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
}

void TstDeviceLiveDriver::heldIpodLeavesAfterTheWindow()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    QSignalSpy removed(&r.tracker, &LiveTracker::ipodRemoved);
    r.driver.start();

    r.world.plug(kClassic, kPort);
    events.sendUsb();
    r.tracker.setHeld(kPort, true);
    r.clock.now = 10000;
    r.world.unplugAll();
    events.sendUsb();
    QCOMPARE(removed.size(), 0);
    QCOMPARE(r.driver.scheduledDelayMs(), static_cast<int>(kSamePortWindowMs + 1));

    r.clock.now = 10000 + kSamePortWindowMs + 1;
    r.driver.wake();
    QCOMPARE(removed.size(), 1);
    QVERIFY(r.tracker.ipods().isEmpty());
    QVERIFY(!r.driver.scheduledDelayMs().has_value());
}

void TstDeviceLiveDriver::stoppedDriverIgnoresEverything()
{
    Rig r;
    FakeEvents events;
    r.driver.addEventSource(&events);
    r.driver.start();
    r.driver.stop();
    QVERIFY(!r.driver.isRunning());

    const int reads = r.world.usbReads;
    r.world.plug(kClassic, kPort);
    events.sendUsb();
    events.sendDisks();
    r.driver.wake();
    QCOMPARE(r.world.usbReads, reads);
    QVERIFY(r.tracker.ipods().isEmpty());
    QVERIFY(!r.driver.scheduledDelayMs().has_value());

    // Polling mode: stop also puts the timer out.
    Rig p;
    p.driver.start();
    QVERIFY(p.driver.scheduledDelayMs().has_value());
    p.driver.stop();
    QVERIFY(!p.driver.scheduledDelayMs().has_value());
}

void TstDeviceLiveDriver::lostSourceGoesBackToPolling()
{
    Rig r;
    auto *events = new FakeEvents;
    r.driver.addEventSource(events);
    r.driver.start();
    QVERIFY(!r.driver.polling());

    QVERIFY(r.driver.backupRunning());
    delete events;
    QVERIFY(r.driver.polling());
    QVERIFY(!r.driver.backupRunning());
    QVERIFY(r.driver.scheduledDelayMs().has_value());

    r.world.plug(kClassic, kPort);
    r.clock.now = 500;
    r.driver.wake();
    QCOMPARE(r.tracker.ipods().size(), 1);
}

QTEST_GUILESS_MAIN(TstDeviceLiveDriver)
#include "tst_device_livedriver.moc"
