// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/updatewatcher.h"

#include "check.h"
#include "constants.h"
#include "journal.h"

#include <QNetworkAccessManager>

namespace tacetos {

using namespace device;

UpdateWatcher::UpdateWatcher(const update::ChannelAddress &address, journal::Journal *journal,
                             QObject *parent)
    : QObject(parent), address_(address), journal_(journal)
{
}

UpdateWatcher::~UpdateWatcher() = default;

void UpdateWatcher::show(const QList<ScreenEntry> &entries, const QList<Model> &models)
{
    QHash<QString, Ipod> shown;
    for (const ScreenEntry &entry : entries) {
        if (!entry.ipod || entry.ipod->firmwareLine.kind != FirmwareKind::Tacetos ||
            entry.ipod->firmware.version.isEmpty())
            continue;
        for (const Model &m : models) {
            if (m.id == entry.ipod->model && !m.bootloaderTarget.isEmpty())
                shown.insert(entry.portPath, {m.bootloaderTarget, entry.ipod->firmware.version});
        }
    }
    // An iPod that left, or that is no longer a `tacetos` one, loses its update.
    const QList<QString> ports = results_.keys();
    for (const QString &port : ports) {
        if (shown.contains(port))
            continue;
        const bool had = !results_.take(port).update.isEmpty();
        if (had)
            emit updateChanged(port);
    }
    shown_ = shown;
    if (shown_.isEmpty())
        return;
    if (read_)
        conclude();
    else if (!reading_ && reads_ == 0)
        startRead();
}

void UpdateWatcher::refresh()
{
    if (shown_.isEmpty())
        return;
    startRead();
}

QString UpdateWatcher::updateFor(const QString &port) const
{
    return results_.value(port).update;
}

void UpdateWatcher::startRead()
{
    if (!reader_) {
        network_ = std::make_unique<QNetworkAccessManager>();
        reader_ = std::make_unique<update::ChannelReader>(network_.get());
        connect(reader_.get(), &update::ChannelReader::finished, this, &UpdateWatcher::onRead);
    }
    ++reads_;
    reading_ = true;
    reader_->read(address_, QString(kUpdateIndexFile), kIndexMaxBytes);
}

void UpdateWatcher::onRead(const update::ChannelRead &read)
{
    reading_ = false;
    read_ = read;
    // A new read: every iPod gets its result again.
    for (Result &result : results_)
        result.ipod = Ipod();
    conclude();
}

void UpdateWatcher::conclude()
{
    for (auto it = shown_.cbegin(); it != shown_.cend(); ++it) {
        const auto known = results_.constFind(it.key());
        if (known != results_.cend() && known->ipod == it.value())
            continue;
        const update::UpdateCheckResult result =
            update::resultOfRead(*read_, it.value().target, it.value().version);
        if (journal_)
            journal_->add(journal::Level::Info, journal::Domain::App, it.key(), result.logLine);
        const QString before = known != results_.cend() ? known->update : QString();
        const QString now = result.update ? result.version : QString();
        results_.insert(it.key(), {it.value(), now});
        if (before != now)
            emit updateChanged(it.key());
    }
}

} // namespace tacetos
