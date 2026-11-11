// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Operations (docs/specs/device-state.md sections 2 and 5.2): fake actions, a
// fake clock and an executor that works at once, so nothing sleeps and no
// D-Bus is needed.

#include "operations.h"

#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <stdexcept>

using namespace tacetos::device;

namespace {

const QString kPort = QStringLiteral("1-2");
const QString kOtherPort = QStringLiteral("1-3");

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return now; }
    qint64 now = 0;
};

// Answers from a script. `onRun` runs inside the work, to change the world in
// the middle of an operation.
class FakeActions : public DiskActions
{
public:
    DiskResult run(OperationKind kind, const QString &portPath) override
    {
        calls << QStringLiteral("%1@%2").arg(operationKindName(kind), portPath);
        heldDuringRun << (held.value(portPath, false));
        if (onRun)
            onRun(kind, portPath);
        if (throwIt)
            throw std::runtime_error("boom");
        return next;
    }

    DiskResult next;
    bool throwIt = false;
    std::function<void(OperationKind, const QString &)> onRun;
    QStringList calls;
    QList<bool> heldDuringRun;
    // The hold as the runner set it, kept by the harness.
    QHash<QString, bool> held;
};

struct Harness
{
    Harness()
        : runner(&clock, &actions, &executor, [this](const QString &port, bool on) {
              holds << QStringLiteral("%1=%2").arg(port).arg(on ? 1 : 0);
              actions.held[port] = on;
          })
    {
        QObject::connect(&runner, &OperationRunner::operationChanged,
                         [this](const Operation &op) { changes << op; });
        QObject::connect(&runner, &OperationRunner::logged,
                         [this](const QString &port, const QStringList &lines) {
                             for (const QString &line : lines)
                                 journal << port + QStringLiteral("|") + line;
                         });
    }

    QStringList statuses() const
    {
        QStringList out;
        for (const Operation &op : changes)
            out << operationStatusName(op.status);
        return out;
    }

    FakeClock clock;
    FakeActions actions;
    ImmediateExecutor executor;
    OperationRunner runner;
    QList<Operation> changes;
    QStringList holds;
    QStringList journal;
};

DiskResult failure(DiskError error, const QString &detail)
{
    DiskResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

// An executor that keeps the jobs until the test runs them, to see `queued`
// and a second request while the first is active.
class ManualExecutor : public Executor
{
public:
    bool submit(Job job) override
    {
        if (refuse)
            return false;
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
    bool refuse = false;
};

} // namespace

class TstDeviceOperations : public QObject
{
    Q_OBJECT

private slots:
    void names();
    void successOfEachKind_data();
    void successOfEachKind();
    void resultIsKept();
    void failures_data();
    void failures();
    void secondRequestRefused();
    void otherPortAccepted();
    void newRequestReplacesFinishedRecord();
    void forgetOnlyWhenNotActive();
    void queuedThenRunning();
    void holdReleasedAfterThrow();
    void holdReleasedWhenWorkerRefuses();
    void holdTakenBeforeWork();
    void unplugDuringOperation();
    void unplugOfAnotherPortIgnored();
    void journalLines();
    void threadExecutor();
};

void TstDeviceOperations::names()
{
    QCOMPARE(operationKindName(OperationKind::Mount), QStringLiteral("mount"));
    QCOMPARE(operationKindName(OperationKind::MountRo), QStringLiteral("mount-ro"));
    QCOMPARE(operationKindName(OperationKind::Unmount), QStringLiteral("unmount"));
    QCOMPARE(operationKindName(OperationKind::Eject), QStringLiteral("eject"));
    QCOMPARE(operationStatusName(OperationStatus::Queued), QStringLiteral("queued"));
    QCOMPARE(operationStatusName(OperationStatus::Failed), QStringLiteral("failed"));
}

void TstDeviceOperations::successOfEachKind_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("name");
    QTest::newRow("mount") << int(OperationKind::Mount) << QStringLiteral("mount");
    QTest::newRow("mount-ro") << int(OperationKind::MountRo) << QStringLiteral("mount-ro");
    QTest::newRow("unmount") << int(OperationKind::Unmount) << QStringLiteral("unmount");
    QTest::newRow("eject") << int(OperationKind::Eject) << QStringLiteral("eject");
}

