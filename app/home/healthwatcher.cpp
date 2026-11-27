// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/healthwatcher.h"

#include "journal.h"

#include <QDateTime>

namespace tacetos {

using namespace device;

HealthWatcher::HealthWatcher(const QString &dataFolder, journal::Journal *journal, QObject *parent)
    : QObject(parent), dataFolder_(dataFolder), journal_(journal)
{
}

void HealthWatcher::show(const QList<ScreenEntry> &entries, const QList<Model> &models)
{
    QHash<QString, Shown> now;
    QList<QString> busy;
    for (const ScreenEntry &entry : entries) {
        if (!entry.ipod || entry.ipod->firmwareLine.kind != FirmwareKind::Tacetos ||
            !entry.ipod->volume || entry.ipod->volume->mountPoint.isEmpty())
            continue;
        // A busy card keeps what was read on `ready`, and reads nothing.
        if (entry.state.kind == ScreenKind::Busy && entry.ipod->state == IpodState::Ready) {
            busy << entry.portPath;
            continue;
        }
        if (entry.state.kind != ScreenKind::Ready)
            continue;
        Shown shown;
        shown.mountPoint = entry.ipod->volume->mountPoint;
        shown.usbSerial = entry.ipod->serial;
        shown.runningOurs = entry.ipod->running == RunningFirmware::Other;
        for (const Model &m : models)
            if (m.id == entry.ipod->model)
                shown.folder = m.firmwareFolder;
        now.insert(entry.portPath, shown);
    }

    // The iPods that left `ready` for something else than a short operation.
    const QList<QString> ports = shown_.keys();
    for (const QString &port : ports) {
        if (now.contains(port) || busy.contains(port))
            continue;
        shown_.remove(port);
        if (states_.remove(port))
            emit healthChanged(port);
    }

    for (auto it = now.cbegin(); it != now.cend(); ++it) {
        const auto old = shown_.constFind(it.key());
        const bool same = old != shown_.cend() && old->mountPoint == it->mountPoint &&
                          old->usbSerial == it->usbSerial && old->runningOurs == it->runningOurs;
        if (same)
            continue;
        shown_.insert(it.key(), *it);
        read(it.key(), *it);
    }
}

void HealthWatcher::refresh()
{
    for (auto it = shown_.cbegin(); it != shown_.cend(); ++it)
        read(it.key(), *it);
}

std::optional<HealthState> HealthWatcher::stateFor(const QString &port) const
{
    const auto it = states_.constFind(port);
    if (it == states_.cend())
        return std::nullopt;
    return *it;
}

void HealthWatcher::read(const QString &port, const Shown &shown)
{
    ++reads_;
    const health::HealthFiles files = health::readHealthFiles(shown.mountPoint, shown.folder);
    QStringList log = files.log;
    if (files.volumeGone) {
        if (states_.remove(port))
            emit healthChanged(port);
        return;
    }

    HealthState state;
    state.reading =
        health::makeReading(files.health, files.charge, QDateTime::currentDateTimeUtc());
    state.runningOurs = shown.runningOurs;
    log << state.reading.log;
    if (state.reading.ok) {
        const QString key = health::historyKey(state.reading, shown.usbSerial);
        if (dataFolder_.isEmpty() || key.isEmpty())
            health::addReading(&state.history, state.reading);
        else
            state.history = health::recordReading(dataFolder_, key, state.reading, &log);
    }
    if (journal_)
        for (const QString &line : std::as_const(log))
            journal_->add(journal::Level::Warning, journal::Domain::Disk, port, line);

    states_.insert(port, state);
    emit healthChanged(port);
}

} // namespace tacetos
