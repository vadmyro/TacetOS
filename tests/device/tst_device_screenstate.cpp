// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The displayed state (docs/specs/device-state.md sections 1 and 5.1): a table
// for the pure function, then scenarios with a fake clock, a scripted world and
// fake disk actions. Nothing sleeps.

#include "constants.h"
#include "live.h"
#include "livedriver.h"
#include "models.h"
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
const QString kClassicDfu = QStringLiteral("05ac:1223");

const QList<QPair<QString, IpodState>> kStates = {
    {QStringLiteral("conflict"), IpodState::Conflict},
    {QStringLiteral("unsupported-model"), IpodState::UnsupportedModel},
    {QStringLiteral("permission"), IpodState::Permission},
    {QStringLiteral("ambiguous"), IpodState::Ambiguous},
    {QStringLiteral("dfu"), IpodState::Dfu},
    {QStringLiteral("waiting"), IpodState::Waiting},
    {QStringLiteral("no-disk"), IpodState::NoDisk},
    {QStringLiteral("mac-format"), IpodState::MacFormat},
    {QStringLiteral("no-volume"), IpodState::NoVolume},
    {QStringLiteral("unsupported-filesystem"), IpodState::UnsupportedFilesystem},
    {QStringLiteral("ready"), IpodState::Ready},
};

IpodState stateOf(const QString &name)
{
    for (const auto &item : kStates) {
        if (item.first == name)
            return item.second;
    }
    qFatal("unknown state %s", qPrintable(name));
}

std::optional<DetectedIpod> ipodIn(const QString &name)
{
    if (name.isEmpty())
        return std::nullopt;
    DetectedIpod ipod;
    ipod.portPath = kPort;
    ipod.state = stateOf(name);
    return ipod;
}

OperationKind kindOf(const QString &name)
{
    for (OperationKind kind : {OperationKind::Mount, OperationKind::MountRo, OperationKind::Unmount,
                               OperationKind::Eject}) {
        if (operationKindName(kind) == name)
            return kind;
    }
    qFatal("unknown kind %s", qPrintable(name));
}

// "mount:queued", "unmount:done@100", "mount:failed@0:Busy", "eject:done@0:left".
std::optional<Operation> operationOf(const QString &text)
{
    if (text.isEmpty())
        return std::nullopt;
    const QStringList parts = text.split(QLatin1Char(':'));
    Operation op;
    op.id = 1;
    op.portPath = kPort;
    op.kind = kindOf(parts.at(0));
    const QString status = parts.at(1).section(QLatin1Char('@'), 0, 0);
    if (status == QLatin1String("queued"))
        op.status = OperationStatus::Queued;
    else if (status == QLatin1String("running"))
        op.status = OperationStatus::Running;
    else
        op.status =
            status == QLatin1String("done") ? OperationStatus::Done : OperationStatus::Failed;
    if (parts.at(1).contains(QLatin1Char('@')))
        op.endMs = parts.at(1).section(QLatin1Char('@'), 1).toLongLong();
    for (qsizetype i = 2; i < parts.size(); ++i) {
        if (parts.at(i) == QLatin1String("left"))
            op.deviceLeft = true;
        else if (parts.at(i) == QLatin1String("Busy"))
            op.result.error = DiskError::Busy;
    }
    return op;
}

std::optional<ReleasedRecord> releasedAt(qint64 sinceMs)
{
    if (sinceMs < 0)
        return std::nullopt;
    return ReleasedRecord{sinceMs};
}

QString shown(const std::optional<ScreenState> &state)
{
    return state ? screenKindName(state->kind) : QStringLiteral("none");
}

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

    QList<UsbDevice> usb;
    QHash<QString, PortReading> ports;
};

class FakeActions : public DiskActions
{
public:
    DiskResult run(OperationKind kind, const QString &portPath) override
    {
        if (onRun)
            onRun(kind, portPath);
        return next;
    }

    DiskResult next;
    std::function<void(OperationKind, const QString &)> onRun;
};

