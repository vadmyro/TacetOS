// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Staying live (docs/specs/detection.md sections 9 and 12.5): a fake clock and
// a scripted world, with no sleeping. The checks are on when the list changes.

#include "constants.h"
#include "live.h"
#include "models.h"

#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <functional>

using namespace tacetos::device;

namespace {

QList<Model> g_models;

const QString kPort = QStringLiteral("1-2");
const QString kOtherPort = QStringLiteral("1-3");
const QString kClassic = QStringLiteral("05ac:1261");
const QString kClassicDfu = QStringLiteral("05ac:1223");
const QString kVideo = QStringLiteral("05ac:1209");

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

DiskInfo someDisk()
{
    DiskInfo disk;
    disk.node = QStringLiteral("/dev/sdb");
    disk.sizeBytes = 120'000'000'000;
    disk.sectorSize = 512;
    return disk;
}

VolumeInfo someVolume()
{
    VolumeInfo volume;
    volume.mountPoint = QStringLiteral("/run/media/ipod");
    volume.filesystem = QStringLiteral("vfat");
    volume.totalBytes = 118'000'000'000;
    volume.freeBytes = 50'000'000'000;
    return volume;
}

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return now; }
    qint64 now = 0;
};

// What the system shows at this moment. The test changes it by hand, so the
// tracker only ever learns of a change through a hint or a reading.
class FakeWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override
    {
        ++usbReads;
        if (usbFails)
            return std::nullopt;
        return usb;
    }

    QStringList usbLog() const override { return usbLines; }

    PortReading readPort(const UsbDevice &device) override
    {
        // The tracker must never ask for the disk of a DFU device.
        if (device.mode == UsbMode::Dfu)
            ++dfuPortReads;
        ++portReads;
        return ports.value(device.portPath);
    }

    void plug(const QString &usbId, const QString &port) { usb << usbDevice(usbId, port); }

    void unplug(const QString &port)
    {
        for (qsizetype i = usb.size() - 1; i >= 0; --i) {
            if (usb.at(i).portPath == port)
                usb.removeAt(i);
        }
        ports.remove(port);
    }

    void giveDisk(const QString &port) { ports[port].disk = someDisk(); }
    void giveVolume(const QString &port) { ports[port].volume = someVolume(); }

    QList<UsbDevice> usb;
    QHash<QString, PortReading> ports;
    bool usbFails = false;
    QStringList usbLines;
    int usbReads = 0;
    int portReads = 0;
    int dfuPortReads = 0;
};

struct Seen
{
    enum Kind { Added, Changed, Removed };
    qint64 at;
    Kind kind;
    QString port;
    IpodState state;
};

// A change to the world at a given time, with the hint that goes with it.
struct Step
{
    enum Hint { None, Usb, Disk };
    qint64 at;
    std::function<void(FakeWorld &)> change;
    Hint hint = None;
};

// Runs the script and the wake-ups the tracker asks for, in time order, then
// moves the clock to `end`. Nothing sleeps: the clock is only set.
class Harness
{
public:
    Harness() : tracker(g_models, &clock, &world)
    {
        QObject::connect(&tracker, &LiveTracker::ipodAdded, [this](const DetectedIpod &ipod) {
            seen << Seen{clock.now, Seen::Added, ipod.portPath, ipod.state};
        });
        QObject::connect(&tracker, &LiveTracker::ipodChanged, [this](const DetectedIpod &ipod) {
            seen << Seen{clock.now, Seen::Changed, ipod.portPath, ipod.state};
        });
        QObject::connect(&tracker, &LiveTracker::ipodRemoved, [this](const QString &port) {
            seen << Seen{clock.now, Seen::Removed, port, IpodState::Waiting};
        });
    }

    void run(QList<Step> steps, qint64 end)
    {
        std::stable_sort(steps.begin(), steps.end(),
                         [](const Step &a, const Step &b) { return a.at < b.at; });
        qsizetype next = 0;
        for (int guard = 0; guard < 100000; ++guard) {
            std::optional<qint64> when;
            if (next < steps.size())
                when = steps.at(next).at;
            const std::optional<qint64> wake = tracker.nextWakeMs();
            if (wake && (!when || *wake < *when))
                when = *wake;
            if (!when || *when > end)
                break;
            clock.now = std::max(clock.now, *when);
            tracker.tick();
            while (next < steps.size() && steps.at(next).at <= clock.now) {
                const Step &step = steps.at(next++);
                step.change(world);
                if (step.hint == Step::Usb)
                    tracker.usbEvent();
                else if (step.hint == Step::Disk)
                    tracker.diskEvent();
            }
        }
        clock.now = end;
        tracker.tick();
    }

