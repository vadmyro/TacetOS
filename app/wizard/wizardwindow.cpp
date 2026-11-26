// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/wizardwindow.h"

#include "backup.h"
#include "diagnose.h"
#include "home/homeaction.h"
#include "package.h"
#include "runner.h"
#include "wholeprogress.h"
#include "wizard/backuppage.h"
#include "wizard/bigprogresspage.h"
#include "wizard/dfututorial.h"
#include "wizard/diagnosepage.h"
#include "wizard/donepage.h"
#include "wizard/formatpage.h"
#include "wizard/installpage.h"
#include "wizard/reportpage.h"
#include "wizard/uninstallpage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QVBoxLayout>

namespace tacetos {

using namespace wizard;

namespace {

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
}

// Section 13.1: the label of a step.
QString labelOf(const QString &name)
{
    if (name == step::download)
        return wtr("Download");
    if (name == step::diagnose)
        return wtr("Checks");
    if (name == step::backupFull || name == step::backupKeep || name == step::backupSettings)
        return wtr("Backup");
    if (name == step::diskMode)
        return wtr("Disk Mode");
    if (name == step::format)
        return wtr("Erase");
    if (name == step::convert)
        return wtr("Convert");
    if (name == step::firmware)
        return wtr("Firmware");
    if (name == step::bootloader || name == step::classicBootloader)
        return wtr("Bootloader");
    if (name == step::removeBootloader || name == step::classicRemove)
        return wtr("Remove bootloader");
    if (name == step::removeFirmware)
        return wtr("Remove files");
    if (name == step::verify)
        return wtr("Check");
    if (name == step::finish)
        return wtr("Done");
    return name;
}

QString gigabytes(qint64 bytes)
{
    return wtr("%1 GB").arg(QString::number(double(bytes) / 1e9, 'f', 1));
}

QString homeShort(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

QString duration(qint64 seconds)
{
    const qint64 minutes = qMax<qint64>(1, (seconds + 30) / 60);
    if (minutes < 60)
        return wtr("About %1 min").arg(minutes);
    return wtr("About %1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

QPushButton *button(const QString &text, const char *role, const char *name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(name));
    b->setProperty("role", QString::fromLatin1(role));
    b->setAccessibleName(text);
    return b;
}

QLabel *label(const char *name, QWidget *parent, bool wrap = true)
{
    auto *l = new QLabel(parent);
    l->setObjectName(QString::fromLatin1(name));
    l->setWordWrap(wrap);
    return l;
}

const QStringList kSigns = {QStringLiteral("done"), QStringLiteral("running"),
                            QStringLiteral("waiting"), QStringLiteral("failed"),
                            QStringLiteral("todo")};

QString signOf(const QString &state)
{
    if (state == QLatin1String("done"))
        return QString(QChar(0x2713));
    if (state == QLatin1String("failed"))
        return QString(QChar(0x2715));
    if (state == QLatin1String("todo"))
        return QString(QChar(0x25CB));
    return QString(QChar(0x25CF));
}

} // namespace

WizardWindow::WizardWindow(const WizardTarget &target, const WizardOptions &options,
                           WizardRunner *runner, WizardEntry entry, QWidget *parent)
    : QWidget(parent, Qt::Window), target_(target), options_(options), runner_(runner),
      entry_(entry)
{
    if (options_.backupRoot.isEmpty())
        options_.backupRoot = defaultBackupRoot();
    ask_ = [this](const QString &title, const QString &text) {
        return QMessageBox::question(this, title, text) == QMessageBox::Yes;
    };
    notify_ = [this](const QString &text) { QMessageBox::information(this, QString(), text); };

    shape_.family = target_.family;
    shape_.kind = entry_ == WizardEntry::Uninstall ? FlowKind::Uninstall
                  : entry_ == WizardEntry::Update  ? FlowKind::Update
                                                   : FlowKind::Install;
    shape_.macFormat = target_.macFormat;
    shape_.bootloaderFirst = target_.bootloaderFirst;
    shape_.path = shape_.kind == FlowKind::Install ? path_ : InstallPath::None;

    build();
    connectRunner();

    if (entry_ == WizardEntry::Resume) {
        const ProgressRead read = ProgressStore(options_.dataDir).load(target_.serial);
        if (read.outcome == ProgressRead::Outcome::Ok) {
            saved_ = read.progress;
            hasSaved_ = true;
            shape_ = saved_.shape;
            setItems(saved_.steps, saved_.current);
            const QString step = saved_.steps.value(saved_.current);
            resumeTitle_->setText((saved_.shape.kind == FlowKind::Uninstall
                                       ? wtr("An uninstall was stopped at \"%1\"")
                                   : saved_.shape.kind == FlowKind::Update
                                       ? wtr("An update was stopped at \"%1\"")
                                       : wtr("An install was stopped at \"%1\""))
                                      .arg(labelOf(step)));
            done_->setUpdate(saved_.shape.kind == FlowKind::Update);
            resumeDates_->setText(
                wtr("Started on %1, stopped at %2.")
                    .arg(QLocale().toString(saved_.started.toLocalTime(), QLocale::ShortFormat),
                         QLocale().toString(saved_.updated.toLocalTime(), QLocale::ShortFormat)));
            showPage(QStringLiteral("resume"));
            return;
        }
        if (read.outcome == ProgressRead::Outcome::Refused)
            firstError_->setText(message(QString(error::progressRefused)) + QLatin1Char(' ') +
                                 read.reason);
        else
            firstError_->setText(message(QString(error::noProgress)));
        firstError_->show();
        shape_.kind = FlowKind::Install;
        entry_ = WizardEntry::Install;
    }
    if (entry_ == WizardEntry::Uninstall) {
        setItems(stepsFor(shape_), 0);
        showPage(QStringLiteral("uninstall"));
    } else if (entry_ == WizardEntry::Update) {
        refreshUpdate();
        showPage(QStringLiteral("update"));
    } else {
        refreshFirst();
        showPage(QStringLiteral("install"));
    }
}

void WizardWindow::build()
{
    setObjectName(QStringLiteral("wizardWindow"));
    setWindowTitle(wtr("TacetOS · %1").arg(target_.name));
    resize(880, 620);
    setMinimumSize(760, 540);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Section 11.
    banner_ = new QFrame(this);
    banner_->setObjectName(QStringLiteral("devBuildBanner"));
    banner_->setFixedHeight(36);
    auto *bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(24, 0, 24, 0);
    auto *bannerText = new QLabel(
        wtr("This development build does not write to iPods. Every step runs up to the first "
            "write, then stops"),
        banner_);
    bannerLayout->addWidget(bannerText);
    root->addWidget(banner_);
    banner_->setVisible(!options_.realWrites);

    // The header.
    auto *header = new QWidget(this);
    header->setFixedHeight(72);
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 8, 24, 8);
    title_ = new QLabel(entry_ == WizardEntry::Uninstall ? wtr("Uninstall TacetOS")
                        : entry_ == WizardEntry::Update  ? wtr("Update TacetOS")
                                                         : wtr("Install TacetOS"),
                        header);
    title_->setObjectName(QStringLiteral("wizardTitle"));
    title_->setProperty("role", QStringLiteral("title"));
    subtitle_ = new QLabel(target_.name, header);
    subtitle_->setProperty("role", QStringLiteral("secondary"));
    headerLayout->addWidget(title_);
    headerLayout->addWidget(subtitle_);
    root->addWidget(header);

    // The body: the steps, then the page.
    auto *body = new QHBoxLayout;
    body->setSpacing(0);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("wizardSteps"));
    list_->setFixedWidth(200);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setAccessibleName(wtr("Steps"));
    body->addWidget(list_);

    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("wizardPages"));
    addPage(QStringLiteral("install"), buildFirstPage());
    auto *uninstall = new QWidget;
    auto *uninstallLayout = new QVBoxLayout(uninstall);
    auto *uninstallText = new QLabel(
        wtr("TacetOS will be removed from this iPod. Your music stays where it is."), uninstall);
    uninstallText->setWordWrap(true);
    uninstallLayout->addWidget(uninstallText);
    uninstallLayout->addStretch();
    addPage(QStringLiteral("uninstall"), uninstall);
    addPage(QStringLiteral("update"), buildUpdatePage());
    addPage(QStringLiteral("resume"), buildResumePage());
    addPage(QStringLiteral("step"), buildStepPage());
    addPage(QStringLiteral("offer"), buildOfferPage());

    // The pages of the steps (sections 3 to 5). Each one follows the runner by itself; the
    // window only chooses which one shows.
    diagnose_ = new DiagnosePage(runner_, target_.serial, target_.model, target_.family);
    if (options_.udevInstaller)
        diagnose_->setUdevInstaller(options_.udevInstaller);
    if (options_.volumeMounter)
        diagnose_->setVolumeMounter(options_.volumeMounter);
    connect(diagnose_, &DiagnosePage::readyToGoOn, this, &WizardWindow::onPageReady);
    addPage(QStringLiteral("diagnose"), diagnose_);
    backup_ = new BackupPage(runner_);
    connect(backup_, &BackupPage::readyToGoOn, this, &WizardWindow::onPageReady);
    addPage(QStringLiteral("backup"), backup_);
    format_ = new FormatPage(runner_);
    addPage(QStringLiteral("format"), format_);
    // Boot screen section 10: one bar for the steps that write, over their own pages.
    big_ = new BigProgressPage(runner_);
    addPage(QStringLiteral("big"), big_);
    // The pages of the last steps (sections 6 to 9). Same rule: each follows the runner.
    dfu_ = new DfuTutorialPage(runner_);
    connect(dfu_, &DfuTutorialPage::retryRequested, this, &WizardWindow::retry);
    addPage(QStringLiteral("dfu"), dfu_);
    install_ = new InstallPage(runner_);
    install_->setBackupFolder(QString());
    connect(install_, &InstallPage::retryRequested, this, &WizardWindow::retry);
    connect(install_, &InstallPage::closeRequested, this, &QWidget::close);
    addPage(QStringLiteral("write"), install_);
    uninstall_page_ = new UninstallPage(runner_);
    uninstall_page_->setIpod(target_.model, target_.name);
    uninstall_page_->setUninstall(shape_.kind == FlowKind::Uninstall);
    uninstall_page_->setBackupFolder(options_.backupRoot);
    connect(uninstall_page_, &UninstallPage::retryRequested, this, &WizardWindow::retry);
    connect(uninstall_page_, &UninstallPage::closeRequested, this, &QWidget::close);
    addPage(QStringLiteral("remove"), uninstall_page_);
    done_ = new DonePage(runner_);
    done_->setIpod(target_.model, target_.name);
    done_->setClassic(target_.family == Family::Dfu);
    done_->setUpdate(shape_.kind == FlowKind::Update);
    connect(done_, &DonePage::closeRequested, this, &QWidget::close);
    addPage(QStringLiteral("done"), done_);
    if (options_.ejecter) {
        done_->setEjecter(options_.ejecter);
        uninstall_page_->setEjecter(options_.ejecter);
    }
    report_page_ = new ReportPage;
    connect(report_page_, &ReportPage::backRequested, this, &WizardWindow::closeReport);
    addPage(QStringLiteral("report"), report_page_);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(pages_);
    body->addWidget(scroll, 1);
    root->addLayout(body, 1);

    // The footer.
    auto *footer = new QWidget(this);
    footer->setFixedHeight(64);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(24, 0, 24, 0);
    report_ = button(wtr("Report"), "secondary", "wizardReport", footer);
    cancel_ = button(wtr("Cancel"), "secondary", "wizardCancel", footer);
    main_button_ = button(QString(), "primary", "wizardMain", footer);
    footerLayout->addWidget(report_);
    footerLayout->addStretch();
    footerLayout->addWidget(cancel_);
    footerLayout->addWidget(main_button_);
    root->addWidget(footer);
    report_->hide();

    connect(report_, &QPushButton::clicked, this, &WizardWindow::openReport);
    connect(main_button_, &QPushButton::clicked, this, &WizardWindow::onMain);
    connect(cancel_, &QPushButton::clicked, this, &WizardWindow::onCancel);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, cancel_, &QPushButton::click);
}

