// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "flow.h"
#include "progress.h"
#include "report.h"
#include "step.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

class QCloseEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QStackedWidget;

namespace tacetos {
class BackupPage;
class DfuTutorialPage;
class DiagnosePage;
class DonePage;
class FormatPage;
class InstallPage;
class ReportPage;
class UninstallPage;
} // namespace tacetos

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The iPod a window is about: what the first screen needs to know of it.
struct WizardTarget
{
    QString serial;
    QString model;
    // The name of the card.
    QString name;
    // The `bootloader_target` of the model, which a package must be for.
    QString bootloaderTarget;
    wizard::Family family = wizard::Family::Partition;
    bool macFormat = false;
    // Used space of the music volume, size of what Keep my music copies, and the firmware
    // partition of an older iPod (0 for a Classic), for the space lines.
    qint64 volumeUsed = 0;
    qint64 keepBytes = 0;
    qint64 partitionBytes = 0;
};

struct WizardOptions
{
    // The data folder of the app: the progress files are in its `wizard/` folder.
    QString dataDir;
    // The folder of the backups. Empty: the default one.
    QString backupRoot;
    // False in a development build, and while the helper is not found (screens section 11).
    bool realWrites = false;
    // The free space of a folder, -1 when not known. Empty: asks the system.
    std::function<qint64(const QString &)> freeBytes;
    // What the action `install-udev-rule` of the checks does (DiagnosePage::UdevInstaller).
    // Empty: the checks show the commands to type only.
    std::function<QString()> udevInstaller;
    // What the action `mount-volume` of the checks does (DiagnosePage::VolumeMounter). Empty: no
    // button, the message only.
    std::function<QString()> volumeMounter;
    // What the report holds beside the facts of the window: the versions, the ring of the
    // journal, the serials and the names to take out. Empty: a report of the window's facts only.
    std::function<journal::ReportInput()> reportInput;
    // What Eject does on the last page (home section 6); true when the iPod was ejected. Empty:
    // the pages do not eject.
    std::function<bool()> ejecter;
};

// What opened the window (screens section 1.1).
enum class WizardEntry { Install, Uninstall, Resume };

// The window of the install wizard for one iPod (docs/specs/install-screens.md sections 1, 2,
// 11 and 12): the steps on the left, the page of the step on the right, the footer. It is
// wired to a runner that it does not own.
class WizardWindow : public QWidget
{
    Q_OBJECT

public:
    WizardWindow(const WizardTarget &target, const WizardOptions &options,
                 wizard::WizardRunner *runner, WizardEntry entry, QWidget *parent = nullptr);

    const WizardTarget &target() const { return target_; }
    wizard::WizardRunner *runner() const { return runner_; }

    // The page that shows: `install`, `uninstall`, `resume`, `step`, `offer`, or the page of a
    // step: `diagnose`, `backup`, `format`, `dfu`, `write` (firmware, bootloader, Disk Mode and the
    // restart of the Classic), `remove` (the steps of an uninstall), `done`, and `report` over
    // them.
    QString pageName() const;

    // The pages of the steps, for the tests.
    DiagnosePage *diagnosePage() const { return diagnose_; }
    BackupPage *backupPage() const { return backup_; }
    FormatPage *formatPage() const { return format_; }
    DfuTutorialPage *dfuPage() const { return dfu_; }
    InstallPage *installPage() const { return install_; }
    UninstallPage *uninstallPage() const { return uninstall_page_; }
    DonePage *donePage() const { return done_; }
    ReportPage *reportPage() const { return report_page_; }
    // The report page shows over the page of the step.
    bool isReportOpen() const { return reportOpen_; }
    // The Report button of the footer, and its Back.
    void openReport();
    void closeReport();

    // Section 1.3: a step is past its first write. The window and the app cannot be closed.
    bool isWriting() const { return writing_; }
    // The flow is started and not ended.
    bool isBusy() const { return active_; }
    // Section 1.3: why the app cannot quit now, empty when it can.
    QString quitRefusal() const;

    // Section 2.1.
    void setPath(wizard::InstallPath path);
    wizard::InstallPath path() const { return path_; }
    void setPackage(const QString &manifestPath);
    void setBackupRoot(const QString &folder);
    void setRealWrites(bool on);

    // The labels of the list of steps, and the state of each item ("done", "running",
    // "waiting", "failed", "todo").
    QStringList stepLabels() const;
    QString stepState(int item) const;

