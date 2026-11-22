// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "automount.h"

namespace tacetos::device {

AutoMounter::AutoMounter(DeviceStateModel *model, LiveTracker *tracker, OperationRunner *runner,
                         bool enabled, QObject *parent)
    : QObject(parent), model_(model), tracker_(tracker), runner_(runner), enabled_(enabled)
{
    // A new plug in is a new arrival (rule 2). The tracker tells it before the
    // model sends the states that follow.
    connect(tracker_, &LiveTracker::ipodAdded, this,
            [this](const DetectedIpod &ipod) { spent_.remove(ipod.portPath); });
    connect(model_, &DeviceStateModel::stateChanged, this, &AutoMounter::onStateChanged);
    connect(runner_, &OperationRunner::operationChanged, this, &AutoMounter::onOperation);
}

void AutoMounter::onStateChanged(const QString &portPath, const ScreenState &state)
{
    const auto before = last_.constFind(portPath);
    const bool fromDetection =
        before != last_.constEnd() &&
        (before->kind == ScreenKind::Reading ||
         (before->kind == ScreenKind::Attention && before->reason == IpodState::NoDisk));
    if (state.kind == ScreenKind::Unplugged) {
        last_.remove(portPath);
        spent_.remove(portPath);
        pending_.remove(portPath);
        return;
    }
    last_.insert(portPath, state);

    // Rows 3 and 7 of section 1.3: only the first `not-mounted` of an arrival,
    // reached from detection. After an eject, an unmount or a failed mount, the
    // state comes from an operation or from `ready`, and stays as it is.
    if (state.kind != ScreenKind::NotMounted || !fromDetection || spent_.contains(portPath))
        return;
    spent_.insert(portPath);
    if (!enabled_ || tracker_->isHeld(portPath))
        return;

    starting_ = portPath;
    const OperationRequest answer = model_->startOperation(OperationKind::Mount, portPath);
    starting_.clear();
    // A runner that works at once has already dropped a failed record.
    const auto record = runner_->operation(portPath);
    if (answer.accepted && record && record->id == answer.id && record->active())
        pending_.insert(portPath, answer.id);
}

bool AutoMounter::ours(const Operation &operation) const
{
    if (operation.kind != OperationKind::Mount)
        return false;
    return operation.portPath == starting_ || pending_.value(operation.portPath) == operation.id;
}

// Rule 3: a refusal leaves `not-mounted` (the record is dropped) and one
// warning in the journal, with what the service said.
void AutoMounter::onOperation(const Operation &operation)
{
    if (operation.active() || !ours(operation))
        return;
    pending_.remove(operation.portPath);
    if (operation.status != OperationStatus::Failed)
        return;
    QString line =
        QStringLiteral("op %1 mount on arrival: the disk service refused").arg(operation.id);
    if (!operation.result.detail.isEmpty())
        line += QStringLiteral(": ") + operation.result.detail;
    emit logged(operation.portPath, {line});
    model_->dropFailed(operation.portPath);
}

} // namespace tacetos::device