QWidget *WizardWindow::buildFirstPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    firstError_ = label("firstError", page);
    firstError_->setProperty("role", QStringLiteral("danger"));
    firstError_->hide();
    layout->addWidget(firstError_);
    auto *heading = new QLabel(wtr("Choose how to install"), page);
    heading->setProperty("role", QStringLiteral("title"));
    layout->addWidget(heading);

    clean_ = new QRadioButton(wtr("Clean install"), page);
    clean_->setObjectName(QStringLiteral("pathClean"));
    cleanInfo_ = label("pathCleanInfo", page);
    keep_ = new QRadioButton(wtr("Keep my music"), page);
    keep_->setObjectName(QStringLiteral("pathKeep"));
    keepInfo_ = label("pathKeepInfo", page);
    layout->addWidget(clean_);
    layout->addWidget(cleanInfo_);
    layout->addWidget(keep_);
    layout->addWidget(keepInfo_);
    connect(clean_, &QRadioButton::clicked, this, [this] { setPath(InstallPath::Clean); });
    connect(keep_, &QRadioButton::clicked, this, [this] { setPath(InstallPath::Keep); });

    auto addLine = [&](const QString &name, QLabel **value, const QString &buttonText,
                       const char *buttonName, void (WizardWindow::*slot)()) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(name, page));
        *value =
            label(buttonName == QByteArray("packageChoose") ? "packageLabel" : "backupFolderLabel",
                  page, false);
        row->addWidget(*value, 1);
        QPushButton *b = button(buttonText, "secondary", buttonName, page);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, slot);
        layout->addLayout(row);
    };
    addLine(wtr("Firmware package"), &packageLabel_, wtr("Choose..."), "packageChoose",
            &WizardWindow::choosePackage);
    packageLabel_->setText(wtr("None chosen"));
    packageWhy_ = label("packageWhy", page);
    packageWhy_->setProperty("role", QStringLiteral("danger"));
    packageWhy_->hide();
    layout->addWidget(packageWhy_);
    addLine(wtr("Backup folder"), &folderLabel_, wtr("Change..."), "backupFolderChange",
            &WizardWindow::changeFolder);

    plan_ = label("planSummary", page);
    layout->addWidget(plan_);
    layout->addStretch();
    return page;
}