// Keeps the jobs until the test runs them, to see `busy`.
class ManualExecutor : public Executor
{
public:
    bool submit(Job job) override
    {
        jobs << std::move(job);
        return true;
    }

    void runNext()
    {
        Job job = jobs.takeFirst();
        job.started();
        job.finished(job.work());
    }

    QList<Job> jobs;
};

struct Harness
{
    Harness()
        : tracker(g_models, &clock, &world),
          runner(&clock, &actions, &executor,
                 [this](const QString &port, bool on) { tracker.setHeld(port, on); }),
          model(&tracker, &runner, &clock)
    {
        QObject::connect(&model, &DeviceStateModel::stateChanged,
                         [this](const QString &port, const ScreenState &state) {
                             log << QStringLiteral("%1 %2 %3")
                                        .arg(clock.now)
                                        .arg(port, screenKindName(state.kind));
                         });
    }

    // The world changes and the tracker is told, like an event would.
    void plug(const QString &usbId, const QString &port)
    {
        world.plug(usbId, port);
        tracker.usbEvent();
    }
    void unplug(const QString &port)
    {
        world.unplug(port);
        tracker.usbEvent();
    }
    void readyIpod(const QString &port)
    {
        plug(kClassic, port);
        world.giveDisk(port);
        world.giveVolume(port);
        tracker.diskEvent();
    }

    // Lets time pass to `end`, waking the tracker and the model at the times
    // they ask for, never earlier.
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
    ManualExecutor executor;
    LiveTracker tracker;
    OperationRunner runner;
    DeviceStateModel model;
    QStringList log;
};

} // namespace

class TstDeviceScreenState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void transitions_data();
    void transitions();
    void everyDetectionState_data();
    void everyDetectionState();
    void operationOverEveryDetectionState();
    void priorityOrder();
    void kindNames();

    void plugInToUnplugged();
    void dfuResetGivesRebootingThenDfu();
    void rebootingEndsWithNoEntry();
    void doneDisappearsAtFiveSeconds();
    void failedStaysUntilNextAction();
    void releasedKeptWhenTheIpodLeaves();
    void releasedKeepsTheLastReadyIpod();
    void releasedEndsOnPlugIn();
    void releasedEndsOnStartOrRefresh();
    void ejectWithTheIpodStillThere();
    void unplugDuringAnOperation();
    void unplugAfterDoneDropsIt();
    void twoIpodsAreIndependent();
    void nothingSentWhenUnchanged();
    void driverWakesTheModel();
};

void TstDeviceScreenState::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

