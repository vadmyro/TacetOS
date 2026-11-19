// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "runner.h"

#include "wizarderrors.h"

#include <QDir>
#include <QMutexLocker>
#include <QSet>

namespace tacetos::wizard {

namespace {

// Section 2.4 rule 4: one runner per serial in this process. The lock file
// does the same between two copies of the app.
QMutex &registryMutex()
{
    static QMutex mutex;
    return mutex;
}

QSet<QString> &registry()
{
    static QSet<QString> serials;
    return serials;
}

} // namespace

class WizardRunner::Context : public StepContext
{
public:
    Context(WizardRunner *runner, const QString &name, bool resume, bool writeProgress)
        : m_runner(runner), m_name(name), m_resume(resume), m_writeProgress(writeProgress)
    {
    }

    QString serial() const override { return m_runner->m_progress.serial; }
    QString model() const override { return m_runner->m_progress.model; }
    FlowShape shape() const override { return m_runner->m_progress.shape; }
    bool isResume() const override { return m_resume; }
    bool stopRequested() const override { return m_runner->m_cancel || m_runner->m_abort; }

    void progress(const QString &stage, qint64 done, qint64 total) override
    {
        const QString name = m_name;
        m_runner->post([runner = m_runner, name, stage, done, total] {
            emit runner->stepProgress(name, stage, done, total);
        });
    }

    void firstWrite() override
    {
        if (m_wrote.exchange(true))
            return;
        const QString name = m_name;
        m_runner->post([runner = m_runner, name] { emit runner->stepFirstWrite(name); });
    }

    void reportChecks(const QList<Check> &checks) override
    {
        m_runner->post([runner = m_runner, checks] { emit runner->checksReported(checks); });
    }

    UserAnswer waitForUser(WaitKind kind, const QJsonObject &info) override
    {
        const bool write = m_writeProgress;
        Progress &progress = m_runner->m_progress;
        if (write) {
            progress.status = StepStatus::Waiting;
            m_runner->save();
        }
        const UserAnswer answer = m_runner->waitForUser(m_name, kind, info);
        if (write && !m_runner->m_abort) {
            progress.status = StepStatus::Running;
            m_runner->save();
        }
        return answer;
    }

    QJsonValue fact(const QString &step, const QString &key) const override
    {
        return m_runner->m_progress.facts.value(step).toObject().value(key);
    }

    void setFact(const QString &key, const QJsonValue &value) override
    {
        QJsonObject &facts = m_runner->m_progress.facts;
        QJsonObject mine = facts.value(m_name).toObject();
        mine.insert(key, value);
        facts.insert(m_name, mine);
        if (m_writeProgress)
            m_runner->save();
    }

    QString backupFolder() const override { return m_runner->m_progress.backup; }

    void setBackupFolder(const QString &path) override
    {
        m_runner->m_progress.backup = path;
        if (m_writeProgress)
            m_runner->save();
    }

    QString packageManifest() const override { return m_runner->m_progress.packageManifest; }
    QString packageSha256() const override { return m_runner->m_progress.packageSha256; }

private:
    WizardRunner *m_runner;
    QString m_name;
    bool m_resume;
    bool m_writeProgress;
    std::atomic<bool> m_wrote{false};
};

WizardRunner::WizardRunner(const QString &dataDir, StepFactory factory, QObject *parent)
    : QObject(parent), m_dataDir(dataDir), m_store(dataDir), m_factory(std::move(factory))
{
}

WizardRunner::~WizardRunner()
{
    m_abort = true;
    {
        QMutexLocker locker(&m_mutex);
        m_answered.wakeAll();
    }
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
    release();
}

template <typename F> void WizardRunner::post(F &&f)
{
    // The signals are sent from the thread of the runner, so that the screen
    // and the tests read them in their own thread.
    QMetaObject::invokeMethod(this, std::forward<F>(f), Qt::QueuedConnection);
}

QString WizardRunner::claim(const QString &serial)
{
    QMutexLocker locker(&registryMutex());
    if (registry().contains(serial))
        return QString(error::flowRunning);
    QDir().mkpath(m_store.folder());
    auto lock = std::make_unique<QLockFile>(m_store.lockPath(serial));
    // A lock is stale only when its process does not run any more, never by
    // its age: a backup can take an hour.
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0))
        return QString(error::flowRunning);
    registry().insert(serial);
    m_lock = std::move(lock);
    m_claimed = serial;
    return QString();
}

void WizardRunner::release()
{
    QMutexLocker locker(&registryMutex());
    if (m_claimed.isEmpty())
        return;
    registry().remove(m_claimed);
    m_claimed.clear();
    if (m_lock)
        m_lock->unlock();
    m_lock.reset();
}