    // The first signal of this kind for this port, or -1.
    qint64 timeOf(Seen::Kind kind, const QString &port, std::optional<IpodState> state = {}) const
    {
        for (const Seen &s : seen) {
            if (s.kind == kind && s.port == port && (!state || s.state == *state))
                return s.at;
        }
        return -1;
    }

    int count(Seen::Kind kind, const QString &port) const
    {
        int n = 0;
        for (const Seen &s : seen)
            n += s.kind == kind && s.port == port;
        return n;
    }

    DetectedIpod only() const
    {
        const QList<DetectedIpod> list = tracker.ipods();
        return list.size() == 1 ? list.first() : DetectedIpod();
    }

    FakeClock clock;
    FakeWorld world;
    LiveTracker tracker;
    QList<Seen> seen;
};

Step plugStep(qint64 at, const QString &usbId, const QString &port)
{
    return {at, [=](FakeWorld &w) { w.plug(usbId, port); }, Step::Usb};
}

Step diskStep(qint64 at, const QString &port)
{
    return {at, [=](FakeWorld &w) { w.giveDisk(port); }, Step::Disk};
}

Step volumeStep(qint64 at, const QString &port)
{
    return {at, [=](FakeWorld &w) { w.giveVolume(port); }, Step::Disk};
}

Step unplugStep(qint64 at, const QString &port)
{
    return {at, [=](FakeWorld &w) { w.unplug(port); }, Step::Usb};
}

} // namespace

class TstDeviceLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void plugInSteps();
    void plugInWithEverythingAlreadyThere();
    void noDiskThenALaterDisk();
    void noVolumeAfterTheMountGrace();
    void volumeInsideTheMountGraceSkipsNoVolume();
    void noDiskStillWaitsForTheDisk();
    void unplugAtEachStep_data();
    void unplugAtEachStep();
    void unplugOfOneLeavesTheOther();
    void dfuAloneShowsAtOnce();
    void dfuAfterPlainUnplugKeepsTheFields();
    void dfuResetWithinTheWindow();
    void dfuResetAfterTheWindow();
    void heldIpodSurvivesTheWindowOnly();
    void dfuBackToNormalOnTheSamePort();
    void dfuKeepsItsFieldsPastTheMemory();
    void lostUsbEventIsRecoveredByPolling();
    void lostDiskEventIsRecoveredByPolling();
    void lostEventWithoutPollingNeedsRefresh();
    void eventsAreHintsNotData();
    void failedUsbReadKeepsTheList();
    void nothingChangedSendsNothing();
    void oneCallOneAnnouncement();
    void dfuIsNeverReadForADisk();
    void journalLinesAtEachChange();
    void journalIsQuietOnIdenticalReadings();
    void nextWakeMs();
};

void TstDeviceLive::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

// Section 12.5: USB first, the disk 1 s later, the volume 2 s later. The list
// changes at the moment of each event, which is within the second of the aim.
void TstDeviceLive::plugInSteps()
{
    Harness h;
    h.run({plugStep(1000, kClassic, kPort), diskStep(2000, kPort), volumeStep(3000, kPort)}, 10000);

    QCOMPARE(h.timeOf(Seen::Added, kPort, IpodState::Waiting), 1000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Waiting), 2000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Ready), 3000);
    // Nothing else happens, in particular no no-disk when the volume came in time.
    QCOMPARE(h.seen.size(), 3);
    QCOMPARE(h.only().state, IpodState::Ready);
    QCOMPARE(h.only().model, QStringLiteral("classic-6g"));
    QVERIFY(h.only().volume.has_value());
}

void TstDeviceLive::plugInWithEverythingAlreadyThere()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(500, kClassic, kPort)}, 600);

    QCOMPARE(h.seen.size(), 1);
    QCOMPARE(h.seen.first().kind, Seen::Added);
    QCOMPARE(h.seen.first().state, IpodState::Ready);
}