void TstDeviceOperations::successOfEachKind()
{
    QFETCH(int, kind);
    QFETCH(QString, name);
    Harness h;
    h.clock.now = 1000;
    h.actions.next.mountPoint = QStringLiteral("/run/media/ipod");

    const OperationRequest request = h.runner.request(OperationKind(kind), kPort);
    QVERIFY(request.accepted);
    QCOMPARE(request.id, quint64(1));

    QCOMPARE(h.actions.calls, QStringList{name + QStringLiteral("@") + kPort});
    QCOMPARE(h.statuses(), (QStringList{"queued", "running", "done"}));
    const Operation op = *h.runner.operation(kPort);
    QCOMPARE(op.id, quint64(1));
    QCOMPARE(op.kind, OperationKind(kind));
    QCOMPARE(op.portPath, kPort);
    QCOMPARE(op.status, OperationStatus::Done);
    QCOMPARE(op.startMs, qint64(1000));
    QCOMPARE(*op.endMs, qint64(1000));
    QVERIFY(!op.active());
    QVERIFY(!op.deviceLeft);
    QCOMPARE(h.holds, (QStringList{"1-2=1", "1-2=0"}));
}

void TstDeviceOperations::resultIsKept()
{
    Harness h;
    h.clock.now = 200;
    h.actions.onRun = [&h](OperationKind, const QString &) { h.clock.now = 4700; };
    h.actions.next.mountPoint = QStringLiteral("/run/media/ipod");
    h.actions.next.unmounted = 2;
    h.actions.next.released = true;
    h.actions.next.detail = QStringLiteral("powered off");
    h.runner.request(OperationKind::Eject, kPort);

    const Operation op = *h.runner.operation(kPort);
    QCOMPARE(op.startMs, qint64(200));
    QCOMPARE(*op.endMs, qint64(4700));
    QCOMPARE(op.result.mountPoint, QStringLiteral("/run/media/ipod"));
    QCOMPARE(op.result.unmounted, 2);
    QVERIFY(op.result.released);
    QCOMPARE(op.result.detail, QStringLiteral("powered off"));
    // The last signal carries the same record.
    QCOMPARE(h.changes.last().result.unmounted, 2);
    QCOMPARE(*h.changes.last().endMs, qint64(4700));
}

void TstDeviceOperations::failures_data()
{
    QTest::addColumn<int>("error");
    QTest::addColumn<int>("kind");
    QTest::newRow("busy-unmount") << int(DiskError::Busy) << int(OperationKind::Unmount);
    QTest::newRow("busy-eject") << int(DiskError::Busy) << int(OperationKind::Eject);
    QTest::newRow("not-authorized-mount")
        << int(DiskError::NotAuthorized) << int(OperationKind::Mount);
    QTest::newRow("not-authorized-mount-ro")
        << int(DiskError::NotAuthorized) << int(OperationKind::MountRo);
    QTest::newRow("no-volume") << int(DiskError::NoVolume) << int(OperationKind::Mount);
    QTest::newRow("service-missing") << int(DiskError::ServiceMissing) << int(OperationKind::Eject);
    QTest::newRow("not-an-ipod-disk")
        << int(DiskError::NotAnIpodDisk) << int(OperationKind::Unmount);
    QTest::newRow("failed") << int(DiskError::Failed) << int(OperationKind::Eject);
}

void TstDeviceOperations::failures()
{
    QFETCH(int, error);
    QFETCH(int, kind);
    Harness h;
    h.clock.now = 10;
    h.actions.next = failure(DiskError(error), QStringLiteral("text of the service"));
    h.runner.request(OperationKind(kind), kPort);

    QCOMPARE(h.statuses(), (QStringList{"queued", "running", "failed"}));
    const Operation op = *h.runner.operation(kPort);
    QCOMPARE(op.status, OperationStatus::Failed);
    QCOMPARE(op.result.error, DiskError(error));
    QCOMPARE(op.result.detail, QStringLiteral("text of the service"));
    QVERIFY(op.endMs.has_value());
    QCOMPARE(h.holds, (QStringList{"1-2=1", "1-2=0"}));
}