// Update section 7: no path and no package, the versions, the backup folder and the plan.
QWidget *WizardWindow::buildUpdatePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    updateVersions_ = label("updateVersions", page);
    updateVersions_->setProperty("role", QStringLiteral("title"));
    layout->addWidget(updateVersions_);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(wtr("Backup folder"), page));
    updateFolderLabel_ = label("updateBackupFolderLabel", page, false);
    row->addWidget(updateFolderLabel_, 1);
    QPushButton *change = button(wtr("Change..."), "secondary", "updateBackupFolderChange", page);
    row->addWidget(change);
    connect(change, &QPushButton::clicked, this, &WizardWindow::changeFolder);
    layout->addLayout(row);
    updatePlan_ = label("updatePlan", page);
    layout->addWidget(updatePlan_);
    layout->addStretch();
    return page;
}

QWidget *WizardWindow::buildResumePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    resumeTitle_ = label("resumeTitle", page);
    resumeTitle_->setProperty("role", QStringLiteral("title"));
    resumeDates_ = label("resumeDates", page);
    auto *safe = label("resumeSafe", page);
    safe->setText(wtr("Your backups are kept."));
    layout->addWidget(resumeTitle_);
    layout->addWidget(resumeDates_);
    layout->addWidget(safe);
    auto *row = new QHBoxLayout;
    row->addStretch();
    QPushButton *again = button(wtr("Start again"), "secondary", "startAgain", page);
    QPushButton *go = button(wtr("Resume"), "primary", "resumeFlow", page);
    row->addWidget(again);
    row->addWidget(go);
    layout->addLayout(row);
    layout->addStretch();
    connect(again, &QPushButton::clicked, this, &WizardWindow::startAgain);
    connect(go, &QPushButton::clicked, this, &WizardWindow::resume);
    return page;
}

