// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "flow.h"

#include <QDeadlineTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QThread>
#include <functional>
#include <memory>

// What a step of the wizard is, and what it is given while it runs
// (docs/specs/install-wizard.md section 2.4).

namespace tacetos::wizard {

struct Check;

// Section 2.4 rule 3: what a step can wait for.
enum class WaitKind { DiskMode, TypedName, ReadyForDfu, Battery, Offer };

// The user's answer to a wait. `cancelled` when the user cancelled the flow, or
// when the runner is going away.
struct UserAnswer
{
    bool cancelled = false;
    bool yes = true;
    QString text;
};

struct StepOutcome
{
    enum class Kind { Done, Failed, Cancelled };
    Kind kind = Kind::Done;
    QString code;
    QString detail;

    static StepOutcome done() { return {}; }
    static StepOutcome failed(const QString &code, const QString &detail = QString())
    {
        return {Kind::Failed, code, detail};
    }
    static StepOutcome cancelled() { return {Kind::Cancelled, QString(), QString()}; }
};

// Given to a step by the runner. Every call is made from the worker thread.
class StepContext
{
public:
    virtual ~StepContext() = default;

    virtual QString serial() const = 0;
    virtual QString model() const = 0;
    virtual FlowShape shape() const = 0;

    // Screens section 1.2: the step is about to make its first write to the iPod, or the helper
    // said its command is no longer `cancellable`. It may be called again: only the first call
    // of a step counts.
    virtual void firstWrite() {}

    // Section 3.3: true when the step was running, waiting or failed when the
    // flow stopped, and runs again now with its rule of resume.
    virtual bool isResume() const = 0;

    // True once the user cancelled or the runner is going away. A step looks
    // at it between two pieces of work and then returns `cancelled()`.
    virtual bool stopRequested() const = 0;

    // Progress inside the step: bytes or seconds, done and total (0 when not
    // known), and a short stage name.
    virtual void progress(const QString &stage, qint64 done, qint64 total) = 0;

    // Screens section 3: the checks of this run of the diagnosis, for the screen. Only the step
    // `diagnose` calls it, each time it runs the checks.
    virtual void reportChecks(const QList<Check> &) {}

    // Section 2.4 rule 3: blocks until the user answers.
    virtual UserAnswer waitForUser(WaitKind kind, const QJsonObject &info = QJsonObject()) = 0;

    // Section 3.2: the facts of the progress file, by step name.
    virtual QJsonValue fact(const QString &step, const QString &key) const = 0;
    virtual void setFact(const QString &key, const QJsonValue &value) = 0;

    // Section 3.2: the dated backup folder of the flow.
    virtual QString backupFolder() const = 0;
    virtual void setBackupFolder(const QString &path) = 0;

    // Section 3.2: the package of an install.
    virtual QString packageManifest() const = 0;
    virtual QString packageSha256() const = 0;

    // Update section 6 step 5: the package that the step `download` gives the
    // flow, and the version it installs, both kept in the progress file.
    virtual QString updateVersion() const { return QString(); }
    virtual void setPackage(const QString &manifest, const QString &sha256, const QString &version)
    {
        Q_UNUSED(manifest)
        Q_UNUSED(sha256)
        Q_UNUSED(version)
    }
};

class Step
{
public:
    virtual ~Step() = default;
    virtual StepOutcome run(StepContext &context) = 0;
};

// Makes the step of a name of section 2.3. Null for a name it does not know.
using StepFactory = std::function<std::unique_ptr<Step>(const QString &name)>;

// After a restart, detection sees the iPod before its volume is mounted (6 to 8 s on
// the real Classic). Asks `ready` every 100 ms until it is true, `ms` is over or the
// user stops; returns whether it became true.
inline bool waitUntil(StepContext &context, qint64 ms, const std::function<bool()> &ready)
{
    const QDeadlineTimer deadline(ms);
    while (!ready()) {
        if (deadline.hasExpired() || context.stopRequested())
            return false;
        QThread::msleep(100);
    }
    return true;
}

} // namespace tacetos::wizard

Q_DECLARE_METATYPE(tacetos::wizard::WaitKind)