// Section 9.2 step 4: no-disk after the wait, and a disk after it replaces the state.
void TstDeviceLive::noDiskThenALaterDisk()
{
    Harness h;
    const qint64 noDiskAt = 1000 + kDiskWaitMs;
    h.run({plugStep(1000, kClassic, kPort)}, noDiskAt - 1);
    QCOMPARE(h.only().state, IpodState::Waiting);

    h.run({}, noDiskAt + 4000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::NoDisk), noDiskAt);
    QCOMPARE(h.only().state, IpodState::NoDisk);

    h.run({diskStep(noDiskAt + 6000, kPort), volumeStep(noDiskAt + 8000, kPort)}, noDiskAt + 14000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Ready), noDiskAt + 8000);
    QCOMPARE(h.only().state, IpodState::Ready);
    // Never removed, never added twice: the late disk only replaces the state.
    QCOMPARE(h.count(Seen::Removed, kPort), 0);
    QCOMPARE(h.count(Seen::Added, kPort), 1);
}

// Section 9.2 step 5: the disk is there and nothing mounts it, so the wait ends
// kMountGraceMs after the disk appeared, not at the end of the disk wait.
void TstDeviceLive::noVolumeAfterTheMountGrace()
{
    Harness h;
    h.run({plugStep(0, kClassic, kPort), diskStep(5500, kPort)}, 5500 + kMountGraceMs - 1);
    QCOMPARE(h.only().state, IpodState::Waiting);
    // The tracker asks to be woken at the right moment.
    QCOMPARE(*h.tracker.nextWakeMs(), 5500 + kMountGraceMs);

    h.run({}, 12000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::NoVolume), 7500);
    QCOMPARE(h.only().state, IpodState::NoVolume);
    QCOMPARE(h.count(Seen::Removed, kPort), 0);
}

void TstDeviceLive::volumeInsideTheMountGraceSkipsNoVolume()
{
    Harness h;
    h.run({plugStep(0, kClassic, kPort), diskStep(5500, kPort), volumeStep(6000, kPort)}, 12000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Ready), 6000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::NoVolume), -1);
    QCOMPARE(h.only().state, IpodState::Ready);
}

// The grace is for the mount only: with no disk the wait is still kDiskWaitMs.
void TstDeviceLive::noDiskStillWaitsForTheDisk()
{
    Harness h;
    h.run({plugStep(0, kClassic, kPort)}, kDiskWaitMs - 1);
    QCOMPARE(h.only().state, IpodState::Waiting);
    QCOMPARE(*h.tracker.nextWakeMs(), kDiskWaitMs);
    h.run({}, kDiskWaitMs + 1000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::NoDisk), kDiskWaitMs);
    QCOMPARE(h.only().state, IpodState::NoDisk);
}

void TstDeviceLive::unplugAtEachStep_data()
{
    QTest::addColumn<qint64>("unplugAt");
    QTest::newRow("before the disk") << qint64(1500);
    QTest::newRow("after the disk") << qint64(2500);
    QTest::newRow("after the volume") << qint64(3500);
    QTest::newRow("after no-disk") << qint64(7000);
}

// Section 9.3: gone at once, with no wait and no message of failure.
void TstDeviceLive::unplugAtEachStep()
{
    QFETCH(qint64, unplugAt);
    Harness h;
    h.run({plugStep(1000, kClassic, kPort), diskStep(2000, kPort), volumeStep(3000, kPort),
           unplugStep(unplugAt, kPort)},
          10000);
    QCOMPARE(h.timeOf(Seen::Removed, kPort), unplugAt);
    QVERIFY(h.tracker.ipods().isEmpty());
    QCOMPARE(h.count(Seen::Removed, kPort), 1);
}

void TstDeviceLive::unplugOfOneLeavesTheOther()
{
    Harness h;
    h.world.giveDisk(kOtherPort);
    h.world.giveVolume(kOtherPort);
    h.run({plugStep(1000, kClassic, kPort), plugStep(1000, kVideo, kOtherPort),
           unplugStep(4000, kPort)},
          6000);
    QCOMPARE(h.timeOf(Seen::Removed, kPort), 4000);
    QCOMPARE(h.count(Seen::Removed, kOtherPort), 0);
    QCOMPARE(h.tracker.ipods().size(), 1);
    QCOMPARE(h.only().portPath, kOtherPort);
    QCOMPARE(h.only().model, QStringLiteral("ipod-video"));
}