// One line per row of the table of section 1.3 of the spec: the inputs are
// what the row leaves after its event, and `expected` is the "After" column.
void TstDeviceScreenState::transitions_data()
{
    QTest::addColumn<QString>("detected");
    QTest::addColumn<bool>("missing");
    QTest::addColumn<QString>("operation");
    QTest::addColumn<qint64>("released");
    QTest::addColumn<qint64>("now");
    QTest::addColumn<QString>("expected");

    const auto row = [](const char *name, const char *detected, bool missing, const char *op,
                        qint64 released, qint64 now, const char *expected) {
        QTest::newRow(name) << QString::fromLatin1(detected) << missing << QString::fromLatin1(op)
                            << released << now << QString::fromLatin1(expected);
    };
    row("1 plug in", "waiting", false, "", -1, 0, "reading");
    row("1 plug in, dfu", "dfu", false, "", -1, 0, "dfu");
    row("2 volume found", "ready", false, "", -1, 0, "ready");
    row("3 disk and no volume", "no-volume", false, "", -1, 0, "not-mounted");
    row("3 disk, mac format", "mac-format", false, "", -1, 0, "attention");
    row("4 disk wait over", "no-disk", false, "", -1, 0, "attention");
    row("5 conflict", "conflict", false, "", -1, 0, "attention");
    row("5 ambiguous", "ambiguous", false, "", -1, 0, "attention");
    row("5 permission", "permission", false, "", -1, 0, "attention");
    row("5 unsupported model", "unsupported-model", false, "", -1, 0, "attention");
    row("5 unsupported filesystem", "unsupported-filesystem", false, "", -1, 0, "attention");
    row("6 volume appears", "ready", false, "", -1, 0, "ready");
    row("7 disk appears", "no-volume", false, "", -1, 0, "not-mounted");
    row("8 operation starts", "ready", false, "mount:queued", -1, 0, "busy");
    row("8 operation running", "no-volume", false, "unmount:running", -1, 0, "busy");
    row("9 mount done", "ready", false, "mount:done@0", -1, 0, "done");
    row("9 mount-ro done", "ready", false, "mount-ro:done@0", -1, 0, "done");
    row("9 unmount done", "no-volume", false, "unmount:done@0", -1, 0, "done");
    row("10 eject done", "ready", false, "", 0, 0, "released");
    row("11 operation failed", "ready", false, "mount:failed@0:Busy", -1, 0, "failed");
    row("12 unplug while active", "", false, "eject:running:left", -1, 0, "busy");
    row("12 result not shown", "", false, "mount:done@0:left", -1, 0, "none");
    row("12 failure not shown", "", false, "mount:failed@0:left", -1, 0, "none");
    row("13 done, just before", "ready", false, "mount:done@0", -1, 4999, "done");
    row("13 done, at DONE_SHOWN", "ready", false, "mount:done@0", -1, 5000, "ready");
    row("14 operation after done", "ready", false, "mount:queued", -1, 100, "busy");
    row("15 operation after failed", "ready", false, "mount:queued", -1, 100, "busy");
    row("16 refresh after failed", "ready", false, "", -1, 100, "ready");
    row("16 refresh after released", "no-volume", false, "", -1, 100, "not-mounted");
    row("17 released, just before", "no-volume", false, "", 0, 29999, "released");
    row("17 released, at RELEASED_KEEP", "no-volume", false, "", 0, 30000, "not-mounted");
    row("17 released, no iPod left", "", false, "", 0, 30000, "none");
    row("18 plug in after released", "waiting", false, "", -1, 100, "reading");
    row("18 plug in dfu after released", "dfu", false, "", -1, 100, "dfu");
    row("19 released, the iPod leaves", "", false, "", 0, 100, "released");
    row("20 leaves, not held", "", false, "", -1, 0, "none");
    row("21 leaves, held, from ready", "ready", true, "", -1, 0, "rebooting");
    row("21 leaves, from dfu", "dfu", true, "", -1, 0, "rebooting");
    row("22 back in dfu", "dfu", false, "", -1, 0, "dfu");
    row("22 back in normal mode", "waiting", false, "", -1, 0, "reading");
    row("23 window passed", "", false, "", -1, 0, "none");
    row("24 mode changes", "ready", false, "", -1, 0, "ready");
    row("25 done, the iPod leaves", "", false, "mount:done@0", -1, 100, "none");
    row("25 failed, the iPod leaves", "", false, "mount:failed@0:Busy", -1, 100, "none");
}

void TstDeviceScreenState::transitions()
{
    QFETCH(QString, detected);
    QFETCH(bool, missing);
    QFETCH(QString, operation);
    QFETCH(qint64, released);
    QFETCH(qint64, now);
    QFETCH(QString, expected);
    QCOMPARE(shown(screenState(ipodIn(detected), missing, operationOf(operation),
                               releasedAt(released), now)),
             expected);
}

