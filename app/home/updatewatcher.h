// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "address.h"
#include "models.h"
#include "reader.h"
#include "screenstate.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace tacetos::journal {
class Journal;
}

namespace tacetos {

// When the home screen reads the update channel (docs/specs/update-channel.md section 8): once
// per launch, the first time an iPod whose firmware line is `tacetos` is shown, and again on each
// Refresh. Without such an iPod nothing is read. One read of the index serves every iPod; each
// one gets one log line with its result, and a failure only shows nothing.
class UpdateWatcher : public QObject
{
    Q_OBJECT

public:
    // `journal` may be null and outlives the watcher.
    UpdateWatcher(const update::ChannelAddress &address, journal::Journal *journal,
                  QObject *parent = nullptr);
    ~UpdateWatcher() override;

    // The entries the home screen shows now.
    void show(const QList<device::ScreenEntry> &entries, const QList<device::Model> &models);
    // Refresh: reads the index again when a `tacetos` iPod is shown.
    void refresh();

    // The version the channel has for the iPod of this port, empty when there is no update.
    QString updateFor(const QString &port) const;

    // How many reads of the index were started, for the tests.
    int reads() const { return reads_; }

signals:
    // The update of this port changed (updateFor).
    void updateChanged(const QString &port);

private:
    // What is compared for one iPod.
    struct Ipod
    {
        QString target;
        QString version;
        bool operator==(const Ipod &other) const
        {
            return target == other.target && version == other.version;
        }
    };
    struct Result
    {
        Ipod ipod;
        QString update;
    };

    void startRead();
    void onRead(const update::ChannelRead &read);
    // Compares the last read with each iPod shown that has no result for what it runs.
    void conclude();

    update::ChannelAddress address_;
    journal::Journal *journal_;
    std::unique_ptr<QNetworkAccessManager> network_;
    std::unique_ptr<update::ChannelReader> reader_;
    bool reading_ = false;
    int reads_ = 0;
    std::optional<update::ChannelRead> read_;
    // The `tacetos` iPods shown now, by port.
    QHash<QString, Ipod> shown_;
    QHash<QString, Result> results_;
};

} // namespace tacetos
