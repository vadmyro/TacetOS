// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The drawing of a model (`:/models/<model>.svg`, else the generic one) at `height` pixels.
QPixmap modelPixmap(const QString &model, int height, qreal ratio = 1.0);

// The pages of the steps `firmware`, `disk-mode`, `bootloader` and `classic-bootloader`
// (docs/specs/install-screens.md section 7): the firmware progress and its offer, the Disk Mode
// request of an older iPod, the bootloader progress with the offers of the helper after a failure,
// and the restart of the Classic with the table of the beeps. It is wired to a runner that it does
// not own. The DFU guide of the Classic is another page (`DfuTutorialPage`).
class InstallPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstallPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // Section 13.4: the steps of Disk Mode for a model, in our words.
    static QStringList diskModeSteps(const QString &model);

    // Section 7.3: the tones and what they mean, as (tone, meaning) pairs.
    static QList<QPair<QString, QString>> beepTable();

    // A clock in milliseconds that never goes back, for the seconds left. The tests give theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }

    // The wait for the iPod after the restart (wizard section 9.2 step 4), 60 s by default; and
    // the wait of the volume after the Disk Mode of an older iPod (`RESTART_WINDOW` plus
    // `VOLUME_RETURN`).
    void setRestartWindowMs(qint64 ms) { restartMs_ = ms; }

    // The dated folder of the backup, for Open folder after `restore-failed`.
    void setBackupFolder(const QString &folder);

    // What Open folder does; the tests give theirs.
    void setFolderOpener(const std::function<void(const QString &)> &opener) { opener_ = opener; }

    // The `next` of the helper (bootloader-legacy.md section 19), read by the window from the
    // facts of the step. The code `bootloader-restored` says `restored` by itself.
    void setHelperNext(const QString &next) { next_ = next; }

    // The version of the package, shown under the title of the firmware (section 7.1).
    void setVersion(const QString &version);

    // The state read before the bootloader was "installed": the title says "Updating".
    void setUpdate(bool update) { update_ = update; }

    // How many times the Classic bootloader failed to take in this window (screens section 7.3).
    void setTakeFailures(int count) { takeFailures_ = count; }

    // The development build never writes: `dev-build` is then a plain stop, not a failure.
    bool isActive() const { return active_; }
    bool isFailed() const { return failed_; }
    bool isDone() const { return done_; }
    bool isStopped() const { return stopped_; }
    // A wait of Disk Mode or an offer shows its buttons.
    bool isAsking() const { return asking_; }
    bool isWriting() const { return writing_; }
    bool isRestarting() const { return restarting_; }
    // Section 7.2: the Report button of the window is the main one (`restore-failed`).
    bool reportIsMain() const { return reportMain_; }
    // The page stops with a plain note (`dev-build`), not with the colour of a failure.
    bool isPlainStop() const { return plain_; }

    QString titleText() const;
    QString messageText() const;
    QString countdownText() const;
    QString versionText() const;
    QString stageText() const;
    QStringList diskModeLines() const;

    QPushButton *diskModeButton() const { return diskMode_; }
    QPushButton *yesButton() const { return yes_; }
    QPushButton *noButton() const { return no_; }
    QPushButton *folderButton() const { return folder_; }
    QPushButton *closeButton() const { return close_; }

    // What the window says when it is closed during a write, empty when closing is allowed.
    QString closeRefusal() const;

    // Redraws the seconds left of the restart. A timer calls it each second; the tests too.
    void refreshCountdown();

signals:
    // "Try again" or "Try once more": the window resumes the flow.
    void retryRequested();
    // The page has nothing more to offer (`restored`): the window closes.
    void closeRequested();

private:
    enum class Offer { None, Settings, RestoreBackup, Retry };

    bool mine(const QString &name) const;
    void onStarted(const QString &name);
    void onWaiting(const QString &name, int kind, const QJsonObject &info);
    void onFirstWrite(const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code);
    void showOnly(QWidget *block);
    void showFailure(const QString &text, bool plain);
    void showRestart();
    void answer(bool yes);
    void announce();
    qint64 now() const;

    wizard::WizardRunner *runner_;
    std::function<qint64()> clock_;
    std::function<void(const QString &)> opener_;
    QTimer tick_;
    qint64 restartMs_;

    QString step_;
    QString model_;
    QString folderPath_;
    QString next_;
    int takeFailures_ = 0;
    Offer offer_ = Offer::None;
    qint64 restartSince_ = -1;
    bool active_ = false;
    bool asking_ = false;
    bool writing_ = false;
    bool failed_ = false;
    bool stopped_ = false;
    bool done_ = false;
    bool restarting_ = false;
    bool reportMain_ = false;
    bool plain_ = false;
    bool update_ = false;

    QLabel *title_ = nullptr;
    QLabel *version_ = nullptr;
    QLabel *message_ = nullptr;
    QWidget *diskBlock_ = nullptr;
    QLabel *diskSteps_ = nullptr;
    QLabel *diskDrawing_ = nullptr;
    QPushButton *diskMode_ = nullptr;
    QWidget *runBlock_ = nullptr;
    QLabel *warning_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *barText_ = nullptr;
    QLabel *stageLine_ = nullptr;
    QWidget *restartBlock_ = nullptr;
    QLabel *listen_ = nullptr;
    QLabel *countdown_ = nullptr;
    QLabel *beeps_ = nullptr;
    QWidget *offerBlock_ = nullptr;
    QPushButton *yes_ = nullptr;
    QPushButton *no_ = nullptr;
    QWidget *endBlock_ = nullptr;
    QPushButton *folder_ = nullptr;
    QPushButton *close_ = nullptr;
};

} // namespace tacetos
