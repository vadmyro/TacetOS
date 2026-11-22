// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The screens of the uninstall (docs/specs/install-screens.md section 9): the first screen, the
// steps that only an uninstall has (`remove-bootloader`, `classic-remove` after its DFU guide,
// `remove-firmware`), `verify` and `finish` of an uninstall, and the end "Apple software only".
// The other steps of the flow use the pages of the install: the backup of the settings, Disk Mode
// and the DFU guide. It is wired to a runner that it does not own.
class UninstallPage : public QWidget
{
    Q_OBJECT

public:
    // What the first screen says (wizard section 10).
    enum class First {
        // "Back to Apple's software only" and the button "Uninstall".
        Confirm,
        // An older iPod with nothing installed: no button but Close.
        NothingToDo,
        // A Classic with no firmware folder and no install record: "Remove it anyway?".
        RemoveAnyway
    };

    explicit UninstallPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // The model file id (for the drawing) and its name ("iPod Classic 120 GB").
    void setIpod(const QString &model, const QString &name);

    // The first screen; it is the page until the flow starts.
    void showFirst(First kind);

    // The folder of the backups, shown on the first screen (with `~` for the home folder) and
    // under "Your settings were saved in" at the end when the flow does not say its own.
    void setBackupFolder(const QString &folder);

    // A clock in milliseconds that never goes back, for the seconds left. The tests give theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }
    // The wait for the iPod after the restart of the Classic, 60 s by default.
    void setRestartWindowMs(qint64 ms) { restartMs_ = ms; }

    // The `next` of the helper (bootloader-legacy.md section 19), read by the window from the
    // facts of the step.
    void setHelperNext(const QString &next) { next_ = next; }

    // The flow is an uninstall, so `verify` and `finish` are the page's. Set by the window, and
    // by the first of the steps that only an uninstall has.
    void setUninstall(bool on) { uninstall_ = on; }

    // What Eject does (home section 6) and Open folder does. It returns true when the iPod was
    // ejected. The tests give theirs.
    void setEjecter(const std::function<bool()> &ejecter) { ejecter_ = ejecter; }
    void setFolderOpener(const std::function<void(const QString &)> &opener) { opener_ = opener; }

    First firstKind() const { return first_; }
    bool isStarted() const { return started_; }
    bool isActive() const { return active_; }
    bool isFailed() const { return failed_; }
    bool isDone() const { return done_; }
    bool isStopped() const { return stopped_; }
    bool isWriting() const { return writing_; }
    bool isRestarting() const { return restarting_; }
    // A wait of the flow, or "Try again", shows its buttons.
    bool isAsking() const { return asking_; }
    bool isChecking() const { return checking_; }
    bool isFinished() const { return finished_; }
    bool isEjected() const { return ejected_; }
    // The page stops with a plain note (`dev-build`, `nothing-to-do`), not with a failure.
    bool isPlainStop() const { return plain_; }
    // How many times `still-tacetos` came back in this window.
    int stillFailures() const { return stillFailures_; }

    QString titleText() const;
    QString messageText() const;
    QString subtitleText() const;
    QString folderText() const;
    QString countdownText() const;
    QString stageText() const;
    QString backupText() const;
    QString ejectNote() const;

    QPushButton *startButton() const { return start_; }
    QPushButton *changeFolderButton() const { return change_; }
    QPushButton *yesButton() const { return yes_; }
    QPushButton *noButton() const { return no_; }
    QPushButton *folderButton() const { return folder_; }
    QPushButton *ejectButton() const { return eject_; }
    QPushButton *closeButton() const { return close_; }

    // What the window says when it is closed during a write, empty when closing is allowed.
    QString closeRefusal() const;

    // Redraws the seconds left of the restart. A timer calls it each second; the tests too.
    void refreshCountdown();

signals:
    // "Uninstall" or "Remove anyway" on the first screen: the window starts the flow.
    void startRequested();
    // "Change..." of the backup folder: the window opens its folder dialog.
    void changeFolderRequested();
    // "Try again": the window resumes the flow.
    void retryRequested();
    void closeRequested();

private:
    enum class Offer { None, RemoveAnyway, Retry };

    bool mine(const QString &name) const;
    void onStarted(const QString &name);
    void onWaiting(const QString &name, int kind, const QJsonObject &info);
    void onFirstWrite(const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code, const QString &detail);
    void showFinish(const QJsonObject &summary);
    void showOnly(QWidget *block);
    void showFailure(const QString &text, bool plain);
    void showRestart();
    void offerRetry();
    void answer(bool yes);
    void eject();
    void announce(QWidget *what);
    qint64 now() const;

    wizard::WizardRunner *runner_;
    std::function<qint64()> clock_;
    std::function<bool()> ejecter_;
    std::function<void(const QString &)> opener_;
    QTimer tick_;
    qint64 restartMs_;

    QString step_;
    QString model_;
    QString name_;
    QString next_;
    QString folderPath_;
    First first_ = First::Confirm;
    Offer offer_ = Offer::None;
    qint64 restartSince_ = -1;
    int stillFailures_ = 0;
    bool uninstall_ = false;
    bool started_ = false;
    bool active_ = false;
    bool asking_ = false;
    bool writing_ = false;
    bool failed_ = false;
    bool stopped_ = false;
    bool done_ = false;
    bool restarting_ = false;
    // The helper reached the transfer of this try (its `resume` ends every try).
    bool sent_ = false;
    bool checking_ = false;
    bool finished_ = false;
    bool ejected_ = false;
    bool plain_ = false;
    // "Remove anyway" was said on the first screen: the offer of the step is answered yes.
    bool removeAnyway_ = false;

    QLabel *title_ = nullptr;
    QLabel *drawing_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QLabel *message_ = nullptr;
    QWidget *firstBlock_ = nullptr;
    QLabel *firstText_ = nullptr;
    QLabel *folderLine_ = nullptr;
    QPushButton *change_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *firstClose_ = nullptr;
    QWidget *runBlock_ = nullptr;
    QLabel *warning_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *stageLine_ = nullptr;
    QWidget *restartBlock_ = nullptr;
    QLabel *listen_ = nullptr;
    QLabel *countdown_ = nullptr;
    QLabel *beeps_ = nullptr;
    QWidget *offerBlock_ = nullptr;
    QPushButton *yes_ = nullptr;
    QPushButton *no_ = nullptr;
    QWidget *stopBlock_ = nullptr;
    QPushButton *stopClose_ = nullptr;
    QWidget *finishBlock_ = nullptr;
    QLabel *backup_ = nullptr;
    QPushButton *folder_ = nullptr;
    QLabel *ejectLine_ = nullptr;
    QPushButton *eject_ = nullptr;
    QPushButton *close_ = nullptr;
};

} // namespace tacetos
