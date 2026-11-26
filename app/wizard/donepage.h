// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The page of the steps `verify` and `finish` (docs/specs/install-screens.md section 8). It is
// wired to a runner that it does not own. The window gives it what the facts of the flow cannot
// say: the model, the family, the backup summary and the text of an older model.
class DonePage : public QWidget
{
    Q_OBJECT

public:
    explicit DonePage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // The model file id (for the drawing) and its name ("iPod Classic 120 GB").
    void setIpod(const QString &model, const QString &name);

    // The Classic shows the three lines of bootloader-classic.md section 13.3; an older iPod shows
    // the lines of its model file, and nothing when there are none (wizard section 16).
    void setClassic(bool classic) { classic_ = classic; }
    void setStartLines(const QStringList &lines) { lines_ = lines; }

    // The summary of the backup: "Backup checked: 1 234 files, 71.0 GB",
    // or "The iPod was empty: nothing to back up" when there were 0 files.
    void setBackup(const QString &folder, qint64 files, qint64 bytes);

    // A clean install: "Put my music back" is shown.
    void setCleanInstall(bool clean) { clean_ = clean; }
    // An update (update-channel.md section 7): "TacetOS 0.3.0 is on the iPod", and the iPod starts
    // on the new version once ejected.
    void setUpdate(bool update) { update_ = update; }

    // The version of the package, when the facts of `finish` have none.
    void setVersion(const QString &version) { version_ = version; }

    // What Eject does (home section 6) and Open folder does. It returns true when the iPod was
    // ejected. The tests give theirs.
    void setEjecter(const std::function<bool()> &ejecter) { ejecter_ = ejecter; }
    void setFolderOpener(const std::function<void(const QString &)> &opener) { opener_ = opener; }

    bool isChecking() const { return checking_; }
    bool isFailed() const { return failed_; }
    bool isFinished() const { return finished_; }
    bool isEjected() const { return ejected_; }

    QString titleText() const;
    QString subtitleText() const;
    QString failureText() const;
    QString startingText() const;
    QString backupText() const;
    QString ejectNote() const;

    QPushButton *ejectButton() const { return eject_; }
    QPushButton *musicButton() const { return music_; }
    QPushButton *folderButton() const { return folder_; }
    QPushButton *closeButton() const { return close_; }

signals:
    // "Put my music back": the window opens the restore page with this backup chosen.
    void putMusicBackRequested(const QString &backupFolder);
    void closeRequested();

private:
    void onStarted(const QString &name);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code, const QString &detail);
    void showFinish(const QJsonObject &summary);
    void eject();

    wizard::WizardRunner *runner_;
    std::function<bool()> ejecter_;
    std::function<void(const QString &)> opener_;
    QString model_;
    QString name_;
    QString folderPath_;
    QString version_;
    QStringList lines_;
    qint64 files_ = 0;
    qint64 bytes_ = 0;
    bool classic_ = false;
    bool clean_ = false;
    bool update_ = false;
    bool checking_ = false;
    bool failed_ = false;
    bool finished_ = false;
    bool ejected_ = false;

    QLabel *title_ = nullptr;
    QLabel *drawing_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *failure_ = nullptr;
    QWidget *finish_ = nullptr;
    QLabel *starting_ = nullptr;
    QLabel *backup_ = nullptr;
    QPushButton *folder_ = nullptr;
    QPushButton *music_ = nullptr;
    QLabel *ejectLine_ = nullptr;
    QPushButton *eject_ = nullptr;
    QPushButton *close_ = nullptr;
};

} // namespace tacetos
