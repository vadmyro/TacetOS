// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The page of the steps `format` and `convert` (docs/specs/install-screens.md section 5): the
// strong confirmation with the name of the iPod to type, then the progress of the erase. It is
// wired to a runner that it does not own. The Cancel of the window stays the one of the footer:
// the page only knows when the first write is made, and then nothing on it can be clicked.
class FormatPage : public QWidget
{
    Q_OBJECT

public:
    explicit FormatPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // A clock in milliseconds that never goes back, for the seconds left. The tests give theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }

    // How long the volume has to come back (wizard section 6.3), 30 s by default.
    void setVolumeReturnMs(qint64 ms) { volumeReturnMs_ = ms; }

    // The dated folder of the backup checked a moment ago, shown under the count of files.
    void setBackupFolder(const QString &folder);

    // The steps `format` and `convert` of this page run now, or wait.
    bool isActive() const { return active_; }
    // The confirmation is shown and waits for the name.
    bool isAsking() const { return asking_; }
    // The first write is made: nothing can be clicked and the window cannot be closed.
    bool isWriting() const { return writing_; }
    bool isFailed() const { return failed_; }
    bool isStopped() const { return stopped_; }
    bool isDone() const { return done_; }

    // The text of the main button: "Erase the iPod".
    QPushButton *confirmButton() const { return confirm_; }

    // What the window says when it is closed during a write, empty when closing is allowed.
    QString closeRefusal() const;

    // Redraws the seconds left of "Waiting for the iPod to come back". A timer calls it each
    // second; the tests call it with their clock.
    void refreshCountdown();

protected:
    // A page that is a window of its own refuses to close during a write.
    void closeEvent(QCloseEvent *event) override;

private:
    bool mine(const QString &name) const;
    void onStarted(const QString &name);
    void onWaiting(const QString &name, int kind, const QJsonObject &info);
    void onFirstWrite(const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code);
    void onTyped();
    void showConfirmation(bool on);
    void lock();
    qint64 now() const;

    wizard::WizardRunner *runner_;
    std::function<qint64()> clock_;
    QTimer tick_;

    QString step_;
    QString expected_;
    QString folder_;
    qint64 volumeReturnMs_;
    qint64 volumeSince_ = -1;
    bool active_ = false;
    bool asking_ = false;
    bool writing_ = false;
    bool failed_ = false;
    bool stopped_ = false;
    bool done_ = false;
    bool classic_ = false;

    QLabel *banner_ = nullptr;
    QLabel *title_ = nullptr;
    QWidget *confirmation_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *folderLine_ = nullptr;
    QLabel *convertNote_ = nullptr;
    QLabel *prompt_ = nullptr;
    QLabel *expectedLabel_ = nullptr;
    QLineEdit *field_ = nullptr;
    QPushButton *confirm_ = nullptr;
    QWidget *progress_ = nullptr;
    QLabel *warning_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *barText_ = nullptr;
    QLabel *stageLine_ = nullptr;
    QLabel *note_ = nullptr;
};

} // namespace tacetos
