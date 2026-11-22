// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "diagnose.h"
#include "flow.h"
#include "step.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The page of the step `diagnose` (docs/specs/install-screens.md section 3): the checks of the
// runner in two groups, the command to type for what is missing, the action of a check, the
// question of the battery and Check again. It is wired to a runner that it does not own.
class DiagnosePage : public QWidget
{
    Q_OBJECT

public:
    // `serial`, `model` and `family` are what `WizardRunner::resume` needs to run the step
    // again.
    DiagnosePage(wizard::WizardRunner *runner, const QString &serial, const QString &model,
                 wizard::Family family, QWidget *parent = nullptr);

    // The last list of checks the runner sent.
    QList<wizard::Check> checks() const { return checks_; }

    // Wizard section 4.1 rule 4: false while a blocking check fails or waits, and before the
    // first list. The window keeps its main button off while it is false.
    bool canGoOn() const;

    // Section 3: the diagnosis ended with every blocking check passed.
    bool isReady() const { return ready_; }

    // What the action `install-udev-rule` does: returns an empty text, or the code of the
    // helper (`cancelled`, `permission-denied`, `unsupported-system`, `udev-failed`). It runs
    // in a thread of its own, since the password window can stay open for long.
    using UdevInstaller = std::function<QString()>;
    void setUdevInstaller(const UdevInstaller &installer) { installer_ = installer; }

    // What the action `mount-volume` does: mounts the music volume through the disk service,
    // and returns an empty text, or what went wrong. It runs in a thread of its own too: the
    // service may ask for a password.
    using VolumeMounter = std::function<QString()>;
    void setVolumeMounter(const VolumeMounter &mounter) { mounter_ = mounter; }

    // The pause of "Everything is ready" before `readyToGoOn`, 1 s by default.
    void setPause(int ms) { pause_.setInterval(ms); }

signals:
    void canGoOnChanged(bool on);
    // Every blocking check passed and the pause is over: the next step can show.
    void readyToGoOn();

private:
    void setChecks(const QList<wizard::Check> &checks);
    void rebuild();
    void addRow(QVBoxLayout *group, const wizard::Check &check);
    void addCommand(QVBoxLayout *group, const QString &id, const QString &command);
    void addUdevAction(QVBoxLayout *group, const wizard::Check &check);
    void onStarted(const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void setChecking(bool on, int percent = 0, const QString &stage = QString());
    void onWaiting(const QString &name, wizard::WaitKind kind);
    void onFailed(const QString &name, const QString &code);
    void onStopped();
    void onDone(const QString &name);
    void checkAgain();
    void resumeStep();
    void installRule();
    void mountVolume();
    void refresh();
    void copy(const QString &id, const QString &text);

    wizard::WizardRunner *runner_;
    QString serial_;
    QString model_;
    wizard::Family family_;
    UdevInstaller installer_;
    VolumeMounter mounter_;

    QList<wizard::Check> checks_;
    bool asking_ = false;
    bool failed_ = false;
    bool stopped_ = false;
    bool again_ = false;
    bool ready_ = false;
    bool installing_ = false;
    bool mounting_ = false;
    bool lastCanGoOn_ = false;
    QTimer pause_;

    QWidget *list_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *checking_ = nullptr;
    QLabel *note_ = nullptr;
    QPushButton *again_button_ = nullptr;
};

} // namespace tacetos
