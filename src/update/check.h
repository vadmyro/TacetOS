// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "address.h"
#include "index.h"
#include "reader.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tacetos::journal {
class Journal;
}

// Whether the channel has an update for one iPod (docs/specs/update-channel.md
// sections 5 and 8): it reads the index only, never a package.

namespace tacetos::update {

struct UpdateCheckResult
{
    // True when the index has the model and its version is newer than the
    // iPod's. Every other case is "no update", silent on the screen.
    bool update = false;
    // The version of the index, when there is an update.
    QString version;
    // The entry of the model, when there is an update.
    IndexPackage package;
    // The one log line of the check, with its reason.
    QString logLine;
};

// What one read of the index says for one iPod (`target` and `ipodVersion` as
// for UpdateCheck::check). Logs nothing: the caller writes `logLine`. One read
// can serve every iPod plugged in.
UpdateCheckResult resultOfRead(const ChannelRead &read, const QString &target,
                               const QString &ipodVersion);

class UpdateCheck : public QObject
{
    Q_OBJECT

public:
    // `network` is the caller's and outlives the check. `journal` may be null.
    UpdateCheck(QNetworkAccessManager *network, journal::Journal *journal,
                QObject *parent = nullptr);

    // How long a read may get no byte, for the tests.
    void setTimeout(qint64 ms) { m_reader.setTimeout(ms); }

    // Reads the index of `address` and compares it with the iPod: `target` is
    // its `bootloader_target`, `ipodVersion` the value of the TacetOS line of
    // its info file. finished() comes once, after check() has returned. A check
    // in progress is dropped first.
    void check(const ChannelAddress &address, const QString &target, const QString &ipodVersion);

signals:
    void finished(const tacetos::update::UpdateCheckResult &result);

private:
    void onRead(const ChannelRead &read);

    ChannelReader m_reader;
    journal::Journal *m_journal;
    QString m_target;
    QString m_ipodVersion;
};

} // namespace tacetos::update
