// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "live.h"
#include "udisks.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

// Operations on one iPod (docs/specs/device-state.md section 2): mount,
// mount read only, unmount and eject. The runner keeps one record per port,
// refuses a second operation on a port that is busy, holds the iPod in the
// tracker for as long as the operation is active, and tells the rest of the
// app when a record changes. The work itself goes through two interfaces that
// a test replaces: DiskActions (what is done to the disk) and Executor (where
// it is done), so the tests need no D-Bus, no thread and no waiting.

namespace tacetos::device {

enum class OperationKind { Mount, MountRo, Unmount, Eject };

enum class OperationStatus { Queued, Running, Done, Failed };

// The names of the spec ("mount", "mount-ro", "unmount", "eject").
QString operationKindName(OperationKind kind);
// "queued", "running", "done", "failed".
QString operationStatusName(OperationStatus status);

// Section 2.5.
struct Operation
{
    // A counter, unique in the session. Starts at 1.
    quint64 id = 0;
    OperationKind kind = OperationKind::Mount;
    QString portPath;
    OperationStatus status = OperationStatus::Queued;
    // The error of the service (or none) and its text. Empty until the end.
    DiskResult result;
    // When the request was accepted, in ms of the clock of the app.
    qint64 startMs = 0;
    // When it ended. None until then.
    std::optional<qint64> endMs;
    // The iPod left while the operation was active (section 2.6 rule 2). The
    // state model shows nothing for this record, except for a good eject.
    bool deviceLeft = false;

    bool active() const
    {
        return status == OperationStatus::Queued || status == OperationStatus::Running;
    }
};

// What is done to the disk of an iPod. The real one (LinuxDiskActions) waits
// for the disk service without any limit, a password window included, so
// run() is called from a worker, never from the thread of the screen.
class DiskActions
{
public:
    virtual ~DiskActions() = default;
    // Does `kind` to the disk behind `portPath`. Never called for two
    // operations at once.
    virtual DiskResult run(OperationKind kind, const QString &portPath) = 0;
};

// What the executor is asked to do for one operation. `started` and
// `finished` are called on the thread of the runner; `work` is called on the
// thread of the executor.
struct Job
{
    std::function<void()> started;
    std::function<DiskResult()> work;
    std::function<void(const DiskResult &)> finished;
};

// Where the work runs. Jobs are run one after another, in the order given.
class Executor
{
public:
    virtual ~Executor() = default;
    // Takes the job. False when it is refused (the executor is stopping): none
    // of the three functions is then called.
    virtual bool submit(Job job) = 0;
};

// Runs the job at once, on the calling thread. For the tests.
class ImmediateExecutor : public Executor
{
public:
    bool submit(Job job) override;
};

// One worker thread. Create it on the thread of the runner: `started` and
// `finished` come back to the thread it lives in, through its event loop.
// The destructor waits for the job that is running, which can last as long as
// a password window.
class ThreadExecutor : public QObject, public Executor
{
    Q_OBJECT

public:
    explicit ThreadExecutor(QObject *parent = nullptr);
    ~ThreadExecutor() override;

    bool submit(Job job) override;

private:
    class Worker;
    std::unique_ptr<Worker> worker_;
};

// The answer to a request.
struct OperationRequest
{
    bool accepted = false;
    // The id of the new record when accepted, else of the one in the way (0 if
    // none).
    quint64 id = 0;
    // When refused: why, in English, for the journal.
    QString reason;
};

class OperationRunner : public QObject
{
    Q_OBJECT

public:
    // Called with (portPath, true) when an operation is accepted and with
    // (portPath, false) when it ends, in every case. In the app it is
    // LiveTracker::setHeld.
    using HoldFunction = std::function<void(const QString &, bool)>;

    // `clock`, `actions` and `executor` must outlive the runner.
    OperationRunner(const Clock *clock, DiskActions *actions, Executor *executor, HoldFunction hold,
                    QObject *parent = nullptr);
    ~OperationRunner() override;

    // Section 2.4: refused at once when an operation of this port is queued or
    // running. A done or failed record of the port is replaced.
    OperationRequest request(OperationKind kind, const QString &portPath);

    // The record of a port, or none.
    std::optional<Operation> operation(const QString &portPath) const;
    // Every record, by id.
    QList<Operation> operations() const;

    // The state model tells that the iPod of this port is gone. Only marks the
    // active operation (Operation::deviceLeft) and writes a warning line.
    void noteUnplugged(const QString &portPath);

    // Drops the record of a port when it is no longer active. Returns whether
    // a record went. The state model calls it after `done` was shown, when
    // the user acts again, and when the iPod leaves.
    bool forget(const QString &portPath);

signals:
    // Every change of a record: queued, running, done, failed, and the mark of
    // an unplug.
    void operationChanged(const tacetos::device::Operation &operation);
    // Lines for the journal, like LiveTracker::logged: the start and the end
    // of each operation with the text of the service, refusals, unplugs.
    void logged(const QString &portPath, const QStringList &lines);

private:
    void onStarted(quint64 id);
    void onFinished(quint64 id, const DiskResult &result);
    void fail(quint64 id, DiskError error, const QString &detail);
    Operation *find(quint64 id);
    void log(const QString &portPath, const QString &line);

    const Clock *clock_;
    DiskActions *actions_;
    Executor *executor_;
    HoldFunction hold_;
    // One record per port.
    QHash<QString, Operation> records_;
    quint64 nextId_ = 1;
};

#if defined(TACETOS_HAVE_DBUS)
// The real actions: finds the disk of the port with readLinuxDisks, then calls
// DiskControl on the system bus. Linux with Qt D-Bus only.
class LinuxDiskActions : public DiskActions
{
public:
    // The real disk service.
    LinuxDiskActions();
    // Another backend (a test, a private bus).
    explicit LinuxDiskActions(std::unique_ptr<UdisksBackend> backend);

    DiskResult run(OperationKind kind, const QString &portPath) override;

private:
    DiskControl control_;
};
#endif

} // namespace tacetos::device

Q_DECLARE_METATYPE(tacetos::device::Operation)