QWidget *WizardWindow::buildStepPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    failure_ = label("stepFailure", page);
    failure_->setProperty("role", QStringLiteral("danger"));
    failure_->hide();
    failureReport_ = ReportPage::failureButton(page);
    failureReport_->hide();
    connect(failureReport_, &QPushButton::clicked, this, &WizardWindow::openReport);
    stepTitle_ = label("stepTitle", page);
    stepTitle_->setProperty("role", QStringLiteral("title"));
    stepText_ = label("stepText", page);
    bar_ = new QProgressBar(page);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setTextVisible(false);
    bar_->setRange(0, 1000);
    barText_ = label("stepProgressText", page);
    layout->addWidget(failure_);
    layout->addWidget(failureReport_, 0, Qt::AlignLeft);
    layout->addWidget(stepTitle_);
    layout->addWidget(stepText_);
    layout->addWidget(bar_);
    layout->addWidget(barText_);
    layout->addStretch();
    return page;
}

QWidget *WizardWindow::buildOfferPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    offerTitle_ = label("offerTitle", page);
    offerTitle_->setProperty("role", QStringLiteral("title"));
    offerText_ = label("offerText", page);
    layout->addWidget(offerTitle_);
    layout->addWidget(offerText_);
    auto *row = new QHBoxLayout;
    row->addStretch();
    QPushButton *no = button(wtr("Not now"), "secondary", "offerNo", page);
    QPushButton *yes = button(wtr("Yes"), "primary", "offerYes", page);
    row->addWidget(no);
    row->addWidget(yes);
    layout->addLayout(row);
    layout->addStretch();
    connect(no, &QPushButton::clicked, this, [this] {
        UserAnswer a;
        a.yes = false;
        waiting_ = false;
        runner_->answer(a);
        showPage(QStringLiteral("step"));
    });
    connect(yes, &QPushButton::clicked, this, [this] {
        waiting_ = false;
        runner_->answer(UserAnswer());
        showPage(QStringLiteral("step"));
    });
    return page;
}

void WizardWindow::connectRunner()
{
    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int index, const QString &name) { onStarted(index, name); });
    connect(runner_, &WizardRunner::stepProgress, this,
            [this](const QString &name, const QString &stage, qint64 done, qint64 total) {
                onProgress(done, total);
                // The restart of the Classic is the page of the step after the DFU guide. The
                // helper says `resume` at the end of every try, also when nothing was sent: only
                // a try that reached `send` restarts the iPod.
                if (stage == QLatin1String("check"))
                    dfuSent_ = false;
                else if (stage == QLatin1String("send"))
                    dfuSent_ = true;
                if (stage == QLatin1String("resume") && dfuSent_ && !restartSeen_ &&
                    WholeProgress::isRestart(name)) {
                    restartSeen_ = true;
                    showBig();
                }
                // Boot screen section 10.1: a step that writes shows the big bar while it runs;
                // its own page comes back when it asks or fails. The Classic's steps keep the
                // DFU guide until the restart.
                if (name == currentStep_ && WholeProgress::isWriting(name) &&
                    !WholeProgress::isRestart(name))
                    showBig();
            });
    connect(runner_, &WizardRunner::stepFirstWrite, this, [this](const QString &) {
        writing_ = true;
        refreshFooter();
    });
    connect(runner_, &WizardRunner::stepWaiting, this,
            [this](const QString &name, WaitKind kind, const QJsonObject &info) {
                onWaiting(name, kind, info);
            });
    connect(runner_, &WizardRunner::stepDone, this, [this](int index, const QString &name) {
        onDone(index);
        // Sections 3 and 4.1: the end line shows for a pause before the next page.
        if (name == step::diagnose || name == step::backupFull || name == step::backupKeep ||
            name == step::backupSettings)
            holding_ = true;
        if (name == step::backupFull || name == step::backupKeep || name == step::backupSettings) {
            const QString folder = backupFolder();
            backup_->setBackupFolder(folder);
            format_->setBackupFolder(folder);
            install_->setBackupFolder(folder);
        }
    });
    connect(runner_, &WizardRunner::stepFailed, this,
            [this](int index, const QString &name, const QString &code, const QString &detail) {
                onFailed(index, name, code, detail);
            });
    connect(runner_, &WizardRunner::flowFinished, this, [this](const QJsonObject &) {
        active_ = writing_ = waiting_ = false;
        holding_ = false;
        pending_.clear();
        for (int i = 0; i < states_.size(); ++i)
            setItemState(i, QStringLiteral("done"));
        stepTitle_->setText(wtr("Done"));
        const QString last = currentFlowPage();
        if (last == QLatin1String("big"))
            showStepPage(currentStep_);
        else if (last == QLatin1String("done") || last == QLatin1String("remove"))
            showPage(last);
        else
            showPage(QStringLiteral("step"));
        main_ = Main::Close;
        refreshFooter();
        emit flowEnded();
    });
    connect(runner_, &WizardRunner::flowStopped, this, [this](const QString &) { onStopped(); });
}

void WizardWindow::addPage(const QString &name, QWidget *page)
{
    pageOf_.insert(name, page);
    pages_->addWidget(page);
}

QString WizardWindow::pageName() const
{
    return pageOf_.key(pages_->currentWidget());
}

// The pages a flow shows once it has started.
bool WizardWindow::isFlowPage(const QString &name) const
{
    return name == QLatin1String("step") || name == QLatin1String("offer") ||
           name == QLatin1String("diagnose") || name == QLatin1String("backup") ||
           name == QLatin1String("format") || name == QLatin1String("dfu") ||
           name == QLatin1String("write") || name == QLatin1String("remove") ||
           name == QLatin1String("done") || name == QLatin1String("big");
}

