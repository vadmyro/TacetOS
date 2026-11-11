// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "screenstate.h"

#include "constants.h"

#include <algorithm>

namespace tacetos::device {

QString screenKindName(ScreenKind kind)
{
    switch (kind) {
    case ScreenKind::Unplugged:
        return QStringLiteral("unplugged");
    case ScreenKind::Reading:
        return QStringLiteral("reading");
    case ScreenKind::Ready:
        return QStringLiteral("ready");
    case ScreenKind::NotMounted:
        return QStringLiteral("not-mounted");
    case ScreenKind::Dfu:
        return QStringLiteral("dfu");
    case ScreenKind::Rebooting:
        return QStringLiteral("rebooting");
    case ScreenKind::Busy:
        return QStringLiteral("busy");
    case ScreenKind::Done:
        return QStringLiteral("done");
    case ScreenKind::Failed:
        return QStringLiteral("failed");
    case ScreenKind::Released:
        return QStringLiteral("released");
    case ScreenKind::Attention:
        return QStringLiteral("attention");
    case ScreenKind::DiskMode:
        return QStringLiteral("disk-mode");
    }
    return QStringLiteral("unknown");
}

bool ScreenState::operator==(const ScreenState &other) const
{
    if (kind != other.kind)
        return false;
    switch (kind) {
    case ScreenKind::Attention:
        return reason == other.reason;
    case ScreenKind::Busy:
    case ScreenKind::Done:
        return operation == other.operation;
    case ScreenKind::Failed:
        return operation == other.operation && error == other.error;
    default:
        return true;
    }
}

namespace {

ScreenState simple(ScreenKind kind)
{
    ScreenState state;
    state.kind = kind;
    return state;
}

// Section 1.1, the column "Comes from" of the states of detection.
ScreenState fromDetection(IpodState detected)
{
    switch (detected) {
    case IpodState::Waiting:
        return simple(ScreenKind::Reading);
    case IpodState::Ready:
        return simple(ScreenKind::Ready);
    case IpodState::NoVolume:
        return simple(ScreenKind::NotMounted);
    case IpodState::Dfu:
        return simple(ScreenKind::Dfu);
    case IpodState::NoDisk:
    case IpodState::MacFormat:
    case IpodState::Conflict:
    case IpodState::Ambiguous:
    case IpodState::Permission:
    case IpodState::UnsupportedFilesystem:
    case IpodState::UnsupportedModel:
        break;
    }
    ScreenState state = simple(ScreenKind::Attention);
    state.reason = detected;
    return state;
}

} // namespace

std::optional<ScreenState> screenState(const std::optional<DetectedIpod> &ipod, bool missing,
                                       const std::optional<Operation> &operation,
                                       const std::optional<ReleasedRecord> &released, qint64 nowMs)
{
    // 1. Operation.
    if (operation) {
        ScreenState state = simple(ScreenKind::Busy);
        state.operation = operation->kind;
        if (operation->active())
            return state;
        // A result is shown on an iPod that is there. When it left, nothing is
        // shown (rows 12 and 25). A good eject is the released record's.
        if (ipod && !operation->deviceLeft) {
            if (operation->status == OperationStatus::Failed) {
                state.kind = ScreenKind::Failed;
                state.error = operation->result.error;
                return state;
            }
            const bool fresh = !operation->endMs || nowMs - *operation->endMs < kDoneShownMs;
            if (operation->kind != OperationKind::Eject && fresh) {
                state.kind = ScreenKind::Done;
                return state;
            }
        }
    }
    // 2. Rebooting.
    if (ipod && missing)
        return simple(ScreenKind::Rebooting);
    // 3. Released.
    if (released && nowMs - released->sinceMs < kReleasedKeepMs)
        return simple(ScreenKind::Released);
    // 4. Detection. 5. Nothing.
    if (ipod)
        return fromDetection(ipod->state);
    return std::nullopt;
}

// ---- The model ----------------------------------------------------------

DeviceStateModel::DeviceStateModel(LiveTracker *tracker, OperationRunner *runner,
                                   const Clock *clock, QObject *parent)
    : WakeSource(parent), tracker_(tracker), runner_(runner), clock_(clock)
{
    for (const DetectedIpod &ipod : tracker_->ipods()) {
        if (const auto now = compute(ipod.portPath)) {
            order_ << ipod.portPath;
            shown_.insert(ipod.portPath, now->state);
        }
    }
    lastWake_ = nextWakeMs();
    connect(tracker_, &LiveTracker::ipodAdded, this, &DeviceStateModel::onAdded);
    connect(tracker_, &LiveTracker::ipodRemoved, this, &DeviceStateModel::onRemoved);
    connect(tracker_, &LiveTracker::ipodMissingChanged, this,
            [this](const QString &portPath, bool missing) {
                // Row 12: the iPod of an active operation went away.
                if (missing)
                    runner_->noteUnplugged(portPath);
            });
    connect(tracker_, &LiveTracker::listChanged, this, &DeviceStateModel::update);
    connect(runner_, &OperationRunner::operationChanged, this, &DeviceStateModel::onOperation);
}

std::optional<ScreenEntry> DeviceStateModel::compute(const QString &portPath) const
{
    std::optional<DetectedIpod> ipod;
    for (const DetectedIpod &candidate : tracker_->ipods()) {
        if (candidate.portPath == portPath)
            ipod = candidate;
    }
    std::optional<ReleasedRecord> released;
    const auto record = released_.constFind(portPath);
    if (record != released_.constEnd())
        released = *record;
    const std::optional<ScreenState> state =
        screenState(ipod, tracker_->isMissing(portPath), runner_->operation(portPath), released,
                    clock_->nowMs());
    if (!state)
        return std::nullopt;
    return ScreenEntry{portPath, *state, ipod};
}

QList<ScreenEntry> DeviceStateModel::entries() const
{
    QList<ScreenEntry> out;
    for (const QString &port : order_) {
        if (const auto item = compute(port))
            out << *item;
    }
    return out;
}

std::optional<ScreenEntry> DeviceStateModel::entry(const QString &portPath) const
{
    return compute(portPath);
}

ScreenState DeviceStateModel::screen() const
{
    const QList<ScreenEntry> list = entries();
    return list.isEmpty() ? ScreenState() : list.first().state;
}

OperationRequest DeviceStateModel::startOperation(OperationKind kind, const QString &portPath)
{
    // Section 2.7: starting an operation ends the released record. A refused
    // request leaves it alone.
    const std::optional<Operation> current = runner_->operation(portPath);
    const bool refused = current && current->active();
    if (!refused)
        released_.remove(portPath);
    const OperationRequest answer = runner_->request(kind, portPath);
    update();
    return answer;
}

void DeviceStateModel::refresh()
{
    // Row 16: `failed` and `released` end, the rest of what is shown is what
    // detection reads again.
    released_.clear();
    for (const Operation &op : runner_->operations()) {
        if (op.status == OperationStatus::Failed)
            runner_->forget(op.portPath);
    }
    tracker_->refresh();
    update();
}

std::optional<qint64> DeviceStateModel::nextWakeMs() const
{
    std::optional<qint64> next;
    const auto consider = [&next](qint64 when) {
        if (!next || when < *next)
            next = when;
    };
    for (const Operation &op : runner_->operations()) {
        if (op.status == OperationStatus::Done && op.endMs)
            consider(*op.endMs + kDoneShownMs);
    }
    for (const ReleasedRecord &record : released_)
        consider(record.sinceMs + kReleasedKeepMs);
    return next;
}

void DeviceStateModel::tick()
{
    const qint64 now = clock_->nowMs();
    for (const Operation &op : runner_->operations()) {
        if (op.status == OperationStatus::Done && op.endMs && now - *op.endMs >= kDoneShownMs)
            runner_->forget(op.portPath);
    }
    for (auto it = released_.begin(); it != released_.end();) {
        if (now - it->sinceMs >= kReleasedKeepMs)
            it = released_.erase(it);
        else
            ++it;
    }
    update();
}

void DeviceStateModel::onAdded(const DetectedIpod &ipod)
{
    // Row 18: a device appears on the port, a new plug in.
    released_.remove(ipod.portPath);
}

void DeviceStateModel::onRemoved(const QString &portPath)
{
    runner_->noteUnplugged(portPath);
    settle(portPath);
    // Rows 12 and 25: a result is dropped with the iPod. An active operation
    // keeps its record until it ends.
    runner_->forget(portPath);
}

void DeviceStateModel::onOperation(const Operation &operation)
{
    settle(operation.portPath);
    update();
}

void DeviceStateModel::settle(const QString &portPath)
{
    const std::optional<Operation> op = runner_->operation(portPath);
    if (!op || op->active())
        return;
    if (op->status == OperationStatus::Done && op->kind == OperationKind::Eject) {
        // Row 10: the aim of an eject is that the drive goes away, so the
        // record outlives the iPod.
        released_.insert(portPath, ReleasedRecord{op->endMs.value_or(clock_->nowMs())});
        runner_->forget(portPath);
    } else if (op->deviceLeft) {
        // Row 12: the result of an operation on an iPod that left is not shown.
        runner_->forget(portPath);
    }
}

void DeviceStateModel::update()
{
    // The ports that may have an entry, the ones already shown first.
    QStringList ports = order_;
    const auto add = [&ports](const QString &port) {
        if (!ports.contains(port))
            ports << port;
    };
    for (const DetectedIpod &ipod : tracker_->ipods())
        add(ipod.portPath);
    for (const Operation &op : runner_->operations())
        add(op.portPath);
    for (auto it = released_.cbegin(); it != released_.cend(); ++it)
        add(it.key());

    QStringList order;
    QList<QPair<QString, ScreenState>> changes;
    QHash<QString, ScreenState> now;
    for (const QString &port : std::as_const(ports)) {
        const std::optional<ScreenEntry> item = compute(port);
        const auto before = shown_.constFind(port);
        if (!item) {
            if (before != shown_.constEnd())
                changes << qMakePair(port, ScreenState());
            continue;
        }
        order << port;
        now.insert(port, item->state);
        if (before == shown_.constEnd() || *before != item->state)
            changes << qMakePair(port, item->state);
    }
    order_ = order;
    shown_ = now;

    // The one delay that the driver must know about may have moved.
    const std::optional<qint64> wake = nextWakeMs();
    const bool wakeMoved = wake != lastWake_;
    lastWake_ = wake;

    for (const auto &change : std::as_const(changes))
        emit stateChanged(change.first, change.second);
    if (!changes.isEmpty())
        emit listChanged();
    if (wakeMoved)
        emit wakeChanged();
}

} // namespace tacetos::device