void TstDeviceOperations::secondRequestRefused()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    QStringList holds;
    OperationRunner runner(&clock, &actions, &executor, [&holds](const QString &p, bool on) {
        holds << QStringLiteral("%1=%2").arg(p).arg(on ? 1 : 0);
    });
    QStringList journal;
    QObject::connect(&runner, &OperationRunner::logged,
                     [&journal](const QString &, const QStringList &lines) { journal << lines; });
    int changes = 0;
    QObject::connect(&runner, &OperationRunner::operationChanged,
                     [&changes](const Operation &) { ++changes; });

    const OperationRequest first = runner.request(OperationKind::Mount, kPort);
    QVERIFY(first.accepted);
    const int changesAfterFirst = changes;

    // Queued: refused. The first record is not touched.
    OperationRequest second = runner.request(OperationKind::Eject, kPort);
    QVERIFY(!second.accepted);
    QCOMPARE(second.id, first.id);
    QVERIFY(!second.reason.isEmpty());
    QCOMPARE(changes, changesAfterFirst);
    QCOMPARE(runner.operation(kPort)->kind, OperationKind::Mount);
    QCOMPARE(holds, (QStringList{"1-2=1"}));
    QVERIFY(journal.last().contains(QStringLiteral("refused")));

    // Running: refused too.
    Job &job = executor.jobs.first();
    job.started();
    QCOMPARE(runner.operation(kPort)->status, OperationStatus::Running);
    second = runner.request(OperationKind::Unmount, kPort);
    QVERIFY(!second.accepted);
    QCOMPARE(runner.operation(kPort)->kind, OperationKind::Mount);
    QCOMPARE(holds, (QStringList{"1-2=1"}));
    QCOMPARE(actions.calls.size(), 0);

    // After the end, a new request is accepted.
    job.finished(job.work());
    executor.jobs.clear();
    QCOMPARE(runner.operation(kPort)->status, OperationStatus::Done);
    QVERIFY(runner.request(OperationKind::Unmount, kPort).accepted);
}

void TstDeviceOperations::otherPortAccepted()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    OperationRunner runner(&clock, &actions, &executor, nullptr);

    QVERIFY(runner.request(OperationKind::Mount, kPort).accepted);
    const OperationRequest other = runner.request(OperationKind::Eject, kOtherPort);
    QVERIFY(other.accepted);
    QCOMPARE(other.id, quint64(2));
    // Two iPods at once: both active, one worker doing them one after another.
    QCOMPARE(runner.operation(kPort)->status, OperationStatus::Queued);
    QCOMPARE(runner.operation(kOtherPort)->status, OperationStatus::Queued);
    QCOMPARE(runner.operations().size(), 2);

    executor.runNext();
    QCOMPARE(runner.operation(kPort)->status, OperationStatus::Done);
    QCOMPARE(runner.operation(kOtherPort)->status, OperationStatus::Queued);
    executor.runNext();
    QCOMPARE(runner.operation(kOtherPort)->status, OperationStatus::Done);
    QCOMPARE(actions.calls, (QStringList{"mount@1-2", "eject@1-3"}));
}

void TstDeviceOperations::newRequestReplacesFinishedRecord()
{
    Harness h;
    h.actions.next = failure(DiskError::Busy, QStringLiteral("in use"));
    h.runner.request(OperationKind::Unmount, kPort);
    QCOMPARE(h.runner.operation(kPort)->status, OperationStatus::Failed);

    h.actions.next = DiskResult();
    const OperationRequest again = h.runner.request(OperationKind::Unmount, kPort);
    QVERIFY(again.accepted);
    QCOMPARE(again.id, quint64(2));
    QCOMPARE(h.runner.operations().size(), 1);
    QCOMPARE(h.runner.operation(kPort)->status, OperationStatus::Done);
    QCOMPARE(h.runner.operation(kPort)->id, quint64(2));
}

void TstDeviceOperations::forgetOnlyWhenNotActive()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    OperationRunner runner(&clock, &actions, &executor, nullptr);

    QVERIFY(!runner.forget(kPort));
    runner.request(OperationKind::Mount, kPort);
    QVERIFY(!runner.forget(kPort));
    QVERIFY(runner.operation(kPort).has_value());
    executor.runNext();
    QVERIFY(runner.forget(kPort));
    QVERIFY(!runner.operation(kPort).has_value());
    QVERIFY(runner.operations().isEmpty());
}