// Section 5.1: every state of section 8.3 of the detection spec reaches the
// right displayed state, and the reason of `attention` is the detection state.
void TstDeviceScreenState::everyDetectionState_data()
{
    QTest::addColumn<QString>("detected");
    QTest::addColumn<QString>("expected");
    const QHash<QString, QString> expected = {
        {QStringLiteral("conflict"), QStringLiteral("attention")},
        {QStringLiteral("unsupported-model"), QStringLiteral("attention")},
        {QStringLiteral("permission"), QStringLiteral("attention")},
        {QStringLiteral("ambiguous"), QStringLiteral("attention")},
        {QStringLiteral("dfu"), QStringLiteral("dfu")},
        {QStringLiteral("waiting"), QStringLiteral("reading")},
        {QStringLiteral("no-disk"), QStringLiteral("attention")},
        {QStringLiteral("mac-format"), QStringLiteral("attention")},
        {QStringLiteral("no-volume"), QStringLiteral("not-mounted")},
        {QStringLiteral("unsupported-filesystem"), QStringLiteral("attention")},
        {QStringLiteral("ready"), QStringLiteral("ready")},
    };
    for (const auto &item : kStates)
        QTest::newRow(qPrintable(item.first)) << item.first << expected.value(item.first);
}

void TstDeviceScreenState::everyDetectionState()
{
    QFETCH(QString, detected);
    QFETCH(QString, expected);
    const auto state = screenState(ipodIn(detected), false, std::nullopt, std::nullopt, 0);
    QCOMPARE(shown(state), expected);
    if (expected == QLatin1String("attention"))
        QCOMPARE(state->reason, stateOf(detected));
    // Disk Mode is reserved: nothing produces it.
    QVERIFY(state->kind != ScreenKind::DiskMode);
}

void TstDeviceScreenState::operationOverEveryDetectionState()
{
    for (const auto &item : kStates) {
        for (bool missing : {false, true}) {
            const auto state = screenState(ipodIn(item.first), missing,
                                           operationOf("eject:running"), releasedAt(0), 0);
            QCOMPARE(shown(state), QStringLiteral("busy"));
            QCOMPARE(state->operation, OperationKind::Eject);
        }
    }
}

// Section 1.2: operation, rebooting, released, detection.
void TstDeviceScreenState::priorityOrder()
{
    // Rebooting over released and detection.
    QCOMPARE(shown(screenState(ipodIn("ready"), true, std::nullopt, releasedAt(0), 10)),
             QStringLiteral("rebooting"));
    // Released over detection.
    QCOMPARE(shown(screenState(ipodIn("ready"), false, std::nullopt, releasedAt(0), 10)),
             QStringLiteral("released"));
    // Done and failed over rebooting.
    QCOMPARE(
        shown(screenState(ipodIn("ready"), true, operationOf("mount:done@0"), std::nullopt, 10)),
        QStringLiteral("done"));
    const auto failed =
        screenState(ipodIn("ready"), true, operationOf("mount:failed@0:Busy"), std::nullopt, 10);
    QCOMPARE(shown(failed), QStringLiteral("failed"));
    QCOMPARE(failed->error, DiskError::Busy);
    QCOMPARE(failed->operation, OperationKind::Mount);
    // Failed does not end by itself.
    QCOMPARE(shown(screenState(ipodIn("ready"), false, operationOf("mount:failed@0:Busy"),
                               std::nullopt, 3'600'000)),
             QStringLiteral("failed"));
    // Equality ignores what a state does not carry.
    ScreenState a;
    a.kind = ScreenKind::Ready;
    ScreenState b = a;
    b.error = DiskError::Busy;
    QCOMPARE(a, b);
    a.kind = b.kind = ScreenKind::Attention;
    b.reason = IpodState::NoDisk;
    a.reason = IpodState::Conflict;
    QVERIFY(a != b);
}

void TstDeviceScreenState::kindNames()
{
    QCOMPARE(screenKindName(ScreenKind::NotMounted), QStringLiteral("not-mounted"));
    QCOMPARE(screenKindName(ScreenKind::DiskMode), QStringLiteral("disk-mode"));
    QCOMPARE(screenKindName(ScreenKind::Unplugged), QStringLiteral("unplugged"));
}

