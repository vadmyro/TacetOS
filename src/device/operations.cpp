// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "operations.h"

#include "linuxdisks.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <exception>
#include <utility>

#if defined(TACETOS_HAVE_DBUS)
#include "udisksbackend.h"
#endif

namespace tacetos::device {

QString operationKindName(OperationKind kind)
{
    switch (kind) {
    case OperationKind::Mount:
        return QStringLiteral("mount");
    case OperationKind::MountRo:
        return QStringLiteral("mount-ro");
    case OperationKind::Unmount:
        return QStringLiteral("unmount");
    case OperationKind::Eject:
        return QStringLiteral("eject");
    }
    return QStringLiteral("unknown");
}

QString operationStatusName(OperationStatus status)
{
    switch (status) {
    case OperationStatus::Queued:
        return QStringLiteral("queued");
    case OperationStatus::Running:
        return QStringLiteral("running");
    case OperationStatus::Done:
        return QStringLiteral("done");
    case OperationStatus::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

namespace {

const char *diskErrorName(DiskError error)
{
    switch (error) {
    case DiskError::None:
        return "none";
    case DiskError::NotAnIpodDisk:
        return "NotAnIpodDisk";
    case DiskError::ServiceMissing:
        return "ServiceMissing";
    case DiskError::NotAuthorized:
        return "NotAuthorized";
    case DiskError::Busy:
        return "Busy";
    case DiskError::NoVolume:
        return "NoVolume";
    case DiskError::Failed:
        return "Failed";
    }
    return "unknown";
}

} // namespace

// ---- Executors ----------------------------------------------------------

bool ImmediateExecutor::submit(Job job)
{
    job.started();
    const DiskResult result = job.work();
    job.finished(result);
    return true;
}

class ThreadExecutor::Worker : public QObject
{
public:
    explicit Worker(ThreadExecutor *owner) : owner_(owner)
    {
        moveToThread(&thread_);
        thread_.start();
    }

    ~Worker() override
    {
        // Waits for the job that is running.
        thread_.quit();
        thread_.wait();
    }

    void post(Job job)
    {
        QMetaObject::invokeMethod(
            this,
            [this, job = std::move(job)]() {
                // Back to the thread of the owner for the two ends.
                QMetaObject::invokeMethod(owner_, job.started, Qt::QueuedConnection);
                const DiskResult result = job.work();
                QMetaObject::invokeMethod(
                    owner_, [finished = job.finished, result]() { finished(result); },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }

private:
    ThreadExecutor *owner_;
    QThread thread_;
};

ThreadExecutor::ThreadExecutor(QObject *parent) : QObject(parent), worker_(new Worker(this)) {}

ThreadExecutor::~ThreadExecutor() = default;

bool ThreadExecutor::submit(Job job)
{
    worker_->post(std::move(job));
    return true;
}

// ---- Runner -------------------------------------------------------------

OperationRunner::OperationRunner(const Clock *clock, DiskActions *actions, Executor *executor,
                                 HoldFunction hold, QObject *parent)
    : QObject(parent), clock_(clock), actions_(actions), executor_(executor), hold_(std::move(hold))
{
}

OperationRunner::~OperationRunner() = default;

OperationRequest OperationRunner::request(OperationKind kind, const QString &portPath)
{
    const auto existing = records_.constFind(portPath);
    if (existing != records_.constEnd() && existing->active()) {
        OperationRequest refused;
        refused.id = existing->id;
        refused.reason = QStringLiteral("operation %1 (%2) is still active on this iPod")
                             .arg(existing->id)
                             .arg(operationKindName(existing->kind));
        log(portPath,
            QStringLiteral("op: %1 refused: %2").arg(operationKindName(kind), refused.reason));
        return refused;
    }

    Operation op;
    op.id = nextId_++;
    op.kind = kind;
    op.portPath = portPath;
    op.status = OperationStatus::Queued;
    op.startMs = clock_->nowMs();
    records_.insert(portPath, op);
    const quint64 id = op.id;

    // Section 2.6 rule 1: held when accepted, before anything is queued.
    if (hold_)
        hold_(portPath, true);
    log(portPath, QStringLiteral("op %1 %2: queued").arg(id).arg(operationKindName(kind)));
    emit operationChanged(op);

    QPointer<OperationRunner> self(this);
    Job job;
    job.started = [self, id]() {
        if (self)
            self->onStarted(id);
    };
    job.work = [this, kind, portPath]() {
        // An exception of the work is an error, not a crash, and the end still
        // comes (section 2.6 rule 1).
        try {
            return actions_->run(kind, portPath);
        } catch (const std::exception &e) {
            DiskResult result;
            result.error = DiskError::Failed;
            result.detail = QStringLiteral("exception: %1").arg(QString::fromUtf8(e.what()));
            return result;
        } catch (...) {
            DiskResult result;
            result.error = DiskError::Failed;
            result.detail = QStringLiteral("exception");
            return result;
        }
    };
    job.finished = [self, id](const DiskResult &result) {
        if (self)
            self->onFinished(id, result);
    };

    // A refusal by the worker still ends the record, so the hold is released.
    if (!executor_ || !executor_->submit(std::move(job)))
        fail(id, DiskError::Failed, QStringLiteral("the worker refused the operation"));

    OperationRequest accepted;
    accepted.accepted = true;
    accepted.id = id;
    return accepted;
}

std::optional<Operation> OperationRunner::operation(const QString &portPath) const
{
    const auto it = records_.constFind(portPath);
    if (it == records_.constEnd())
        return std::nullopt;
    return *it;
}

QList<Operation> OperationRunner::operations() const
{
    QList<Operation> all = records_.values();
    std::sort(all.begin(), all.end(),
              [](const Operation &a, const Operation &b) { return a.id < b.id; });
    return all;
}

void OperationRunner::noteUnplugged(const QString &portPath)
{
    const auto it = records_.find(portPath);
    if (it == records_.end() || !it->active() || it->deviceLeft)
        return;
    it->deviceLeft = true;
    log(portPath, QStringLiteral("op %1 %2: the iPod left while the operation was active")
                      .arg(it->id)
                      .arg(operationKindName(it->kind)));
    emit operationChanged(*it);
}

bool OperationRunner::forget(const QString &portPath)
{
    const auto it = records_.find(portPath);
    if (it == records_.end() || it->active())
        return false;
    records_.erase(it);
    return true;
}

Operation *OperationRunner::find(quint64 id)
{
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->id == id)
            return &*it;
    }
    return nullptr;
}

void OperationRunner::onStarted(quint64 id)
{
    Operation *op = find(id);
    if (!op || op->status != OperationStatus::Queued)
        return;
    op->status = OperationStatus::Running;
    const Operation copy = *op;
    log(copy.portPath,
        QStringLiteral("op %1 %2: running").arg(copy.id).arg(operationKindName(copy.kind)));
    emit operationChanged(copy);
}

void OperationRunner::onFinished(quint64 id, const DiskResult &result)
{
    Operation *op = find(id);
    if (!op || !op->active())
        return;
    op->status = result.ok() ? OperationStatus::Done : OperationStatus::Failed;
    op->result = result;
    op->endMs = clock_->nowMs();
    const Operation copy = *op;

    // Section 2.6 rule 1: released at the end, in every case. The record is
    // complete first, so whoever reacts to the release sees the end.
    if (hold_)
        hold_(copy.portPath, false);

    QString line = QStringLiteral("op %1 %2: %3")
                       .arg(copy.id)
                       .arg(operationKindName(copy.kind), operationStatusName(copy.status));
    if (!result.ok())
        line += QStringLiteral(" (%1)").arg(QLatin1String(diskErrorName(result.error)));
    if (!result.detail.isEmpty())
        line += QStringLiteral(": ") + result.detail;
    log(copy.portPath, line);
    emit operationChanged(copy);
}

void OperationRunner::fail(quint64 id, DiskError error, const QString &detail)
{
    DiskResult result;
    result.error = error;
    result.detail = detail;
    onFinished(id, result);
}

void OperationRunner::log(const QString &portPath, const QString &line)
{
    emit logged(portPath, {line});
}

// ---- Real actions -------------------------------------------------------

#if defined(TACETOS_HAVE_DBUS)

LinuxDiskActions::LinuxDiskActions() : control_(std::make_unique<UdisksDBusBackend>()) {}

LinuxDiskActions::LinuxDiskActions(std::unique_ptr<UdisksBackend> backend)
    : control_(std::move(backend))
{
}

DiskResult LinuxDiskActions::run(OperationKind kind, const QString &portPath)
{
    const LinuxDiskList list = readLinuxDisks();
    for (const LinuxDisk &disk : list.disks) {
        if (disk.portPath != portPath)
            continue;
        switch (kind) {
        case OperationKind::Mount:
            return control_.mount(disk, false);
        case OperationKind::MountRo:
            return control_.mount(disk, true);
        case OperationKind::Unmount:
            return control_.unmount(disk);
        case OperationKind::Eject:
            return control_.eject(disk);
        }
    }
    DiskResult result;
    result.error = DiskError::NotAnIpodDisk;
    result.detail = list.error.isEmpty()
                        ? QStringLiteral("no iPod disk on port %1").arg(portPath)
                        : QStringLiteral("no iPod disk on port %1 (%2)").arg(portPath, list.error);
    return result;
}

#endif

} // namespace tacetos::device
