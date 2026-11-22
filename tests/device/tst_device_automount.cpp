// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Mounting on arrival (docs/specs/device-state.md sections 2.8 and 5.2): a
// scripted world, a fake clock and a fake disk service. Nothing sleeps.

#include "automount.h"
#include "constants.h"
#include "live.h"
#include "models.h"
#include "operations.h"
#include "screenstate.h"

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
    std::optional<QList<UsbDevice>> readUsb() override { return usb; }

    PortReading readPort(const UsbDevice &device) override { return ports.value(device.portPath); }

    void plug(const QString &usbId, const QString &port) { usb << usbDevice(usbId, port); }

    void unplug(const QString &port)
    {
        for (qsizetype i = usb.size() - 1; i >= 0; --i) {
            if (usb.at(i).portPath == port)
                usb.removeAt(i);
        }
        ports.remove(port);
    }

    void giveDisk(const QString &port)
    {
        DiskInfo disk;
        disk.node = QStringLiteral("/dev/sdb");
        disk.sizeBytes = 120'000'000'000;
        disk.sectorSize = 512;
        ports[port].disk = disk;
    }

    void giveVolume(const QString &port)
    {
        VolumeInfo volume;
        volume.mountPoint = QStringLiteral("/run/media/ipod");
        volume.filesystem = QStringLiteral("vfat");
        volume.totalBytes = 118'000'000'000;
        volume.freeBytes = 50'000'000'000;
        ports[port].volume = volume;
    }

    void takeVolume(const QString &port) { ports[port].volume.reset(); }

    QList<UsbDevice> usb;
    QHash<QString, PortReading> ports;
};

// The disk service: a mount gives the volume to the world, an unmount takes it
// back, like the real one.
class FakeActions : public DiskActions
{
public:
    DiskResult run(OperationKind kind, const QString &portPath) override
    {
        calls << QStringLiteral("%1@%2").arg(operationKindName(kind), portPath);
        if (!refuse.isEmpty() && kind == OperationKind::Mount) {
            DiskResult result;
            result.error = DiskError::NotAuthorized;
            result.detail = refuse;
            return result;
        }
        if (kind == OperationKind::Mount && world)
            world->giveVolume(portPath);
        if (kind == OperationKind::Unmount && world)
            world->takeVolume(portPath);
        return DiskResult();
    }

    int count(const QString &kind) const
    {
        int n = 0;
        for (const QString &call : calls) {
            if (call.startsWith(kind + QLatin1Char('@')))
                ++n;
        }
        return n;
    }

    FakeWorld *world = nullptr;
    QString refuse;
    QStringList calls;
};

struct Harness
{
    explicit Harness(bool enabled = true)
        : tracker(g_models, &clock, &world),
          runner(&clock, &actions, &executor,
                 [this](const QString &port, bool on) { tracker.setHeld(port, on); }),
          model(&tracker, &runner, &clock), mounter(&model, &tracker, &runner, enabled)
    {
        actions.world = &world;
        QObject::connect(&mounter, &AutoMounter::logged,
                         [this](const QString &port, const QStringList &lines) {
                             for (const QString &line : lines)
                                 warnings << port + QStringLiteral("|") + line;
                         });
        // The app reads the disks again when a mount ends.
        QObject::connect(&runner, &OperationRunner::operationChanged, [this](const Operation &op) {
            if (!op.active())
                tracker.diskEvent();
        });
    }

    void plug(const QString &port)
    {
        world.plug(kClassic, port);
        tracker.usbEvent();
    }
    void unplug(const QString &port)
    {
        world.unplug(port);
        tracker.usbEvent();
        runTo(clock.now + 20'000);
    }
    // An iPod whose volume is not mounted, once the grace of the mount is over.
    void arriveNotMounted(const QString &port)
    {
        plug(port);
        world.giveDisk(port);
        tracker.diskEvent();
        runTo(clock.now + kMountGraceMs + 100);
    }

    void runTo(qint64 end)
    {
        for (int guard = 0; guard < 1000; ++guard) {
            std::optional<qint64> when = tracker.nextWakeMs();
            const std::optional<qint64> own = model.nextWakeMs();
            if (own && (!when || *own < *when))
                when = own;
            if (!when || *when > end)
                break;
            clock.now = std::max(clock.now, *when);
            tracker.tick();
            model.tick();
        }
        clock.now = end;
        tracker.tick();
        model.tick();
    }

    QString kindAt(const QString &port) const
    {
        const auto item = model.entry(port);
        return item ? screenKindName(item->state.kind) : QStringLiteral("none");
    }

    FakeClock clock;
    FakeWorld world;
    FakeActions actions;
    ImmediateExecutor executor;
    LiveTracker tracker;
    OperationRunner runner;
    DeviceStateModel model;
    AutoMounter mounter;
    QStringList warnings;
};

} // namespace

class TstDeviceAutoMount : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void settingIsOnByDefault();
    void oneMountForAnIpodThatArrivesNotMounted();
    void noMountWhenAnIpodArrivesMounted();
    void noMountWhenHeld();
    void noMountForMacFormat();
    void noMountAfterAnUnmountByTheUser();
    void noMountAfterAnEjectByTheUser();
    void oneAgainAfterANewPlugIn();
    void noMountWhenTheSettingIsOff();
    void aRefusalLeavesNotMountedAndOneWarning();
    void aRefusalOfAMountOfTheUserStaysFailed();
    void twoIpodsAreIndependent();
};

void TstDeviceAutoMount::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

void TstDeviceAutoMount::settingIsOnByDefault()
{
    QVERIFY(kMountOnArrival);
    Harness h;
    QVERIFY(h.mounter.enabled());
}

