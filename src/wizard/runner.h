// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "diagnose.h"
#include "progress.h"
#include "step.h"

#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <memory>
#include <optional>

// Runs the steps of a flow one after another (docs/specs/install-wizard.md
// sections 2.4 and 3).

namespace tacetos::wizard {

class WizardRunner : public QObject
{
    Q_OBJECT

public:
    // `dataDir` is the data folder of the app: the progress files and the locks
    // go in its `wizard/` folder.
    WizardRunner(const QString &dataDir, StepFactory factory, QObject *parent = nullptr);

    // Destroying the runner in the middle of a step is a cut: the step is asked
    // to stop, and nothing more is written in the progress file.
    ~WizardRunner() override;

    void setAppVersion(const QString &version) { m_appVersion = version; }

    struct Start
    {
        QString serial;
        QString model;
        FlowShape shape;
        QString packageManifest;
        QString packageSha256;
    };

    // Starts a new flow. Returns an empty text, or the code of section 13
    // (`no-serial`, `flow-running`, `bad-flow`).
    QString start(const Start &start);

    // Section 3.3: resumes the flow of the progress file of this serial.
    // Returns an empty text, or `no-progress`, `progress-refused` (the file is
    // renamed, the reason in `refusedReason()`), `progress-mismatch` (the
    // iPod is not of the family and model of the file) or `flow-running`.
    QString resume(const QString &serial, const QString &model, Family family);

    QString refusedReason() const { return m_refusedReason; }

    // Answers the wait of the running step.
    void answer(const UserAnswer &answer);

    // Section 2.4 rule 5: a wait is answered as cancelled, a step stops at its
    // next stopping point.
    void cancel();

    bool isRunning() const { return m_thread != nullptr && m_thread->isRunning(); }

    // Waits for the worker to end, for the tests.
    bool waitFinished(unsigned long ms);

signals:
    void stepStarted(int index, const QString &name);
    void stepProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    // Screens section 1.2: the step made its first write to the iPod, so it can no longer be
    // cancelled. Once per step.
    void stepFirstWrite(const QString &name);
    void stepWaiting(const QString &name, tacetos::wizard::WaitKind kind, const QJsonObject &info);
    // Screens section 3: the checks of the step `diagnose`, each time it runs them.
    void checksReported(const QList<tacetos::wizard::Check> &checks);
    void stepDone(int index, const QString &name);
    void stepFailed(int index, const QString &name, const QString &code, const QString &detail);
    // Every step is done: the progress file is deleted. `summary` is the facts
    // of `finish` (section 11.2), for the last screen.
    void flowFinished(const QJsonObject &summary);
    // The flow stopped on a failure or a cancel. The progress file stays.
    void flowStopped(const QString &code);

private:
    class Context;
    friend class Context;

    QString claim(const QString &serial);
    void release();
    void launch(bool resuming);
    void work(bool resuming);
    bool runOne(int index, bool resume, bool writeProgress);
    void save();
    UserAnswer waitForUser(const QString &name, WaitKind kind, const QJsonObject &info);
    template <typename F> void post(F &&f);

    QString m_dataDir;
    ProgressStore m_store;
    StepFactory m_factory;
    QString m_appVersion;
    QString m_refusedReason;

    Progress m_progress;
    QString m_claimed;
    std::unique_ptr<QLockFile> m_lock;
    QThread *m_thread = nullptr;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_abort{false};
    QMutex m_mutex;
    QWaitCondition m_answered;
    std::optional<UserAnswer> m_answer;
    bool m_waiting = false;
};

} // namespace tacetos::wizard