// The id 05ac:1223 must reach the list through the same path as a plain plug in.
void TstDeviceLive::dfuAloneShowsAtOnce()
{
    Harness h;
    h.run({plugStep(2000, kClassicDfu, kPort)}, 3000);
    QCOMPARE(h.timeOf(Seen::Added, kPort, IpodState::Dfu), 2000);
    QVERIFY(h.only().model.isEmpty());
    QVERIFY(!h.only().candidates.isEmpty());
    // DFU has no disk, so it never turns into no-disk.
    h.run({}, 20000);
    QCOMPARE(h.only().state, IpodState::Dfu);
    QCOMPARE(h.seen.size(), 1);
}

// Plain unplug (nothing running) then DFU 1 s later: gone at once, and the new
// DFU iPod still carries the fields of the minute before (section 8.2 rule 5).
void TstDeviceLive::dfuAfterPlainUnplugKeepsTheFields()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(1000, kClassic, kPort), unplugStep(10000, kPort),
           plugStep(11000, kClassicDfu, kPort)},
          12000);
    QCOMPARE(h.timeOf(Seen::Removed, kPort), 10000);
    QCOMPARE(h.count(Seen::Added, kPort), 2);
    QCOMPARE(h.only().state, IpodState::Dfu);
    QCOMPARE(h.only().model, QStringLiteral("classic-6g"));
}

// Section 9.4 rule 2: held, it leaves the USB list, and comes back as DFU 2.9 s
// later. The screen never shows it gone and nothing else is lost.
void TstDeviceLive::dfuResetWithinTheWindow()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(1000, kClassic, kPort)}, 9000);
    h.tracker.setHeld(kPort, true);
    h.run({unplugStep(10000, kPort), plugStep(10000 + kSamePortWindowMs - 100, kClassicDfu, kPort)},
          20000);

    QCOMPARE(h.count(Seen::Removed, kPort), 0);
    QCOMPARE(h.count(Seen::Added, kPort), 1);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Dfu), 10000 + kSamePortWindowMs - 100);
    QCOMPARE(h.tracker.ipods().size(), 1);
    QCOMPARE(h.only().usbId, kClassicDfu);
    QCOMPARE(h.only().mode, UsbMode::Dfu);
    QCOMPARE(h.only().model, QStringLiteral("classic-6g"));
    QVERIFY(h.only().volume.has_value());
}

// Section 9.4 rule 3: back after more than 3 s, it was dropped in between.
void TstDeviceLive::dfuResetAfterTheWindow()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(1000, kClassic, kPort)}, 9000);
    h.tracker.setHeld(kPort, true);
    h.run({unplugStep(10000, kPort), plugStep(14000, kClassicDfu, kPort)}, 20000);

    // Still shown at exactly 3 s, dropped just after.
    QCOMPARE(h.timeOf(Seen::Removed, kPort), 10000 + kSamePortWindowMs + 1);
    QCOMPARE(h.timeOf(Seen::Added, kPort, IpodState::Dfu), 14000);
    QCOMPARE(h.count(Seen::Added, kPort), 2);
    QCOMPARE(h.tracker.ipods().size(), 1);
}

void TstDeviceLive::heldIpodSurvivesTheWindowOnly()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(1000, kClassic, kPort)}, 9000);
    h.tracker.setHeld(kPort, true);
    h.run({unplugStep(10000, kPort)}, 12999);
    QCOMPARE(h.tracker.ipods().size(), 1);
    QCOMPARE(h.count(Seen::Removed, kPort), 0);
    h.run({}, 13001);
    QVERIFY(h.tracker.ipods().isEmpty());
    QCOMPARE(h.count(Seen::Removed, kPort), 1);
}

// An iPod in DFU that leaves is kept for the window without being held, and
// comes back as a normal iPod: the wait starts again.
void TstDeviceLive::dfuBackToNormalOnTheSamePort()
{
    Harness h;
    h.run({plugStep(1000, kClassicDfu, kPort), unplugStep(5000, kPort),
           plugStep(6000, kClassic, kPort)},
          6500);
    QCOMPARE(h.count(Seen::Removed, kPort), 0);
    QCOMPARE(h.count(Seen::Added, kPort), 1);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::Waiting), 6000);
    QCOMPARE(h.only().mode, UsbMode::Normal);

    h.run({}, 20000);
    QCOMPARE(h.timeOf(Seen::Changed, kPort, IpodState::NoDisk), 6000 + kDiskWaitMs);
}

