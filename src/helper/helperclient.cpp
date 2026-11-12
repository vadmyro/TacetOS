// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "helperclient.h"

#include "helperconstants.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace tacetos::helper {

HelperClient::HelperClient(QObject *parent)
    : QObject(parent), m_expectedVersion(helperVersion()),
      m_env(QProcessEnvironment::systemEnvironment())
{
    m_helloTimer.setSingleShot(true);
    m_helloTimer.setInterval(kHelloTimeoutMs);
    connect(&m_helloTimer, &QTimer::timeout, this, [this] {
        failAll(QString(err::helperSilent),
                QStringLiteral("the helper did not say hello within %1 ms").arg(kHelloTimeoutMs));
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &HelperClient::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errBuffer += m_process.readAllStandardError();
        int at;
        while ((at = m_errBuffer.indexOf('\n')) >= 0) {
            const QString text = QString::fromUtf8(m_errBuffer.left(at));
            m_errBuffer.remove(0, at + 1);
            if (m_journal && !text.trimmed().isEmpty())
                m_journal->add(journal::Level::Warning, journal::Domain::Op, QString(),
                               QStringLiteral("helper: ") + text);
        }
    });
    connect(&m_process, &QProcess::finished, this, [this] { onProcessFinished(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onProcessFinished();
    });
}

HelperClient::~HelperClient()
{
    shutdown(2000);
}

QString HelperClient::defaultProgram()
{
    QString name = QStringLiteral("tacet-helper");
#if defined(Q_OS_WIN)
    name += QStringLiteral(".exe");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

QJsonObject HelperClient::imageTarget(const QString &path, int sectorSize)
{
    QJsonObject t;
    t.insert(QStringLiteral("kind"), QStringLiteral("image"));
    t.insert(QStringLiteral("path"), path);
    t.insert(QStringLiteral("sectorSize"), sectorSize);
    return t;
}

int HelperClient::readImageState(const QString &path, int sectorSize, const QString &port)
{
    QJsonObject args;
    args.insert(QStringLiteral("target"), imageTarget(path, sectorSize));
    return request(QString(cmd::readState), args, port);
}

int HelperClient::request(const QString &cmd, const QJsonObject &args, const QString &port)
{
    const int id = m_nextId;
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("cmd"), cmd);
    o.insert(QStringLiteral("args"), args);
    return requestRaw(eventLine(o), port);
}

int HelperClient::requestRaw(const QByteArray &line, const QString &port)
{
    const int id = m_nextId++;
    m_pending.insert(id, port);
    if (m_state == State::Stopped)
        start();
    if (m_state == State::Ready)
        write(line);
    else if (m_state == State::Starting)
        m_queued.append(line);
    else
        QTimer::singleShot(
            0, this, [this, id] { completeWith(id, QString(err::helperMissing), QString()); });
    return id;
}

void HelperClient::start()
{
    m_hello = {};
    m_buffer.clear();
    m_queued.clear();
    m_shuttingDown = false;
    const QString program = m_program.isEmpty() ? defaultProgram() : m_program;
    if (!QFileInfo(program).isFile()) {
        if (m_journal)
            m_journal->add(journal::Level::Error, journal::Domain::Op, QString(),
                           QStringLiteral("the helper program is missing: ") + program);
        m_state = State::Stopped;
        return;
    }
    m_process.setProcessEnvironment(m_env);
    m_state = State::Starting;
    m_process.start(program, {});
    m_helloTimer.start();
}

void HelperClient::write(const QByteArray &line)
{
    m_process.write(line);
}

void HelperClient::readOutput()
{
    m_buffer += m_process.readAllStandardOutput();
    int at;
    while ((at = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(at);
        m_buffer.remove(0, at + 1);
        handleLine(line);
    }
}

void HelperClient::handleLine(const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        if (m_journal)
            m_journal->add(journal::Level::Warning, journal::Domain::Op, QString(),
                           QStringLiteral("the helper wrote a line that is not JSON"));
        return;
    }
    const QJsonObject event = doc.object();
    const QString kind = event.value(QStringLiteral("event")).toString();
    if (kind == QLatin1String("hello")) {
        handleHello(event);
        return;
    }
    qint64 idValue = 0;
    isInteger(event.value(QStringLiteral("id")), &idValue);
    const int id = int(idValue);
    const QString port = m_pending.value(id);
    if (kind == QLatin1String("log") && m_journal) {
        const QString level = event.value(QStringLiteral("level")).toString();
        m_journal->add(level == QLatin1String("error")     ? journal::Level::Error
                       : level == QLatin1String("warning") ? journal::Level::Warning
                                                           : journal::Level::Info,
                       journal::Domain::Op, port, event.value(QStringLiteral("text")).toString());
    }
    emit eventReceived(id, event);
    if (kind == QLatin1String("result")) {
        m_pending.remove(id);
        emit finished(id, event);
    }
}

void HelperClient::handleHello(const QJsonObject &hello)
{
    m_helloTimer.stop();
    qint64 protocol = 0;
    isInteger(hello.value(QStringLiteral("protocol")), &protocol);
    if (protocol != kProtocol ||
        hello.value(QStringLiteral("version")).toString() != m_expectedVersion) {
        // Never talk to a helper of another version (section 3.2).
        failAll(QString(err::helperMismatch),
                QStringLiteral("the helper reports protocol %1, version %2")
                    .arg(protocol)
                    .arg(hello.value(QStringLiteral("version")).toString()));
        m_shuttingDown = true;
        m_process.kill();
        return;
    }
    m_hello = hello;
    m_state = State::Ready;
    for (const QByteArray &line : std::as_const(m_queued))
        write(line);
    m_queued.clear();
    emit helloReceived(hello);
}

void HelperClient::completeWith(int id, const QString &code, const QString &detail)
{
    if (!m_pending.contains(id))
        return;
    const QString port = m_pending.take(id);
    if (m_journal)
        m_journal->add(journal::Level::Error, journal::Domain::Op, port,
                       QStringLiteral("%1: %2").arg(code, detail));
    QJsonObject result;
    result.insert(QStringLiteral("event"), QStringLiteral("result"));
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"), code);
    result.insert(QStringLiteral("detail"), detail);
    emit eventReceived(id, result);
    emit finished(id, result);
}

void HelperClient::failAll(const QString &code, const QString &detail)
{
    const QList<int> ids = m_pending.keys();
    for (const int id : ids)
        completeWith(id, code, detail);
    m_queued.clear();
}

void HelperClient::onProcessFinished()
{
    m_helloTimer.stop();
    readOutput();
    const bool expected = m_shuttingDown;
    m_state = State::Stopped;
    m_queued.clear();
    // Section 3.7: a request that was running gets `helper-crashed`, and the next
    // request starts a new helper.
    if (!expected && !m_pending.isEmpty())
        failAll(QString(err::helperCrashed),
                QStringLiteral("the helper ended with code %1").arg(m_process.exitCode()));
    else
        m_pending.clear();
}

void HelperClient::shutdown(int waitMs)
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_shuttingDown = true;
    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(waitMs))
        m_process.kill();
    m_process.waitForFinished(1000);
}

} // namespace tacetos::helper
