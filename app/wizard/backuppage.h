// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "timeleft.h"

#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The size as the pages of the backup write it: decimal units with one digit ("71.0 GB",
// "2.1 MB", "12.0 kB").
QString backupSizeText(qint64 bytes);

// The time left of docs/specs/install-screens.md section 4.1: "about 22 min left", "about 1 h
// 05 min left", "less than a minute left".
QString backupTimeLeftText(qint64 seconds);

// The page of `backup-full`, `backup-keep` and `backup-settings` (screens section 4.1 and 4.2):
// the bar in bytes with the time left, the file being copied, the folder of the backup, the end
// line, and the failed and stopped states. It is wired to a runner that it does not own. The
// choice of the folder is on the first screen of the window (section 2.1).
class BackupPage : public QWidget
{
    Q_OBJECT

public:
    explicit BackupPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // A clock in milliseconds that never goes back, for the time left. The tests give theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }

    // What "Open folder" does with the folder of the backup.
    void setOpener(const std::function<void(const QString &)> &opener) { opener_ = opener; }

    // The pause of the end line before `readyToGoOn`, 1 s by default.
    void setPause(int ms) { pause_.setInterval(ms); }

    // The dated folder of the backup, shown under the bar.
    void setBackupFolder(const QString &folder);

    // The file being copied, with its path on the volume. It is only shown: nothing here writes
    // it in the journal (wizard section 5.3 rule 3).
    void setCurrentFile(const QString &path);

    // The names of `backup-read-failed` and `backup-name-refused`, for the failed state.
    void setFileNames(const QStringList &names);

    // The count of the end line "Backup checked: N files, X GB".
    void setFileCount(int files) { files_ = files; }

    // True while a backup step runs or waits: the Cancel of the window is on.
    bool isActive() const { return active_; }
    bool isFailed() const { return failed_; }
    bool isStopped() const { return stopped_; }

    // The line of the end of the copy, empty before.
    QString checkedLine() const { return checkedLine_; }

signals:
    // The end line has shown for its pause: the flow goes on.
    void readyToGoOn();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    bool mine(const QString &name) const;
    void onStarted(const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code);
    void showFailure(const QString &code);
    void showNames();
    void elideFile();
    qint64 now() const;

    wizard::WizardRunner *runner_;
    std::function<qint64()> clock_;
    std::function<void(const QString &)> opener_;
    QTimer pause_;
    wizard::TimeLeft timeLeft_;

    QString step_;
    QString folder_;
    QString file_;
    QStringList names_;
    QString checkedLine_;
    qint64 total_ = 0;
    qint64 started_ = -1;
    int files_ = -1;
    bool active_ = false;
    bool failed_ = false;
    bool stopped_ = false;

    QLabel *title_ = nullptr;
    QLabel *what_ = nullptr;
    QLabel *banner_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *barText_ = nullptr;
    QLabel *fileLine_ = nullptr;
    QLabel *note_ = nullptr;
    QLabel *folderLine_ = nullptr;
    QPushButton *open_ = nullptr;
    QLabel *namesTitle_ = nullptr;
    QPlainTextEdit *namesList_ = nullptr;
    QLabel *last_ = nullptr;
};

} // namespace tacetos
