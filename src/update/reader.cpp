// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "reader.h"

#include "constants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#if QT_CONFIG(ssl)
#include <QSslSocket>
#endif

namespace tacetos::update {

namespace {

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

int statusOf(const QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QString tooLarge(qint64 limit)
{
    return QStringLiteral("the file is larger than its limit of %1 bytes").arg(limit);
}

} // namespace

ChannelReader::ChannelReader(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network), m_timeoutMs(device::kChannelTimeoutMs)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ChannelReader::onTimeout);
}

ChannelReader::~ChannelReader()
{
    dropReply();
}

void ChannelReader::read(const ChannelAddress &address, const QString &name, qint64 limit)
{
    cancel();
    m_address = address;
    m_limit = limit;
    m_redirects = 0;
    m_data.clear();

    if (address.kind == ChannelAddress::Kind::Folder) {
        const QString path = QDir(address.folder).filePath(name);
        // After read() has returned, as for a read on the network.
        const int run = m_run;
        QMetaObject::invokeMethod(
            this,
            [this, run, path] {
                if (run == m_run)
                    readFolder(path);
            },
            Qt::QueuedConnection);
        return;
    }
#if QT_CONFIG(ssl)
    if (address.kind == ChannelAddress::Kind::Remote && !QSslSocket::supportsSsl()) {
        const int run = m_run;
        QMetaObject::invokeMethod(
            this,
            [this, run] {
                if (run == m_run)
                    finish(ChannelRead::Outcome::Failed,
                           QStringLiteral("this system has no TLS support for Qt"));
            },
            Qt::QueuedConnection);
        return;
    }
#endif
    request(address.url.resolved(QUrl(name)));
}

void ChannelReader::cancel()
{
    // A read of a folder that is queued but not run yet sees the new number.
    ++m_run;
    dropReply();
    m_timer.stop();
}

void ChannelReader::readFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        finish(ChannelRead::Outcome::Missing, QStringLiteral("no file %1").arg(path));
        return;
    }
    if (!info.isFile()) {
        finish(ChannelRead::Outcome::Failed, QStringLiteral("%1 is not a file").arg(path));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        finish(ChannelRead::Outcome::Failed, file.errorString());
        return;
    }
    // One byte more than the limit tells a file that is too large, even one that
    // grows while it is read.
    m_data = file.read(m_limit + 1);
    if (m_data.size() > m_limit) {
        m_data.clear();
        finish(ChannelRead::Outcome::Failed, tooLarge(m_limit), true);
        return;
    }
    finish(ChannelRead::Outcome::Read, QString());
}

void ChannelReader::request(const QUrl &url)
{
    m_url = url;
    m_data.clear();
    QNetworkRequest request(url);
    // Each redirect is checked here (section 4), never followed by Qt alone.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, &ChannelReader::onMetaData);
    connect(m_reply, &QNetworkReply::readyRead, this, &ChannelReader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ChannelReader::onFinished);
    m_timer.start(m_timeoutMs);
}

void ChannelReader::onMetaData()
{
    m_timer.start(m_timeoutMs);
    const int status = statusOf(m_reply);
    if (status < 200 || status >= 300)
        return;
    bool known = false;
    const qint64 length = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&known);
    if (known && length > m_limit)
        finish(ChannelRead::Outcome::Failed, tooLarge(m_limit), true);
}

void ChannelReader::onReadyRead()
{
    m_timer.start(m_timeoutMs);
    const int status = statusOf(m_reply);
    if (status < 200 || status >= 300) {
        // The body of a redirect or of an error is not the file.
        m_reply->readAll();
        return;
    }
    m_data.append(m_reply->read(m_limit + 1 - m_data.size()));
    if (m_data.size() > m_limit)
        finish(ChannelRead::Outcome::Failed, tooLarge(m_limit), true);
}

void ChannelReader::onFinished()
{
    QNetworkReply *reply = m_reply;
    const int status = statusOf(reply);

    if (isRedirect(status)) {
        const QUrl target =
            m_url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        dropReply();
        if (++m_redirects > device::kRedirectMax) {
            finish(ChannelRead::Outcome::Failed,
                   QStringLiteral("more than %1 redirects").arg(device::kRedirectMax));
            return;
        }
        // Only to HTTPS. A server on the computer may also send to another
        // address on the computer: the fake server of the tests.
        const QString scheme = target.scheme().toLower();
        const bool local = m_address.kind == ChannelAddress::Kind::Local &&
                           scheme == QLatin1String("http") && isThisComputer(target);
        if (!target.isValid() || (scheme != QLatin1String("https") && !local)) {
            finish(ChannelRead::Outcome::Failed,
                   QStringLiteral("a redirect to %1 is refused: it is not https")
                       .arg(target.toString()));
            return;
        }
        request(target);
        return;
    }

    if (status == 404) {
        dropReply();
        finish(ChannelRead::Outcome::Missing,
               QStringLiteral("HTTP 404 for %1").arg(m_url.toString()));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString reason = reply->errorString();
        dropReply();
        finish(ChannelRead::Outcome::Failed, reason);
        return;
    }
    if (status < 200 || status >= 300) {
        dropReply();
        finish(ChannelRead::Outcome::Failed, QStringLiteral("HTTP %1").arg(status));
        return;
    }
    m_data.append(reply->read(m_limit + 1 - m_data.size()));
    dropReply();
    if (m_data.size() > m_limit) {
        finish(ChannelRead::Outcome::Failed, tooLarge(m_limit), true);
        return;
    }
    finish(ChannelRead::Outcome::Read, QString());
}

void ChannelReader::onTimeout()
{
    finish(ChannelRead::Outcome::Failed, QStringLiteral("no byte for %1 ms").arg(m_timeoutMs));
}

void ChannelReader::finish(ChannelRead::Outcome outcome, const QString &reason, bool overLimit)
{
    dropReply();
    m_timer.stop();
    ChannelRead result;
    result.outcome = outcome;
    result.reason = reason;
    result.overLimit = overLimit;
    if (outcome == ChannelRead::Outcome::Read)
        result.data = m_data;
    m_data.clear();
    emit finished(result);
}

void ChannelReader::dropReply()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

} // namespace tacetos::update