// The page of the step, also while the report shows over it.
QString WizardWindow::currentFlowPage() const
{
    return reportOpen_ ? beforeReport_ : pageName();
}

// The page of a step of the runner (wizard section 2.3): its own page when it has one, else the
// page with a bar.
void WizardWindow::showStepPage(const QString &name)
{
    QString page = QStringLiteral("step");
    if (name == step::diagnose)
        page = QStringLiteral("diagnose");
    else if (name == step::backupFull || name == step::backupKeep || name == step::backupSettings)
        page = QStringLiteral("backup");
    else if (name == step::format || name == step::convert)
        page = QStringLiteral("format");
    else if (name == step::classicBootloader || name == step::classicRemove)
        page = restartSeen_ ? (name == step::classicBootloader ? QStringLiteral("write")
                                                               : QStringLiteral("remove"))
                            : QStringLiteral("dfu");
    else if (name == step::firmware || name == step::bootloader || name == step::diskMode)
        page = QStringLiteral("write");
    else if (name == step::removeBootloader || name == step::removeFirmware)
        page = QStringLiteral("remove");
    else if (name == step::verify || name == step::finish)
        page =
            shape_.kind == FlowKind::Uninstall ? QStringLiteral("remove") : QStringLiteral("done");
    if (holding_) {
        pending_ = page;
        return;
    }
    showPage(page);
}

// Boot screen section 10: the big bar, after the pause of the page before if there is one.
void WizardWindow::showBig()
{
    if (holding_) {
        pending_ = QStringLiteral("big");
        return;
    }
    if (currentFlowPage() != QLatin1String("big"))
        showPage(QStringLiteral("big"));
}

// The end line of the checks or of the backup has shown for its pause: the next page comes.
void WizardWindow::onPageReady()
{
    holding_ = false;
    if (!pending_.isEmpty())
        showPage(pending_);
    pending_.clear();
}

// The dated folder of the backup of this flow (the progress file keeps it), empty before.
QString WizardWindow::backupFolder() const
{
    const ProgressRead read = ProgressStore(options_.dataDir).load(target_.serial);
    return read.outcome == ProgressRead::Outcome::Ok ? read.progress.backup : QString();
}

void WizardWindow::showPage(const QString &name)
{
    // The report stays over the page of the step; Back finds the new page.
    if (reportOpen_)
        beforeReport_ = name;
    else
        pages_->setCurrentWidget(pageOf_.value(name));
    if (name == QLatin1String("install") || name == QLatin1String("uninstall") ||
        name == QLatin1String("update"))
        main_ = Main::Start;
    else if (name == QLatin1String("resume") || name == QLatin1String("offer"))
        main_ = Main::None;
    else if (!active_)
        main_ = Main::Close;
    else
        main_ = Main::None;
    refreshFooter();
}

void WizardWindow::refreshFooter()
{
    const bool onPage = isFlowPage(currentFlowPage());
    main_button_->setVisible(main_ != Main::None);
    if (main_ == Main::Start) {
        main_button_->setText(entry_ == WizardEntry::Uninstall ? wtr("Uninstall")
                              : entry_ == WizardEntry::Update  ? wtr("Update")
                                                               : wtr("Start"));
        main_button_->setEnabled(entry_ == WizardEntry::Uninstall ||
                                 entry_ == WizardEntry::Update ||
                                 (path_ != InstallPath::None && packageOk_));
    } else if (main_ == Main::Close) {
        main_button_->setText(wtr("Close"));
        main_button_->setEnabled(true);
    }
    main_button_->setAccessibleName(main_button_->text());
    report_->setVisible(!reportOpen_ && (active_ || onPage));

    // Section 1.2: on while a step waits, and before the first write.
    const bool canStop = !onPage || !active_ || waiting_ || !writing_;
    cancel_->setEnabled(canStop);
    cancel_->setVisible(!(onPage && !active_));
    cancel_->setToolTip(canStop ? QString()
                                : wtr("TacetOS is writing to the iPod. This step goes to its end"));
}

void WizardWindow::setRealWrites(bool on)
{
    options_.realWrites = on;
    banner_->setVisible(!on);
}

qint64 WizardWindow::free() const
{
    if (options_.freeBytes)
        return options_.freeBytes(options_.backupRoot);
    QDir().mkpath(options_.backupRoot);
    const QStorageInfo info(options_.backupRoot);
    return info.isValid() ? info.bytesAvailable() : -1;
}