void TstDeviceOperations::queuedThenRunning()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    OperationRunner runner(&clock, &actions, &executor, nullptr);
    QList<Operation> changes;
    QObject::connect(&runner, &OperationRunner::operationChanged,
                     [&changes](const Operation &op) { changes << op; });

    clock.now = 50;
    runner.request(OperationKind::Mount, kPort);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.last().status, OperationStatus::Queued);
    QVERIFY(!changes.last().endMs.has_value());
    QCOMPARE(actions.calls.size(), 0);

    clock.now = 80;
    executor.jobs.first().started();
    QCOMPARE(changes.last().status, OperationStatus::Running);
    QCOMPARE(changes.last().startMs, qint64(50));
    QVERIFY(!changes.last().endMs.has_value());

    clock.now = 900;
    executor.jobs.first().finished(DiskResult());
    QCOMPARE(changes.last().status, OperationStatus::Done);
    QCOMPARE(*changes.last().endMs, qint64(900));
    // A second end for the same operation changes nothing.
    executor.jobs.first().finished(failure(DiskError::Failed, QStringLiteral("late")));
    QCOMPARE(changes.size(), 3);
    QCOMPARE(runner.operation(kPort)->status, OperationStatus::Done);
}

void TstDeviceOperations::holdReleasedAfterThrow()
{
    Harness h;
    h.actions.throwIt = true;
    h.runner.request(OperationKind::Eject, kPort);

    QCOMPARE(h.holds, (QStringList{"1-2=1", "1-2=0"}));
    const Operation op = *h.runner.operation(kPort);
    QCOMPARE(op.status, OperationStatus::Failed);
    QCOMPARE(op.result.error, DiskError::Failed);
    QVERIFY(op.result.detail.contains(QStringLiteral("boom")));
    // The runner takes the next request after that.
    h.actions.throwIt = false;
    QVERIFY(h.runner.request(OperationKind::Unmount, kPort).accepted);
}

void TstDeviceOperations::holdReleasedWhenWorkerRefuses()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    executor.refuse = true;
    QStringList holds;
    OperationRunner runner(&clock, &actions, &executor, [&holds](const QString &p, bool on) {
        holds << QStringLiteral("%1=%2").arg(p).arg(on ? 1 : 0);
    });

    const OperationRequest request = runner.request(OperationKind::Mount, kPort);
    QVERIFY(request.accepted);
    QCOMPARE(holds, (QStringList{"1-2=1", "1-2=0"}));
    const Operation op = *runner.operation(kPort);
    QCOMPARE(op.status, OperationStatus::Failed);
    QVERIFY(op.endMs.has_value());
    QCOMPARE(actions.calls.size(), 0);
}

void TstDeviceOperations::holdTakenBeforeWork()
{
    Harness h;
    h.runner.request(OperationKind::Mount, kPort);
    QCOMPARE(h.actions.heldDuringRun, (QList<bool>{true}));
    QVERIFY(!h.actions.held.value(kPort));

    // Every kind and every end: the hold count is always back at zero.
    const OperationKind kinds[] = {OperationKind::MountRo, OperationKind::Unmount,
                                   OperationKind::Eject};
    int taken = 1;
    for (const OperationKind kind : kinds) {
        h.actions.next = (taken % 2) ? failure(DiskError::Busy, QStringLiteral("x")) : DiskResult();
        h.runner.request(kind, kPort);
        ++taken;
    }
    QCOMPARE(h.holds.size(), taken * 2);
    for (qsizetype i = 0; i < h.holds.size(); ++i)
        QCOMPARE(h.holds.at(i), i % 2 == 0 ? QStringLiteral("1-2=1") : QStringLiteral("1-2=0"));
}

