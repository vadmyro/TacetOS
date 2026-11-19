// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "backup.h"

#include <QList>
#include <QString>
#include <QWidget>
#include <atomic>
#include <functional>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QVBoxLayout;

namespace tacetos {

// One line of the list of backups of an iPod.
struct BackupEntry
{
    QString folder;
    // The folder name, `AAAA-MM-JJ_HHMMSS`, which sorts as the date does.
    QString name;
    wizard::BackupManifest manifest;
    // False when the manifest is missing, refused or marked incomplete: the line is greyed.
    bool usable = false;
};

// The page of the restore (docs/specs/install-screens.md section 4.3, wizard section 5.7): the
// backups of one serial in the backup folder, newest first, with its own progress. It is not a
// flow of the runner. The file names it lists are shown, never written to the journal.
class RestorePage : public QWidget
{
    Q_OBJECT

public:
    // `backupRoot` is the folder the user chose (without serial and date).
    RestorePage(const QString &backupRoot, const QString &serial, QWidget *parent = nullptr);
    ~RestorePage() override;

    // The name of the iPod, for the confirmations.
    void setIpodName(const QString &name) { ipodName_ = name; }

    // The mount point of the volume of this iPod, empty when it is not plugged in or not
    // mounted: the page then says "Plug in the iPod of this backup" and Restore stays off.
    void setVolume(const QString &mountRoot);

    // Null for a Classic (no firmware partition to write back).
    void setFirmwarePartition(wizard::FirmwarePartition *partition);

    // The questions of the page: true is a yes. The tests give theirs, the default is a dialog.
    using Asker = std::function<bool(const QString &title, const QString &text)>;
    void setAsker(const Asker &asker) { asker_ = asker; }

    // Reads the backup folder again.
    void reload();

    QList<BackupEntry> entries() const { return entries_; }

    // The backup chosen, -1 when none.
    int chosen() const;

    // The faults to inject in a restore, for the tests.
    void setFault(const installer::FileFault &fault) { fault_ = fault; }

    bool isRunning() const { return running_; }
    bool isDone() const { return done_; }

    // Cancel is on between two files of a restore that runs.
    void cancel() { stop_ = true; }

signals:
    // The restore ended: the code of wizard section 13, empty on success.
    void restoreFinished(const QString &code);

private:
    void rebuild();
    void refresh();
    void onRestore();
    void onResult(const QString &code, int restored, int skipped);
    void onProgress(qint64 done, qint64 total);
    QString rowText(const BackupEntry &entry) const;

    QString root_;
    QString serial_;
    QString ipodName_;
    QString volume_;
    wizard::FirmwarePartition *firmware_ = nullptr;
    Asker asker_;
    installer::FileFault fault_;

    QList<BackupEntry> entries_;
    bool running_ = false;
    bool done_ = false;
    std::atomic<bool> stop_{false};
    QThread *thread_ = nullptr;

    QLabel *title_ = nullptr;
    QLabel *banner_ = nullptr;
    QWidget *list_ = nullptr;
    QButtonGroup *group_ = nullptr;
    QCheckBox *firmwareBox_ = nullptr;
    QLabel *plugNote_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *barText_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *restore_ = nullptr;
};

} // namespace tacetos