// Plugged in, reading, ready, busy (eject), released, unplugged, unplugged.
void TstDeviceScreenState::plugInToUnplugged()
{
    Harness h;
    QCOMPARE(h.model.screen().kind, ScreenKind::Unplugged);
    QVERIFY(!h.model.nextWakeMs());

    h.clock.now = 100;
    h.plug(kClassic, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("reading"));
    h.clock.now = 1000;
    h.world.giveDisk(kPort);
    h.world.giveVolume(kPort);
    h.tracker.diskEvent();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));

    h.clock.now = 2000;
    const OperationRequest answer = h.model.startOperation(OperationKind::Eject, kPort);
    QVERIFY(answer.accepted);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    // The drive powers off, and the app sees the USB device go.
    h.clock.now = 2500;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    QVERIFY(h.runner.operation(kPort)->deviceLeft);
    h.clock.now = 3000;
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    QCOMPARE(h.model.screen().kind, ScreenKind::Released);
    // The iPod left, the record carries the last one that was ready.
    QVERIFY(h.model.entry(kPort)->ipod);
    // The cable is out: released goes on (row 19), up to RELEASED_KEEP.
    QCOMPARE(*h.model.nextWakeMs(), 3000 + kReleasedKeepMs);
    h.runTo(3000 + kReleasedKeepMs - 1);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    h.runTo(3000 + kReleasedKeepMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
    QCOMPARE(h.model.screen().kind, ScreenKind::Unplugged);
    QVERIFY(!h.model.nextWakeMs());

    QCOMPARE(h.log, (QStringList{
                        QStringLiteral("100 1-2 reading"),
                        QStringLiteral("1000 1-2 ready"),
                        QStringLiteral("2000 1-2 busy"),
                        QStringLiteral("3000 1-2 released"),
                        QStringLiteral("33000 1-2 unplugged"),
                    }));
}

// A held iPod that leaves is "rebooting", and a DFU device on the same port
// within the window is "dfu" (rows 21 and 22).
void TstDeviceScreenState::dfuResetGivesRebootingThenDfu()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
    // A flow that expects a restart holds the iPod (the kinds of v0.1 do not).
    h.tracker.setHeld(kPort, true);
    h.clock.now = 1000;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("rebooting"));
    QVERIFY(h.model.entry(kPort)->ipod);
    h.clock.now = 2000;
    h.plug(kClassicDfu, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("dfu"));
    QCOMPARE(h.log, (QStringList{
                        QStringLiteral("100 1-2 reading"),
                        QStringLiteral("100 1-2 ready"),
                        QStringLiteral("1000 1-2 rebooting"),
                        QStringLiteral("2000 1-2 dfu"),
                    }));

    // And from dfu, with nothing held: the reset of the iPod itself.
    h.log.clear();
    h.clock.now = 5000;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("rebooting"));
    h.clock.now = 6000;
    h.plug(kClassicDfu, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("dfu"));
    QCOMPARE(h.log,
             (QStringList{QStringLiteral("5000 1-2 rebooting"), QStringLiteral("6000 1-2 dfu")}));
}

// Row 23, when no flow expected a failure: no entry.
void TstDeviceScreenState::rebootingEndsWithNoEntry()
{
    Harness h;
    h.clock.now = 100;
    h.plug(kClassicDfu, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("dfu"));
    h.clock.now = 1000;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("rebooting"));
    h.runTo(1000 + kSamePortWindowMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("rebooting"));
    h.runTo(1000 + kSamePortWindowMs + 1);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
    QCOMPARE(h.model.screen().kind, ScreenKind::Unplugged);
    QCOMPARE(h.log.last(), QStringLiteral("4001 1-2 unplugged"));
}

void TstDeviceScreenState::doneDisappearsAtFiveSeconds()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.clock.now = 1000;
    h.model.startOperation(OperationKind::MountRo, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    h.clock.now = 1500;
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("done"));
    QCOMPARE(h.model.entry(kPort)->state.operation, OperationKind::MountRo);
    QCOMPARE(*h.model.nextWakeMs(), 1500 + kDoneShownMs);
    h.runTo(1500 + kDoneShownMs - 1);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("done"));
    h.runTo(1500 + kDoneShownMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
    // The runner let go of the record too, so a new operation is accepted.
    QVERIFY(!h.runner.operation(kPort));
    QVERIFY(!h.model.nextWakeMs());
    QCOMPARE(h.log.mid(h.log.size() - 3),
             (QStringList{QStringLiteral("1000 1-2 busy"), QStringLiteral("1500 1-2 done"),
                          QStringLiteral("6500 1-2 ready")}));
}

