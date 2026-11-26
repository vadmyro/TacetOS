// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "address.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// One read of a file of the channel, with the rules of
// docs/specs/update-channel.md section 4: HTTPS only, a few redirects, a time
// out when no byte comes, a size limit.

namespace tacetos::update {

struct ChannelRead
{
    enum class Outcome {
        // `data` holds the whole file.
        Read,
        // HTTP 404, or no such file in the folder: "no update", not an error.
        Missing,
        // Anything else: `reason` says what, for the log.
        Failed,
    };

    Outcome outcome = Outcome::Failed;
    QByteArray data;
    QString reason;
    // Failed because the file is larger than its limit: the size given for it
    // is wrong, the download is damaged.
    bool overLimit = false;
};

class ChannelReader : public QObject
{
    Q_OBJECT

public:
    // `network` is the caller's and outlives the reader.
    explicit ChannelReader(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ChannelReader() override;

    // How long a read may get no byte (kChannelTimeoutMs), for the tests.
    void setTimeout(qint64 ms) { m_timeoutMs = ms; }

    // Reads the file `name` of the channel, at most `limit` bytes: a larger
    // file fails, and the rest of it is never kept. finished() comes once, after
    // read() has returned. A read already in progress is dropped first.
    void read(const ChannelAddress &address, const QString &name, qint64 limit);

    // Drops the read in progress: finished() does not come.
    void cancel();

signals:
    void finished(const tacetos::update::ChannelRead &result);

private:
    void readFolder(const QString &path);
    void request(const QUrl &url);
    void onMetaData();
    void onReadyRead();
    void onFinished();
    void onTimeout();
    void finish(ChannelRead::Outcome outcome, const QString &reason, bool overLimit = false);
    void dropReply();

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    qint64 m_timeoutMs;
    ChannelAddress m_address;
    QUrl m_url;
    qint64 m_limit = 0;
    int m_redirects = 0;
    QByteArray m_data;
    // Counts the reads, so that a queued end of a dropped read does nothing.
    int m_run = 0;
};

} // namespace tacetos::update