// Rule 5 only lasts a minute, but an iPod that stays in DFU must not lose what
// it kept when the memory runs out.
void TstDeviceLive::dfuKeepsItsFieldsPastTheMemory()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.tracker.setPolling(true);
    h.run({plugStep(0, kClassic, kPort)}, 5000);
    h.tracker.setHeld(kPort, true);
    h.run({unplugStep(6000, kPort), plugStep(7000, kClassicDfu, kPort)}, 200000);
    QCOMPARE(h.only().state, IpodState::Dfu);
    QCOMPARE(h.only().model, QStringLiteral("classic-6g"));
}

// Section 12.5: a lost event is recovered by the next poll, at most one poll
// period later.
void TstDeviceLive::lostUsbEventIsRecoveredByPolling()
{
    Harness h;
    h.tracker.setPolling(true);
    h.run({{1200, [](FakeWorld &w) { w.plug(kClassic, kPort); }, Step::None}}, 5000);
    const qint64 at = h.timeOf(Seen::Added, kPort);
    QVERIFY(at >= 1200);
    QVERIFY(at - 1200 <= kPollUsbMs);

    h.run({{6100, [](FakeWorld &w) { w.unplug(kPort); }, Step::None}}, 9000);
    const qint64 gone = h.timeOf(Seen::Removed, kPort);
    QVERIFY(gone >= 6100);
    QVERIFY(gone - 6100 <= kPollUsbMs);
}

void TstDeviceLive::lostDiskEventIsRecoveredByPolling()
{
    Harness h;
    h.tracker.setPolling(true);
    h.run({plugStep(1000, kClassic, kPort),
           {2200,
            [](FakeWorld &w) {
                w.giveDisk(kPort);
                w.giveVolume(kPort);
            },
            Step::None}},
          6000);
    const qint64 at = h.timeOf(Seen::Changed, kPort, IpodState::Ready);
    QVERIFY(at >= 2200);
    QVERIFY(at - 2200 <= kPollDisksMs);
}

void TstDeviceLive::lostEventWithoutPollingNeedsRefresh()
{
    Harness h;
    h.run({{1200, [](FakeWorld &w) { w.plug(kClassic, kPort); }, Step::None}}, 30000);
    QVERIFY(h.tracker.ipods().isEmpty());
    QVERIFY(h.seen.isEmpty());

    // Section 9.5: the button.
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.tracker.refresh();
    QCOMPARE(h.only().state, IpodState::Ready);
    QCOMPARE(h.timeOf(Seen::Added, kPort), 30000);
}

// Section 9.1: the event is a hint, the list is what counts. An event with
// nothing behind it changes nothing, and a change shows up with any hint.
void TstDeviceLive::eventsAreHintsNotData()
{
    Harness h;
    h.world.plug(kClassic, kPort);
    h.clock.now = 1000;
    // A disk event only rereads the disks of the devices already known.
    h.tracker.diskEvent();
    QVERIFY(h.tracker.ipods().isEmpty());
    h.tracker.usbEvent();
    QCOMPARE(h.tracker.ipods().size(), 1);
}

void TstDeviceLive::failedUsbReadKeepsTheList()
{
    Harness h;
    h.run({plugStep(1000, kClassic, kPort)}, 2000);
    h.world.usbFails = true;
    h.clock.now = 3000;
    h.tracker.usbEvent();
    QCOMPARE(h.tracker.ipods().size(), 1);
    QCOMPARE(h.count(Seen::Removed, kPort), 0);
    h.world.usbFails = false;
    h.world.unplug(kPort);
    h.tracker.usbEvent();
    QVERIFY(h.tracker.ipods().isEmpty());
}

void TstDeviceLive::nothingChangedSendsNothing()
{
    Harness h;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.run({plugStep(1000, kClassic, kPort)}, 2000);
    QSignalSpy list(&h.tracker, &LiveTracker::listChanged);
    h.tracker.usbEvent();
    h.tracker.diskEvent();
    h.tracker.refresh();
    h.tracker.tick();
    QCOMPARE(list.count(), 0);
    QCOMPARE(h.seen.size(), 1);
}