void WizardWindow::refreshFirst()
{
    if (shape_.kind == FlowKind::Update) {
        refreshUpdate();
        return;
    }
    const qint64 cleanNeed =
        backupSpaceNeeded(BackupKind::Full, target_.volumeUsed, 0, target_.partitionBytes);
    const qint64 keepNeed = backupSpaceNeeded(BackupKind::Keep, target_.volumeUsed,
                                              target_.keepBytes, target_.partitionBytes);
    const qint64 freeBytes = free();
    const PathOffer offer =
        offerPaths(target_.family, target_.macFormat, cleanNeed, keepNeed, freeBytes);
    const QString freeText = freeBytes >= 0 ? gigabytes(freeBytes) : wtr("unknown");

    clean_->setEnabled(offer.clean);
    keep_->setEnabled(offer.keep);
    auto info = [&](QLabel *l, bool ok, const QString &why, const QString &text, qint64 need) {
        l->setText(
            ok ? text + QLatin1Char(' ') +
                     wtr("Needs %1 on this computer, %2 free.").arg(gigabytes(need), freeText)
               : why);
        l->setProperty("role", ok ? QString() : QStringLiteral("danger"));
    };
    info(cleanInfo_, offer.clean, offer.cleanWhy,
         wtr("Everything on the iPod is copied to this computer and checked, then the iPod is "
             "erased and gets TacetOS only."),
         cleanNeed);
    info(keepInfo_, offer.keep, offer.keepWhy,
         wtr("Only your settings, themes and playlists are copied. TacetOS is added next to your "
             "music."),
         keepNeed);

    if ((path_ == InstallPath::Clean && !offer.clean) ||
        (path_ == InstallPath::Keep && !offer.keep) || path_ == InstallPath::None)
        path_ = offer.chosen;
    clean_->setChecked(path_ == InstallPath::Clean);
    keep_->setChecked(path_ == InstallPath::Keep);
    folderLabel_->setText(homeShort(options_.backupRoot));

    shape_.path = path_;
    if (!active_)
        setItems(stepsFor(shape_), 0);

    QStringList words;
    for (const QString &item : stepLabels())
        words.append(item.toLower());
    const qint64 bytes = (path_ == InstallPath::Keep ? target_.keepBytes : target_.volumeUsed) +
                         target_.partitionBytes;
    plan_->setText(wtr("What will happen: %1. %2. Keep the iPod plugged in.")
                       .arg(words.join(QStringLiteral(", ")), duration(bytes / kCopySpeedGuess)));
    refreshFooter();
}

// Update section 7: "TacetOS 0.2.0 > 0.3.0", the folder, and what will happen.
void WizardWindow::refreshUpdate()
{
    updateVersions_->setText(
        wtr("TacetOS %1 > %2").arg(target_.tacetosVersion, target_.updateVersion));
    updateFolderLabel_->setText(homeShort(options_.backupRoot));
    if (!active_)
        setItems(stepsFor(shape_), 0);
    QStringList words;
    for (const QString &item : stepLabels())
        words.append(item.toLower());
    updatePlan_->setText(wtr("What will happen: %1. Keep the iPod plugged in.")
                             .arg(words.join(QStringLiteral(", "))));
    refreshFooter();
}

void WizardWindow::setPath(InstallPath path)
{
    path_ = path;
    refreshFirst();
}

void WizardWindow::setBackupRoot(const QString &folder)
{
    options_.backupRoot = folder;
    refreshFirst();
}

void WizardWindow::setPackage(const QString &manifestPath)
{
    PackageFiles files;
    const PackageCheck check =
        checkPackage(manifestPath, target_.bootloaderTarget, target_.firmwareFolder, &files);
    packageOk_ = check.code.isEmpty();
    if (packageOk_) {
        packageManifest_ = manifestPath;
        packageSha_ = check.manifestSha256;
        packageLabel_->setText(wtr("%1  %2  %3")
                                   .arg(QFileInfo(manifestPath).completeBaseName(), files.version,
                                        target_.bootloaderTarget));
        packageWhy_->hide();
    } else {
        packageManifest_.clear();
        packageSha_.clear();
        packageLabel_->setText(wtr("None chosen"));
        packageWhy_->setText(check.message + QLatin1Char(' ') + wtr("Choose another package."));
        packageWhy_->show();
    }
    refreshFooter();
}

void WizardWindow::choosePackage()
{
    const QString file = QFileDialog::getOpenFileName(this, wtr("Firmware package"), QString(),
                                                      wtr("Package manifests (*.json)"));
    if (!file.isEmpty())
        setPackage(file);
}

void WizardWindow::changeFolder()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, wtr("Backup folder"), options_.backupRoot);
    if (!dir.isEmpty())
        setBackupRoot(dir);
}

void WizardWindow::setItems(const QStringList &steps, int done)
{
    steps_ = steps;
    big_->setSteps(steps);
    list_->clear();
    itemOfStep_.clear();
    states_.clear();
    QStringList labels;
    for (const QString &name : steps) {
        const QString text = labelOf(name);
        int item = labels.indexOf(text);
        if (item < 0) {
            labels.append(text);
            item = labels.size() - 1;
            list_->addItem(new QListWidgetItem);
            states_.append(QStringLiteral("todo"));
        }
        itemOfStep_.append(item);
    }
    labels_ = labels;
    for (int i = 0; i < steps.size() && i < done; ++i)
        states_[itemOfStep_.at(i)] = QStringLiteral("done");
    for (int i = 0; i < labels.size(); ++i)
        setItemState(i, states_.at(i));
}

QStringList WizardWindow::stepLabels() const
{
    return labels_;
}

QString WizardWindow::stepState(int item) const
{
    return states_.value(item);
}

void WizardWindow::setItemState(int item, const QString &state)
{
    if (item < 0 || item >= states_.size())
        return;
    states_[item] = state;
    QListWidgetItem *it = list_->item(item);
    it->setText(signOf(state) + QLatin1Char(' ') + labels_.at(item));
    it->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2").arg(labels_.at(item), state));
    QFont f = it->font();
    f.setWeight(state == QLatin1String("running") || state == QLatin1String("waiting")
                    ? QFont::DemiBold
                    : QFont::Normal);
    it->setFont(f);
}