void TstDeviceOperations::unplugDuringOperation()
{
    Harness h;
    h.clock.now = 100;
    // The iPod leaves in the middle of the work. The work ends well here (an
    // eject: the disappearance is the aim).
    h.actions.onRun = [&h](OperationKind, const QString &port) {
        h.clock.now = 600;
        h.runner.noteUnplugged(port);
    };
    h.actions.next.released = true;
    h.runner.request(OperationKind::Eject, kPort);

    Operation op = *h.runner.operation(kPort);
    QCOMPARE(op.status, OperationStatus::Done);
    QVERIFY(op.deviceLeft);
    QVERIFY(op.result.released);
    QCOMPARE(h.holds, (QStringList{"1-2=1", "1-2=0"}));
    QVERIFY(h.statuses().contains(QStringLiteral("running")));
    // A line about it, once.
    int warnings = 0;
    for (const QString &line : h.journal) {
        if (line.contains(QStringLiteral("left while")))
            ++warnings;
    }
    QCOMPARE(warnings, 1);
    // The record is there for the state model, which drops it.
    QVERIFY(h.runner.forget(kPort));

    // The same with a failure after the unplug: the record says failed, with
    // the mark; showing nothing is the job of the state model.
    h.actions.next = failure(DiskError::NotAnIpodDisk, QStringLiteral("gone"));
    h.runner.request(OperationKind::Unmount, kPort);
    op = *h.runner.operation(kPort);
    QCOMPARE(op.status, OperationStatus::Failed);
    QVERIFY(op.deviceLeft);
    QCOMPARE(h.holds.last(), QStringLiteral("1-2=0"));
    QCOMPARE(h.holds.size(), 4);

    // After the end, an unplug is not an event of the operation.
    const int before = h.changes.size();
    h.runner.noteUnplugged(kPort);
    QCOMPARE(h.changes.size(), before);
}

void TstDeviceOperations::unplugOfAnotherPortIgnored()
{
    FakeClock clock;
    FakeActions actions;
    ManualExecutor executor;
    OperationRunner runner(&clock, &actions, &executor, nullptr);
    runner.request(OperationKind::Mount, kPort);
    runner.noteUnplugged(kOtherPort);
    QVERIFY(!runner.operation(kPort)->deviceLeft);
    QVERIFY(!runner.operation(kOtherPort).has_value());
}

void TstDeviceOperations::journalLines()
{
    Harness h;
    h.actions.next = failure(DiskError::NotAuthorized, QStringLiteral("Not authorized to mount"));
    h.runner.request(OperationKind::Mount, kPort);
    QCOMPARE(h.journal, (QStringList{
                            "1-2|op 1 mount: queued",
                            "1-2|op 1 mount: running",
                            "1-2|op 1 mount: failed (NotAuthorized): Not authorized to mount",
                        }));

    h.journal.clear();
    h.actions.next = DiskResult();
    h.runner.request(OperationKind::Unmount, kOtherPort);
    QCOMPARE(h.journal.last(), QStringLiteral("1-3|op 2 unmount: done"));
}

void TstDeviceOperations::threadExecutor()
{
    // The real worker: the work runs on another thread, the ends come back to
    // this one. The wait is on a condition, not on a time.
    FakeClock clock;
    FakeActions actions;
    ThreadExecutor executor;
    QStringList holds;
    OperationRunner runner(&clock, &actions, &executor, [&holds](const QString &p, bool on) {
        holds << QStringLiteral("%1=%2").arg(p).arg(on ? 1 : 0);
    });
    QStringList statuses;
    QObject::connect(&runner, &OperationRunner::operationChanged, [&statuses](const Operation &op) {
        statuses << operationStatusName(op.status);
    });

    actions.next.unmounted = 1;
    QVERIFY(runner.request(OperationKind::Unmount, kPort).accepted);
    QCOMPARE(statuses, QStringList{"queued"});
    QTRY_COMPARE_WITH_TIMEOUT(runner.operation(kPort)->status, OperationStatus::Done, 10000);
    QCOMPARE(statuses, (QStringList{"queued", "running", "done"}));
    QCOMPARE(runner.operation(kPort)->result.unmounted, 1);
    QCOMPARE(holds, (QStringList{"1-2=1", "1-2=0"}));
    QCOMPARE(actions.calls, QStringList{"unmount@1-2"});
}

QTEST_GUILESS_MAIN(TstDeviceOperations)
#include "tst_device_operations.moc"