// A refresh that adds an iPod and reads its disk in one call announces it once,
// with the final value, and sends listChanged once.
void TstDeviceLive::oneCallOneAnnouncement()
{
    Harness h;
    h.world.plug(kClassic, kPort);
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.clock.now = 4000;
    QSignalSpy list(&h.tracker, &LiveTracker::listChanged);
    h.tracker.refresh();
    QCOMPARE(list.count(), 1);
    QCOMPARE(h.seen.size(), 1);
    QCOMPARE(h.seen.first().state, IpodState::Ready);
}

void TstDeviceLive::dfuIsNeverReadForADisk()
{
    Harness h;
    h.tracker.setPolling(true);
    h.run({plugStep(1000, kClassicDfu, kPort)}, 10000);
    QCOMPARE(h.world.dfuPortReads, 0);
    QCOMPARE(h.world.portReads, 0);
    QVERIFY(h.world.usbReads > 1);
}

// The lines of the journal: an add, each change of state, the log of the merge
// and of the USB list, a removal. Each one is sent when it appears.
void TstDeviceLive::journalLinesAtEachChange()
{
    Harness h;
    QList<QPair<QString, QStringList>> got;
    QObject::connect(
        &h.tracker, &LiveTracker::logged,
        [&got](const QString &port, const QStringList &lines) { got << qMakePair(port, lines); });
    const auto flat = [&got] {
        QStringList all;
        for (const auto &item : got)
            all << item.first + QStringLiteral("|") + item.second.join(QLatin1Char('/'));
        return all;
    };

    h.world.usbLines = {QStringLiteral("port 1-9: Apple id 05ac:ffff is in no model")};
    h.world.plug(kClassic, kPort);
    h.clock.now = 1000;
    h.tracker.usbEvent();
    QCOMPARE(got.size(), 2);
    QCOMPARE(got.at(0).first, QString());
    QCOMPARE(got.at(0).second, h.world.usbLines);
    QCOMPARE(got.at(1).first, kPort);
    // What the merge said, and the add.
    QVERIFY(
        got.at(1).second.contains(QStringLiteral("added: usb 05ac:1261 normal, state waiting")));

    got.clear();
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.clock.now = 1500;
    h.tracker.diskEvent();
    QCOMPARE(h.only().state, IpodState::Ready);
    QVERIFY(flat().join(QLatin1Char(';')).contains(QStringLiteral("state: waiting > ready")));
    for (const auto &item : got)
        QCOMPARE(item.first, kPort);

    got.clear();
    h.world.unplug(kPort);
    h.clock.now = 2000;
    h.tracker.usbEvent();
    QCOMPARE(flat(), QStringList{kPort + QStringLiteral("|removed")});
}

// Reading the same thing again, even by polling, adds nothing to the journal.
void TstDeviceLive::journalIsQuietOnIdenticalReadings()
{
    Harness h;
    h.world.usbLines = {QStringLiteral("port 1-9: Apple id 05ac:ffff is in no model")};
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.world.plug(kClassic, kPort);
    h.world.plug(kClassicDfu, kOtherPort);
    h.clock.now = 5000;
    h.tracker.refresh();
    QSignalSpy logged(&h.tracker, &LiveTracker::logged);
    for (int i = 0; i < 10; ++i) {
        h.clock.now += 2000;
        h.tracker.refresh();
        h.tracker.usbEvent();
        h.tracker.diskEvent();
        h.tracker.tick();
    }
    QCOMPARE(logged.count(), 0);
}

void TstDeviceLive::nextWakeMs()
{
    Harness h;
    h.clock.now = 100;
    QVERIFY(!h.tracker.nextWakeMs().has_value());
    h.world.plug(kClassic, kPort);
    h.tracker.usbEvent();
    QCOMPARE(*h.tracker.nextWakeMs(), 100 + kDiskWaitMs);
    h.tracker.setPolling(true);
    QCOMPARE(*h.tracker.nextWakeMs(), qint64(100));
    h.tracker.tick();
    QCOMPARE(*h.tracker.nextWakeMs(), 100 + kPollUsbMs);
}

QTEST_GUILESS_MAIN(TstDeviceLive)
#include "tst_device_live.moc"