void TstDeviceAutoMount::oneMountForAnIpodThatArrivesNotMounted()
{
    Harness h;
    h.arriveNotMounted(kPort);
    QCOMPARE(h.actions.calls, QStringList{QStringLiteral("mount@1-2")});
    // The volume is there, so the screen goes on to `done`, then `ready`.
    QCOMPARE(h.kindAt(kPort), QStringLiteral("done"));
    h.runTo(h.clock.now + kDoneShownMs + 100);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
    QCOMPARE(h.actions.calls.size(), 1);
    QVERIFY(h.warnings.isEmpty());
}

void TstDeviceAutoMount::noMountWhenAnIpodArrivesMounted()
{
    Harness h;
    h.plug(kPort);
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.tracker.diskEvent();
    h.runTo(h.clock.now + 5000);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
    QVERIFY(h.actions.calls.isEmpty());
}

void TstDeviceAutoMount::noMountWhenHeld()
{
    Harness h;
    h.plug(kPort);
    // A flow of the wizard holds the iPod before its volume is looked for.
    h.tracker.setHeld(kPort, true);
    h.world.giveDisk(kPort);
    h.tracker.diskEvent();
    h.runTo(h.clock.now + kMountGraceMs + 100);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("not-mounted"));
    QVERIFY(h.actions.calls.isEmpty());
}

void TstDeviceAutoMount::noMountForMacFormat()
{
    Harness h;
    h.plug(kPort);
    h.world.giveDisk(kPort);
    h.world.ports[kPort].diskReading.macFormat = true;
    h.tracker.diskEvent();
    h.runTo(h.clock.now + kMountGraceMs + 100);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("attention"));
    QVERIFY(h.actions.calls.isEmpty());
}

void TstDeviceAutoMount::noMountAfterAnUnmountByTheUser()
{
    Harness h;
    h.arriveNotMounted(kPort);
    h.runTo(h.clock.now + kDoneShownMs + 100);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));

    QVERIFY(h.model.startOperation(OperationKind::Unmount, kPort).accepted);
    h.runTo(h.clock.now + kDoneShownMs + kMountGraceMs + 100);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("not-mounted"));
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 1);
    QCOMPARE(h.actions.count(QStringLiteral("unmount")), 1);
}

void TstDeviceAutoMount::noMountAfterAnEjectByTheUser()
{
    Harness h;
    h.arriveNotMounted(kPort);
    h.runTo(h.clock.now + kDoneShownMs + 100);
    // The eject unmounts and leaves the iPod on its port: a drive that powers
    // off by itself is not what is scripted here, so the volume goes.
    h.actions.world->takeVolume(kPort);
    QVERIFY(h.model.startOperation(OperationKind::Eject, kPort).accepted);
    h.runTo(h.clock.now + kReleasedKeepMs + kMountGraceMs + 100);
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 1);
    QCOMPARE(h.actions.count(QStringLiteral("eject")), 1);
}

void TstDeviceAutoMount::oneAgainAfterANewPlugIn()
{
    Harness h;
    h.arriveNotMounted(kPort);
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 1);
    h.runTo(h.clock.now + kDoneShownMs + 100);

    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
    h.arriveNotMounted(kPort);
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 2);
}

void TstDeviceAutoMount::noMountWhenTheSettingIsOff()
{
    Harness h(false);
    QVERIFY(!h.mounter.enabled());
    h.arriveNotMounted(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("not-mounted"));
    QVERIFY(h.actions.calls.isEmpty());

    // Turned on later: the arrival that is already there is not mounted.
    h.mounter.setEnabled(true);
    h.runTo(h.clock.now + 10'000);
    QVERIFY(h.actions.calls.isEmpty());
}

void TstDeviceAutoMount::aRefusalLeavesNotMountedAndOneWarning()
{
    Harness h;
    h.actions.refuse = QStringLiteral("Not authorized to perform operation");
    h.arriveNotMounted(kPort);
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 1);
    // Not `failed`: the card is as before, with its Open button.
    QCOMPARE(h.kindAt(kPort), QStringLiteral("not-mounted"));
    QVERIFY(!h.runner.operation(kPort).has_value());
    QCOMPARE(h.warnings.size(), 1);
    QVERIFY(h.warnings.first().startsWith(kPort + QStringLiteral("|")));
    QVERIFY(h.warnings.first().contains(QStringLiteral("Not authorized to perform operation")));

    // And it is not tried again for this arrival.
    h.runTo(h.clock.now + 20'000);
    QCOMPARE(h.actions.count(QStringLiteral("mount")), 1);
    QCOMPARE(h.warnings.size(), 1);
}

void TstDeviceAutoMount::aRefusalOfAMountOfTheUserStaysFailed()
{
    Harness h(false);
    h.actions.refuse = QStringLiteral("Not authorized");
    h.arriveNotMounted(kPort);
    QVERIFY(h.model.startOperation(OperationKind::Mount, kPort).accepted);
    // Only the mount of the app is dropped: the user sees what they asked for.
    QCOMPARE(h.kindAt(kPort), QStringLiteral("failed"));
    QVERIFY(h.warnings.isEmpty());
}

void TstDeviceAutoMount::twoIpodsAreIndependent()
{
    Harness h;
    h.arriveNotMounted(kPort);
    QCOMPARE(h.actions.calls, QStringList{QStringLiteral("mount@1-2")});
    h.arriveNotMounted(kOtherPort);
    QCOMPARE(h.actions.calls,
             (QStringList{QStringLiteral("mount@1-2"), QStringLiteral("mount@1-3")}));
}

QTEST_GUILESS_MAIN(TstDeviceAutoMount)
#include "tst_device_automount.moc"