// Section 2.5: no timer on `failed`; Refresh or a new operation ends it.
void TstDeviceScreenState::failedStaysUntilNextAction()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.actions.next.error = DiskError::Busy;
    h.model.startOperation(OperationKind::Unmount, kPort);
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("failed"));
    QCOMPARE(h.model.entry(kPort)->state.error, DiskError::Busy);
    QVERIFY(!h.model.nextWakeMs());
    h.runTo(3'600'000);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("failed"));

    // Row 15.
    h.model.startOperation(OperationKind::Unmount, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("failed"));
    // Row 16.
    h.model.refresh();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
}

// Row 19 and section 2.7: the iPod leaves, the released record goes on.
void TstDeviceScreenState::releasedKeptWhenTheIpodLeaves()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.model.startOperation(OperationKind::Eject, kPort);
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    QVERIFY(h.model.entry(kPort)->ipod);
    h.clock.now = 10'000;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    // The last complete iPod stays with the record, for the name on the card.
    QVERIFY(h.model.entry(kPort)->ipod);
    QCOMPARE(h.model.entry(kPort)->ipod->state, IpodState::Ready);
    h.runTo(100 + kReleasedKeepMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
}

// Section 2.7: after the eject the drive loses its volume, but `released`
// carries the iPod as it was when it was ready.
void TstDeviceScreenState::releasedKeepsTheLastReadyIpod()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    QVERIFY(h.model.entry(kPort)->ipod->volume);
    h.model.startOperation(OperationKind::Eject, kPort);
    // The volume goes away while the eject runs.
    h.world.ports[kPort].volume.reset();
    h.tracker.diskEvent();
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    const auto entry = h.model.entry(kPort);
    QVERIFY(entry->ipod);
    QCOMPARE(entry->ipod->state, IpodState::Ready);
    QVERIFY(entry->ipod->volume);
    // After RELEASED_KEEP detection speaks again, with what it reads now.
    h.runTo(100 + kReleasedKeepMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("not-mounted"));
    QCOMPARE(h.model.entry(kPort)->ipod->state, IpodState::NoVolume);
}

// Row 18: a device that appears on the port starts from the beginning.
void TstDeviceScreenState::releasedEndsOnPlugIn()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.model.startOperation(OperationKind::Eject, kPort);
    h.executor.runNext();
    h.clock.now = 1000;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    h.clock.now = 5000;
    h.plug(kClassic, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("reading"));
    QVERIFY(!h.model.nextWakeMs());
}

void TstDeviceScreenState::releasedEndsOnStartOrRefresh()
{
    for (bool byRefresh : {true, false}) {
        Harness h;
        h.clock.now = 100;
        h.readyIpod(kPort);
        h.model.startOperation(OperationKind::Eject, kPort);
        h.executor.runNext();
        QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
        h.clock.now = 2000;
        if (byRefresh) {
            h.model.refresh();
            QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
        } else {
            h.model.startOperation(OperationKind::Mount, kPort);
            QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
        }
        QVERIFY(!h.model.nextWakeMs() || byRefresh == false);
    }
}

// After the released time, a drive that is still there shows what detection
// says (section 2.7 last point).
void TstDeviceScreenState::ejectWithTheIpodStillThere()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.model.startOperation(OperationKind::Eject, kPort);
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    h.runTo(100 + kReleasedKeepMs - 1);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("released"));
    h.runTo(100 + kReleasedKeepMs);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
}