void WizardWindow::onStarted(int index, const QString &name)
{
    writing_ = waiting_ = false;
    stopped_ = false;
    active_ = true;
    restartSeen_ = dfuSent_ = false;
    currentStep_ = name;
    failure_->hide();
    failureReport_->hide();
    for (int i = 0; i < index && i < itemOfStep_.size(); ++i)
        setItemState(itemOfStep_.at(i), QStringLiteral("done"));
    if (index < itemOfStep_.size()) {
        setItemState(itemOfStep_.at(index), QStringLiteral("running"));
        stepTitle_->setText(labels_.at(itemOfStep_.at(index)));
    }
    stepText_->clear();
    bar_->setRange(0, 0);
    barText_->clear();
    // From one writing step to the next, the big bar stays (boot screen section 10.1).
    if (currentFlowPage() == QLatin1String("big") && !holding_ && WholeProgress::isWriting(name) &&
        !WholeProgress::isRestart(name))
        return;
    showStepPage(name);
}

void WizardWindow::onProgress(qint64 done, qint64 total)
{
    if (total > 0) {
        bar_->setRange(0, 1000);
        bar_->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
        // A download is a few megabytes: its sizes in the units that fit.
        const bool small = currentStep_ == step::download;
        barText_->setText(wtr("%1 of %2")
                              .arg(small ? sizeText(done) : gigabytes(done),
                                   small ? sizeText(total) : gigabytes(total)));
    } else {
        bar_->setRange(0, 0);
    }
}

void WizardWindow::onWaiting(const QString &name, WaitKind kind, const QJsonObject &info)
{
    waiting_ = true;
    const int index = steps_.indexOf(name);
    if (index >= 0)
        setItemState(itemOfStep_.at(index), QStringLiteral("waiting"));
    // The guide asks for a new try (the iPod came back as a disk, or the transfer failed).
    if (kind == WaitKind::ReadyForDfu)
        restartSeen_ = dfuSent_ = false;
    const bool ownPage = name == step::firmware || name == step::bootloader ||
                         name == step::diskMode || name == step::classicBootloader ||
                         name == step::classicRemove || name == step::removeBootloader ||
                         name == step::removeFirmware;
    if (kind == WaitKind::Offer && !ownPage) {
        // Section 1.4: a page of its own, the yes as the main button.
        offerTitle_->setText(info.value(QStringLiteral("question")).toString(wtr("Go on?")));
        offerText_->setText(info.value(QStringLiteral("text")).toString());
        showPage(QStringLiteral("offer"));
    } else {
        // The page of the step asks by itself (the battery on the checks, the typed name on the
        // erase). It shows at once, even during the pause of the page before.
        holding_ = false;
        pending_.clear();
        showStepPage(name);
    }
}

void WizardWindow::onDone(int index)
{
    writing_ = false;
    if (index >= itemOfStep_.size())
        return;
    const int item = itemOfStep_.at(index);
    if (index + 1 >= itemOfStep_.size() || itemOfStep_.at(index + 1) != item)
        setItemState(item, QStringLiteral("done"));
}

void WizardWindow::onFailed(int index, const QString &name, const QString &code,
                            const QString &detail)
{
    if (index < itemOfStep_.size())
        setItemState(itemOfStep_.at(index), QStringLiteral("failed"));
    // The detail (the check that blocks, the file that failed) stays with the code. The space a
    // download needs is part of its sentence (update section 6).
    const QString text = detail.isEmpty() ? message(code)
                         : code == QString(error::downloadSpace)
                             ? message(code) + QStringLiteral(": ") + detail
                             : message(code) + QStringLiteral(" (") + detail + QLatin1Char(')');
    failure_->setText(text);
    failure_->show();
    failureReport_->show();
    failures_.append({code, detail});
    // The accessible event of section 12.
    failure_->setAccessibleName(text);
    stopped_ = code == QString(error::cancelled);
    // A page of its own shows the failure in its own way (section 1.4). A cancel ends on the
    // page with the bar, which says "Stopped" (section 1.2).
    holding_ = false;
    pending_.clear();
    if (!stopped_)
        showStepPage(name);
}

void WizardWindow::onStopped()
{
    active_ = writing_ = waiting_ = false;
    if (stopped_) {
        failure_->hide();
        failureReport_->hide();
        stepText_->setText(wtr("Stopped. Resume from the card when you want"));
    }
    holding_ = false;
    pending_.clear();
    restartSeen_ = dfuSent_ = false;
    // The page of a step keeps its failed or stopped state; the others go back to the bar.
    const QString page = currentFlowPage();
    if (!stopped_ && page == QLatin1String("big"))
        showStepPage(currentStep_);
    else
        showPage(!stopped_ && isFlowPage(page) && page != QLatin1String("offer")
                     ? page
                     : QStringLiteral("step"));
    emit flowEnded();
}

QString WizardWindow::message(const QString &code) const
{
    if (code == QString(error::noProgress))
        return wtr("There is no saved progress for this iPod.");
    if (code == QString(error::progressRefused))
        return wtr("The saved progress cannot be used.");
    if (code == QString(error::progressMismatch))
        return wtr("This is not the iPod of the saved progress.");
    if (code == QString(error::flowRunning))
        return wtr("A flow is already running for this iPod.");
    if (code == QString(error::noSerial))
        return wtr("This iPod has no serial number, so the wizard cannot start.");
    if (code == QString(error::cancelled))
        return wtr("Stopped.");
    // Update section 6.
    if (code == QString(error::updateGone))
        return wtr("This update is no longer available. Press Refresh");
    if (code == QString(error::downloadSpace))
        return wtr("Not enough space on this computer to download the update");
    if (code == QString(error::downloadDamaged))
        return wtr("The download is damaged. Try again");
    if (code == QString(error::downloadFailed))
        return wtr("The update could not be downloaded. Check the connection, then try again");
    return code;
}