    // What the window asks the user, so a test can answer: yes or no.
    using Asker = std::function<bool(const QString &title, const QString &text)>;
    void setAsker(const Asker &asker) { ask_ = asker; }
    // What the window says when it refuses to close.
    using Notifier = std::function<void(const QString &text)>;
    void setNotifier(const Notifier &notifier) { notify_ = notifier; }

signals:
    void flowEnded();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Main { None, Start, Close, Offer };

    void build();
    QWidget *buildFirstPage();
    QWidget *buildResumePage();
    QWidget *buildStepPage();
    QWidget *buildOfferPage();
    void connectRunner();
    void addPage(const QString &name, QWidget *page);
    void showPage(const QString &name);
    bool isFlowPage(const QString &name) const;
    void showStepPage(const QString &name);
    void onPageReady();
    QString currentFlowPage() const;
    void retry();
    QString backupFolder() const;
    void refreshFirst();
    void refreshFooter();
    void setItems(const QStringList &steps, int done);
    void setItemState(int item, const QString &state);
    void onStarted(int index, const QString &name);
    void onProgress(qint64 done, qint64 total);
    void onWaiting(const QString &name, wizard::WaitKind kind, const QJsonObject &info);
    void onDone(int index);
    void onFailed(int index, const QString &name, const QString &code, const QString &detail);
    void onStopped();
    void onMain();
    void onCancel();
    void start();
    void resume();
    void startAgain();
    void choosePackage();
    void changeFolder();
    bool confirm(const QString &title, const QString &text);
    QString message(const QString &code) const;
    qint64 free() const;

    WizardTarget target_;
    WizardOptions options_;
    wizard::WizardRunner *runner_;
    WizardEntry entry_;
    Asker ask_;
    Notifier notify_;

    wizard::InstallPath path_ = wizard::InstallPath::Clean;
    wizard::Progress saved_;
    bool hasSaved_ = false;
    QString packageManifest_;
    QString packageSha_;
    bool packageOk_ = false;
    wizard::FlowShape shape_;
    QStringList steps_;
    QStringList labels_;
    // The item of each step of the runner.
    QList<int> itemOfStep_;
    QStringList states_;
    Main main_ = Main::None;
    bool active_ = false;
    bool writing_ = false;
    bool waiting_ = false;
    bool stopped_ = false;
    // The Classic restarts: the page of the step is the one of the restart, not the DFU guide.
    bool restartSeen_ = false;
    // The helper reached the transfer of this try: only then is its `resume` a restart.
    bool dfuSent_ = false;
    QString currentStep_;
    bool reportOpen_ = false;
    QString beforeReport_;
    QList<journal::ReportFailure> failures_;

    QWidget *banner_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QListWidget *list_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QHash<QString, QWidget *> pageOf_;
    // Screens section 3 and 4.1: a page that ended well shows its end line for a pause; the page
    // of the next step waits for it.
    bool holding_ = false;
    QString pending_;
    DiagnosePage *diagnose_ = nullptr;
    BackupPage *backup_ = nullptr;
    FormatPage *format_ = nullptr;
    DfuTutorialPage *dfu_ = nullptr;
    InstallPage *install_ = nullptr;
    UninstallPage *uninstall_page_ = nullptr;
    DonePage *done_ = nullptr;
    ReportPage *report_page_ = nullptr;
    QPushButton *main_button_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *report_ = nullptr;

    QRadioButton *clean_ = nullptr;
    QRadioButton *keep_ = nullptr;
    QLabel *cleanInfo_ = nullptr;
    QLabel *keepInfo_ = nullptr;
    QLabel *packageLabel_ = nullptr;
    QLabel *packageWhy_ = nullptr;
    QLabel *folderLabel_ = nullptr;
    QLabel *plan_ = nullptr;
    QLabel *firstError_ = nullptr;

    QLabel *resumeTitle_ = nullptr;
    QLabel *resumeDates_ = nullptr;
    QLabel *stepTitle_ = nullptr;
    QLabel *stepText_ = nullptr;
    QLabel *failure_ = nullptr;
    QPushButton *failureReport_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *barText_ = nullptr;
    QLabel *offerTitle_ = nullptr;
    QLabel *offerText_ = nullptr;
};

} // namespace tacetos