QString WizardRunner::start(const Start &start)
{
    if (isRunning())
        return QString(error::flowRunning);
    if (start.serial.isEmpty())
        return QString(error::noSerial);
    const QStringList steps = stepsFor(start.shape);
    if (steps.isEmpty())
        return QString(error::badFlow);
    const QString claimed = claim(start.serial);
    if (!claimed.isEmpty())
        return claimed;

    m_progress = Progress();
    m_progress.appVersion = m_appVersion;
    m_progress.serial = start.serial;
    m_progress.model = start.model;
    m_progress.shape = start.shape;
    m_progress.steps = steps;
    m_progress.packageManifest = start.packageManifest;
    m_progress.packageSha256 = start.packageSha256;
    m_progress.started = QDateTime::currentDateTimeUtc();
    launch(false);
    return QString();
}

QString WizardRunner::resume(const QString &serial, const QString &model, Family family)
{
    if (isRunning())
        return QString(error::flowRunning);
    if (serial.isEmpty())
        return QString(error::noSerial);
    const QString claimed = claim(serial);
    if (!claimed.isEmpty())
        return claimed;

    const ProgressRead read = m_store.load(serial);
    switch (read.outcome) {
    case ProgressRead::Outcome::None:
        release();
        return QString(error::noProgress);
    case ProgressRead::Outcome::Refused:
        m_refusedReason = read.reason;
        release();
        return QString(error::progressRefused);
    case ProgressRead::Outcome::Ok:
        break;
    }
    if (read.progress.shape.family != family || read.progress.model != model) {
        release();
        return QString(error::progressMismatch);
    }
    m_progress = read.progress;
    m_progress.appVersion = m_appVersion;
    launch(true);
    return QString();
}

void WizardRunner::launch(bool resuming)
{
    m_cancel = false;
    m_abort = false;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
    m_thread = QThread::create([this, resuming] { work(resuming); });
    m_thread->start();
}

void WizardRunner::work(bool resuming)
{
    const int first = m_progress.current;
    const bool cut = resuming && m_progress.status != StepStatus::Next;

    // Section 3.3 step 3: a resume runs the diagnosis again, whatever step the
    // flow had reached. It changes nothing in the progress file.
    if (resuming && first > 0) {
        if (!runOne(0, false, false))
            return;
    }
    for (int i = first; i < m_progress.steps.size(); ++i) {
        if (!runOne(i, cut && i == first, true))
            return;
    }
    const QJsonObject summary = m_progress.facts.value(QString(step::finish)).toObject();
    m_store.remove(m_progress.serial);
    release();
    post([this, summary] { emit flowFinished(summary); });
}

bool WizardRunner::runOne(int index, bool resume, bool writeProgress)
{
    const QString name = m_progress.steps.at(index);
    std::unique_ptr<Step> step = m_factory ? m_factory(name) : nullptr;

    if (writeProgress) {
        m_progress.current = index;
        m_progress.status = StepStatus::Running;
        m_progress.error.clear();
        save();
    }
    post([this, index, name] { emit stepStarted(index, name); });

    StepOutcome outcome;
    if (!step) {
        outcome = StepOutcome::failed(QString(error::badFlow), QStringLiteral("no step ") + name);
    } else {
        Context context(this, name, resume, writeProgress);
        outcome = step->run(context);
    }

    // The runner is going away: a cut, so nothing more is written.
    if (m_abort)
        return false;

    if (outcome.kind == StepOutcome::Kind::Done) {
        if (writeProgress && index + 1 < m_progress.steps.size()) {
            m_progress.current = index + 1;
            m_progress.status = StepStatus::Next;
            save();
        }
        post([this, index, name] { emit stepDone(index, name); });
        return true;
    }

    const QString code =
        outcome.kind == StepOutcome::Kind::Cancelled ? QString(error::cancelled) : outcome.code;
    if (writeProgress) {
        m_progress.status = StepStatus::Failed;
        m_progress.error = code;
        save();
    }
    release();
    const QString detail = outcome.detail;
    post([this, index, name, code, detail] {
        emit stepFailed(index, name, code, detail);
        emit flowStopped(code);
    });
    return false;
}

void WizardRunner::save()
{
    m_progress.updated = QDateTime::currentDateTimeUtc();
    QString why;
    if (!m_store.save(m_progress, &why))
        qWarning("wizard: progress file not written: %s", qPrintable(why));
}

UserAnswer WizardRunner::waitForUser(const QString &name, WaitKind kind, const QJsonObject &info)
{
    QMutexLocker locker(&m_mutex);
    if (m_cancel || m_abort)
        return UserAnswer{true, false, QString()};
    m_answer.reset();
    m_waiting = true;
    post([this, name, kind, info] { emit stepWaiting(name, kind, info); });
    while (!m_answer && !m_cancel && !m_abort)
        m_answered.wait(&m_mutex);
    m_waiting = false;
    if (!m_answer)
        return UserAnswer{true, false, QString()};
    return *m_answer;
}

void WizardRunner::answer(const UserAnswer &answer)
{
    QMutexLocker locker(&m_mutex);
    if (!m_waiting)
        return;
    m_answer = answer;
    m_answered.wakeAll();
}

void WizardRunner::cancel()
{
    m_cancel = true;
    QMutexLocker locker(&m_mutex);
    m_answered.wakeAll();
}

bool WizardRunner::waitFinished(unsigned long ms)
{
    return !m_thread || m_thread->wait(ms);
}

} // namespace tacetos::wizard