// Row 12: no error for unplugging. The entry stays busy until the operation
// ends, then nothing is shown.
void TstDeviceScreenState::unplugDuringAnOperation()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.model.startOperation(OperationKind::Unmount, kPort);
    h.clock.now = 500;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    // Longer than the same port window: the tracker lets the iPod go, the
    // record alone keeps the entry.
    h.clock.now = 500 + kSamePortWindowMs + 10;
    h.tracker.tick();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    QVERIFY(!h.model.entry(kPort)->ipod);
    h.actions.next.error = DiskError::NoVolume;
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
    QVERIFY(!h.runner.operation(kPort));
    QCOMPARE(h.model.screen().kind, ScreenKind::Unplugged);
    QCOMPARE(h.log.last(), QStringLiteral("3510 1-2 unplugged"));
    for (const QString &line : std::as_const(h.log))
        QVERIFY2(!line.endsWith(QLatin1String("failed")), qPrintable(line));
}

// Row 25.
void TstDeviceScreenState::unplugAfterDoneDropsIt()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.model.startOperation(OperationKind::Mount, kPort);
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("done"));
    h.clock.now = 200;
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("none"));
    QVERIFY(!h.runner.operation(kPort));
    QVERIFY(!h.model.nextWakeMs());
}

void TstDeviceScreenState::twoIpodsAreIndependent()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    h.readyIpod(kOtherPort);
    QCOMPARE(h.model.entries().size(), qsizetype(2));
    h.log.clear();
    h.model.startOperation(OperationKind::Unmount, kPort);
    QCOMPARE(h.kindAt(kPort), QStringLiteral("busy"));
    QCOMPARE(h.kindAt(kOtherPort), QStringLiteral("ready"));
    h.executor.runNext();
    h.unplug(kPort);
    QCOMPARE(h.kindAt(kOtherPort), QStringLiteral("ready"));
    for (const QString &line : std::as_const(h.log))
        QVERIFY2(!line.contains(kOtherPort), qPrintable(line));
    // One entry left: not unplugged.
    h.clock.now = 10'000;
    h.runTo(10'000);
    QCOMPARE(h.model.screen().kind, ScreenKind::Ready);
}

void TstDeviceScreenState::nothingSentWhenUnchanged()
{
    Harness h;
    h.clock.now = 100;
    h.readyIpod(kPort);
    QSignalSpy states(&h.model, &DeviceStateModel::stateChanged);
    QSignalSpy lists(&h.model, &DeviceStateModel::listChanged);
    for (int i = 0; i < 5; ++i) {
        h.tracker.refresh();
        h.tracker.diskEvent();
        h.model.tick();
    }
    QCOMPARE(states.size(), 0);
    QCOMPARE(lists.size(), 0);
    // A change of the displayed state sends the port first, then the list, once.
    h.model.startOperation(OperationKind::Mount, kPort);
    QCOMPARE(states.size(), 1);
    QCOMPARE(lists.size(), 1);
}

// The delays of the model go through the driver's timer: none runs for nothing.
void TstDeviceScreenState::driverWakesTheModel()
{
    Harness h;
    h.clock.now = 0;
    h.readyIpod(kPort);
    LiveDriver driver(&h.tracker, &h.clock);
    EventSource events;
    driver.addEventSource(&events);
    driver.addWakeSource(&h.model);
    driver.start();
    // The disk wait of the tracker ends at 10 s.
    QCOMPARE(*driver.scheduledDelayMs(), int(kDiskWaitMs));
    h.model.startOperation(OperationKind::Mount, kPort);
    h.executor.runNext();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("done"));
    // The model told the driver, which set its timer to the nearer time.
    QCOMPARE(*driver.scheduledDelayMs(), int(kDoneShownMs));
    h.clock.now = kDoneShownMs;
    driver.wake();
    QCOMPARE(h.kindAt(kPort), QStringLiteral("ready"));
    QVERIFY(!h.model.nextWakeMs());
    QCOMPARE(*driver.scheduledDelayMs(), int(kDiskWaitMs - kDoneShownMs));
    h.clock.now = kDiskWaitMs;
    driver.wake();
    QVERIFY(!driver.scheduledDelayMs());
}

QTEST_GUILESS_MAIN(TstDeviceScreenState)
#include "tst_device_screenstate.moc"
