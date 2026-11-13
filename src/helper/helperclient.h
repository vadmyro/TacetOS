// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "journal.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

// The client of the helper in the app (docs/specs/helper.md section 4): starts
// the program, waits for its hello, gives each request its id, calls back with
// each event, turns the `log` events into entries of the journal, and gives a
// request the result `helper-crashed` when the program goes away under it.

namespace tacetos::helper {

class HelperClient : public QObject
{
    Q_OBJECT

public:
    // `journal` may be null. The version is the one the helper must report.
    explicit HelperClient(QObject *parent = nullptr);
    ~HelperClient() override;

    // Section 2.3: the one place the app looks, the folder of its own executable.
    static QString defaultProgram();

    void setProgram(const QString &path) { m_program = path; }
    void setJournal(journal::Journal *journal) { m_journal = journal; }
    // The version the hello must carry (default: the one of this build).
    void setExpectedVersion(const QString &version) { m_expectedVersion = version; }
    // Starts the helper with this extra environment, for the tests.
    void setEnvironment(const QProcessEnvironment &env) { m_env = env; }

    // The target objects of section 5.1.
    static QJsonObject imageTarget(const QString &path, int sectorSize);

    // Sends a request and returns its id. The helper is started when it is not
    // running. `port` is the port path of the iPod, for the journal. Every request
    // gets exactly one `finished`.
    int request(const QString &cmd, const QJsonObject &args, const QString &port = QString());
    // Sends a line as it is, to try the refusals of section 3.5. Returns its id.
    int requestRaw(const QByteArray &line, const QString &port = QString());

    // Section 6.12: asks the running request `id` to stop. Its own result says
    // whether it was heard (`too-late` after the first write).
    int cancel(int id);

    // The command `read-state` on an image file.
    int readImageState(const QString &path, int sectorSize, const QString &port = QString());

    // The hello of the running helper, empty before it came.
    QJsonObject hello() const { return m_hello; }
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }

    // Closes the input of the helper and waits for it to end. Called when the app quits.
    void shutdown(int waitMs = 5000);

signals:
    // Every event of a request, `result` included, with its id.
    void eventReceived(int id, const QJsonObject &event);
    // The result of a request: `ok`, then `data`, or `error` and `detail`.
    void finished(int id, const QJsonObject &result);
    void helloReceived(const QJsonObject &hello);

private:
    enum class State { Stopped, Starting, Ready };

    void start();
    void readOutput();
    void handleLine(const QByteArray &line);
    void handleHello(const QJsonObject &hello);
    void failAll(const QString &code, const QString &detail);
    void completeWith(int id, const QString &code, const QString &detail);
    void onProcessFinished();
    void write(const QByteArray &line);

    QProcess m_process;
    QTimer m_helloTimer;
    QString m_program;
    QString m_expectedVersion;
    QProcessEnvironment m_env;
    journal::Journal *m_journal = nullptr;
    State m_state = State::Stopped;
    QByteArray m_buffer;
    QByteArray m_errBuffer;
    QJsonObject m_hello;
    int m_nextId = 1;
    QList<QByteArray> m_queued;
    // The requests that have not got their result, with the port of each.
    QHash<int, QString> m_pending;
    bool m_shuttingDown = false;
};

} // namespace tacetos::helper
