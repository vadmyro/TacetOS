// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "helpercalls.h"

#include "helperclient.h"
#include "helperconstants.h"

#include <QEventLoop>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

namespace tacetos::wizard {

namespace {

// How often a waiting call looks at `stop`.
constexpr int kStopLookMs = 100;

HelperAnswer answerOf(const QJsonObject &result)
{
    HelperAnswer answer;
    if (!result.value(QStringLiteral("ok")).toBool()) {
        answer.code = result.value(QStringLiteral("error")).toString();
        answer.detail = result.value(QStringLiteral("detail")).toString();
        if (answer.code.isEmpty())
            answer.code = QString(helper::err::badRequest);
    }
    answer.data = result.value(QStringLiteral("data")).toObject();
    return answer;
}

} // namespace

HelperAnswer callHelper(helper::HelperClient &client, const QString &cmd, const QJsonObject &args,
                        const QString &port, const HelperWatch &watch)
{
    using helper::HelperClient;
    QMutex mutex;
    QWaitCondition arrived;
    // By id: a request that fails at once has its result before its id is known.
    QHash<int, QJsonObject> results;
    QHash<int, QList<QJsonObject>> events;
    int id = -1;
    bool cancelSent = false;
    QEventLoop loop;
    const bool sameThread = QThread::currentThread() == client.thread();

    const auto wantsStop = [&] { return !cancelSent && watch.stop && watch.stop(); };
    const auto drain = [&](QMutexLocker<QMutex> &locker) {
        QList<QJsonObject> mine = events.take(id);
        locker.unlock();
        for (const QJsonObject &event : std::as_const(mine)) {
            if (watch.progress)
                watch.progress(event);
        }
        locker.relock();
    };

    const QMetaObject::Connection onResult = QObject::connect(
        &client, &HelperClient::finished, &client,
        [&](int got, const QJsonObject &result) {
            QMutexLocker locker(&mutex);
            results.insert(got, result);
            arrived.wakeAll();
            if (sameThread && got == id)
                loop.quit();
        },
        Qt::DirectConnection);
    const QMetaObject::Connection onEvent = QObject::connect(
        &client, &HelperClient::eventReceived, &client,
        [&](int got, const QJsonObject &event) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("progress"))
                return;
            if (sameThread) {
                if (got == id && watch.progress)
                    watch.progress(event);
                return;
            }
            QMutexLocker locker(&mutex);
            events[got] << event;
            arrived.wakeAll();
        },
        Qt::DirectConnection);

    QJsonObject result;
    if (sameThread) {
        id = client.request(cmd, args, port);
        if (!results.contains(id)) {
            QTimer look;
            look.setInterval(kStopLookMs);
            QObject::connect(&look, &QTimer::timeout, [&] {
                if (wantsStop()) {
                    cancelSent = true;
                    client.cancel(id);
                }
            });
            look.start();
            loop.exec();
        }
        result = results.value(id);
    } else {
        QMetaObject::invokeMethod(
            &client,
            [&] {
                const int sent = client.request(cmd, args, port);
                QMutexLocker locker(&mutex);
                id = sent;
            },
            Qt::BlockingQueuedConnection);
        QMutexLocker locker(&mutex);
        while (!results.contains(id)) {
            drain(locker);
            if (wantsStop()) {
                cancelSent = true;
                const int running = id;
                QMetaObject::invokeMethod(
                    &client, [&client, running] { client.cancel(running); }, Qt::QueuedConnection);
            }
            if (!results.contains(id))
                arrived.wait(&mutex, kStopLookMs);
        }
        drain(locker);
        result = results.value(id);
    }
    QObject::disconnect(onResult);
    QObject::disconnect(onEvent);
    return answerOf(result);
}

HelperAnswer HelperCommands::callWithToken(const QString &cmd, QJsonObject args,
                                           const HelperWatch &watch)
{
    const HelperAnswer state = call(QString(helper::cmd::readState), QJsonObject());
    if (!state.ok())
        return state;
    args.insert(QStringLiteral("scanToken"), state.data.value(QStringLiteral("scanToken")));
    return call(cmd, args, watch);
}

ClientHelperCommands::ClientHelperCommands(helper::HelperClient &client, const QJsonObject &target,
                                           const QString &port)
    : m_client(client), m_target(target), m_port(port)
{
}

HelperAnswer ClientHelperCommands::call(const QString &cmd, const QJsonObject &args,
                                        const HelperWatch &watch)
{
    QJsonObject withTarget = args;
    withTarget.insert(QStringLiteral("target"), m_target);
    return callHelper(m_client, cmd, withTarget, m_port, watch);
}

} // namespace tacetos::wizard