bool WizardWindow::confirm(const QString &title, const QString &text)
{
    return ask_ ? ask_(title, text) : false;
}

void WizardWindow::onMain()
{
    if (main_ == Main::Start)
        start();
    else if (main_ == Main::Close)
        close();
}

void WizardWindow::onCancel()
{
    const QString page = pageName();
    if (page == QLatin1String("install") || page == QLatin1String("uninstall") ||
        page == QLatin1String("update") || page == QLatin1String("resume") || !active_) {
        close();
        return;
    }
    if (writing_ && !waiting_)
        return;
    if (confirm(wtr("Stop here?"),
                wtr("Stop here? Nothing has been written to the iPod in this step. You can "
                    "resume later from the card")))
        runner_->cancel();
}

void WizardWindow::start()
{
    WizardRunner::Start s;
    s.serial = target_.serial;
    s.model = target_.model;
    s.shape = shape_;
    if (shape_.kind == FlowKind::Update) {
        s.version = target_.updateVersion;
    } else {
        s.packageManifest = packageManifest_;
        s.packageSha256 = packageSha_;
    }
    const QString code = runner_->start(s);
    if (!code.isEmpty()) {
        firstError_->setText(message(code));
        firstError_->show();
        return;
    }
    failures_.clear();
    uninstall_page_->setUninstall(shape_.kind == FlowKind::Uninstall);
    active_ = true;
    stepTitle_->setText(wtr("Starting"));
    showPage(QStringLiteral("step"));
}

void WizardWindow::resume()
{
    const QString code = runner_->resume(target_.serial, target_.model, target_.family);
    if (!code.isEmpty()) {
        resumeDates_->setText(message(code));
        return;
    }
    failures_.clear();
    uninstall_page_->setUninstall(shape_.kind == FlowKind::Uninstall);
    active_ = true;
    stepTitle_->setText(wtr("Resuming"));
    showPage(QStringLiteral("step"));
}

// "Try again" of a page: the flow goes on from its progress file, at the step that failed. The
// page of the step shows again when the runner says the step starts.
void WizardWindow::retry()
{
    const QString code = runner_->resume(target_.serial, target_.model, target_.family);
    if (!code.isEmpty()) {
        failure_->setText(message(code));
        failure_->show();
        return;
    }
    active_ = true;
}

// Section 10: the report over the page of the step. It holds the facts of the window and what
// the app gives (versions, journal).
void WizardWindow::openReport()
{
    if (reportOpen_)
        return;
    journal::ReportInput in =
        options_.reportInput ? options_.reportInput() : journal::ReportInput();
    in.model = target_.model;
    in.family = target_.family == Family::Dfu ? QStringLiteral("dfu") : QStringLiteral("partition");
    in.serial = target_.serial;
    in.flow = kindName(shape_.kind);
    in.path = path_ == InstallPath::Keep    ? QStringLiteral("keep")
              : path_ == InstallPath::Clean ? QStringLiteral("clean")
                                            : QString();
    in.step = currentStep_;
    in.failures = failures_;
    in.backupFolder = backupFolder();
    if (in.dataFolder.isEmpty())
        in.dataFolder = options_.dataDir;
    report_page_->setInput(in);
    beforeReport_ = pageName();
    reportOpen_ = true;
    pages_->setCurrentWidget(report_page_);
    refreshFooter();
}

void WizardWindow::closeReport()
{
    if (!reportOpen_)
        return;
    reportOpen_ = false;
    pages_->setCurrentWidget(pageOf_.value(beforeReport_));
    refreshFooter();
}

void WizardWindow::startAgain()
{
    if (!confirm(wtr("Start again?"),
                 wtr("Start again from the beginning? The saved progress is deleted. Your backups "
                     "are kept")))
        return;
    ProgressStore(options_.dataDir).remove(target_.serial);
    hasSaved_ = false;
    entry_ = WizardEntry::Install;
    shape_.kind = FlowKind::Install;
    shape_.family = target_.family;
    shape_.macFormat = target_.macFormat;
    shape_.bootloaderFirst = target_.bootloaderFirst;
    refreshFirst();
    showPage(QStringLiteral("install"));
}

QString WizardWindow::quitRefusal() const
{
    if (active_ && writing_)
        return wtr("TacetOS is writing to %1. Wait for the end of this step, then quit")
            .arg(target_.name);
    return QString();
}

void WizardWindow::closeEvent(QCloseEvent *event)
{
    if (!active_) {
        event->accept();
        return;
    }
    const QString formatRefusal = format_->closeRefusal();
    if ((writing_ && !waiting_) || !formatRefusal.isEmpty()) {
        event->ignore();
        if (notify_)
            notify_(!formatRefusal.isEmpty()
                        ? formatRefusal
                        : wtr("TacetOS is writing to the iPod. Wait for the end of this step, "
                              "then close"));
        return;
    }
    // Section 1.3: out of a write, the cross closes the window like Close, after the question
    // of a Cancel. The progress file stays, so the card offers Resume.
    if (confirm(wtr("Stop here?"),
                wtr("Stop here? Nothing has been written to the iPod in this step. You can "
                    "resume later from the card"))) {
        active_ = false;
        runner_->cancel();
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace tacetos
