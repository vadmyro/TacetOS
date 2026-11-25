// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/installpage.h"

#include "flow.h"
#include "runner.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QSvgRenderer>
#include <QUrl>
#include <QVBoxLayout>

namespace tacetos {

using namespace wizard;

namespace {

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
}

qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

// Screens section 13.2: the stage of the firmware or of the helper in words. A stage the table
// does not know shows no text, never its code.
QString stageWords(const QString &step, const QString &stage)
{
    if (stage == QLatin1String("unpack"))
        return wtr("Unpacking the firmware");
    if (stage == QLatin1String("keep"))
        return wtr("Keeping your settings");
    if (stage == QLatin1String("swap"))
        return wtr("Switching to the new version");
    if (stage == QLatin1String("check") && step == QLatin1String(step::firmware))
        return wtr("Checking the files");
    if (stage == QLatin1String("unmount"))
        return wtr("Closing the iPod's volume");
    if (stage == QLatin1String("open"))
        return wtr("Waiting for your permission");
    if (stage == QLatin1String("check"))
        return wtr("Checking the iPod");
    if (stage == QLatin1String("backup"))
        return wtr("Saving the firmware partition");
    if (stage == QLatin1String("write"))
        return wtr("Writing");
    if (stage == QLatin1String("verify"))
        return wtr("Reading back and checking");
    if (stage == QLatin1String("record"))
        return wtr("Keeping a record");
    return QString();
}

// The steps to reset the iPod, then DFU mode and the restore by Apple's software, for the
// failures that never offer to install again (bootloader-classic.md sections 12 and 16.3).
QString resetSteps()
{
    return wtr("Reset the iPod: slide the Hold switch on and off, then hold MENU and the center "
               "button until the screen goes black. If it still does not start, put it in DFU "
               "mode and restore it with Apple's software. Your backup is kept.");
}

// Wizard section 13: the message of a code of these steps.
QString failureText(const QString &code, int takeFailures)
{
    if (code == error::packageWrongModel)
        return wtr("This package is not for this iPod. Choose the package of your model.");
    if (code == error::packageDamaged || code == error::packageIncomplete ||
        code == error::packageFormat)
        return wtr("The package is damaged or incomplete. Download it again.");
    if (code == error::packageOtherFolder)
        return wtr("This package is for an older version of TacetOS. Choose the package of "
                   "this version.");
    if (code == error::packageChanged)
        return wtr("The package changed after it was chosen. Choose it again.");
    if (code == error::firmwareNoRoom)
        return wtr("There is not enough room on the iPod for this firmware.");
    if (code == error::firmwareWriteFailed)
        return wtr("The firmware could not be written. Unplug the iPod, plug it in again, then "
                   "press Resume.");
    if (code == error::backupNotVerified)
        return wtr("No verified backup of this iPod. Run the backup again.");
    if (code == error::ipodGone)
        return wtr("The iPod was unplugged. Plug it in again and press Resume.");
    if (code == error::volumeNotBack)
        return wtr("The iPod's volume did not come back. Plug it in again and press Resume.");
    if (code == error::verifyFailed)
        return wtr("The bootloader is not in the state it should be. Nothing more was written.");
    if (code == error::bootloaderNotTaken) {
        if (takeFailures == 0)
            return wtr("The iPod started again with Apple's firmware: the bootloader did not take. "
                       "Nothing is broken.");
        return wtr("The bootloader did not take again. Common causes: a low battery, a USB hub, "
                   "or buttons let go too early. Charge the iPod, plug it straight into the "
                   "computer, and ask on the Discord if it keeps happening.");
    }
    if (code == error::ipodNotBack || code == error::dfuFailed)
        return resetSteps();
    if (code == QLatin1String("in-use"))
        return wtr("Another program is using the iPod. Close it, then try again.");
    if (code == QLatin1String("permission-denied"))
        return wtr("TacetOS was not given the permission to write to the iPod.");
    return wtr("The step could not be finished.");
}

QLabel *makeLabel(const QString &name, QWidget *parent, const char *role = nullptr)
{
    auto *l = new QLabel(parent);
    l->setObjectName(name);
    l->setWordWrap(true);
    if (role)
        l->setProperty("role", QString::fromLatin1(role));
    return l;
}

QPushButton *makeButton(const QString &name, QWidget *parent, const char *role = nullptr)
{
    auto *b = new QPushButton(parent);
    b->setObjectName(name);
    if (role)
        b->setProperty("role", QString::fromLatin1(role));
    return b;
}

} // namespace

QPixmap modelPixmap(const QString &model, int height, qreal ratio)
{
    QSvgRenderer renderer(QStringLiteral(":/models/%1.svg").arg(model));
    if (!renderer.isValid())
        renderer.load(QStringLiteral(":/models/generic.svg"));
    if (!renderer.isValid())
        return QPixmap();
    const QSizeF view = renderer.viewBoxF().size();
    const int width = view.height() > 0 ? qRound(height * view.width() / view.height()) : height;
    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, width, height));
    painter.end();
    return pixmap;
}

QStringList InstallPage::diskModeSteps(const QString &model)
{
    // Screens section 13.4 (D22: facts, checked on Apple's page about Disk Mode and on the
    // manuals of the models).
    if (model == QLatin1String("ipod-1g2g") || model == QLatin1String("ipod-3g"))
        return {wtr("Hold MENU and PLAY until the Apple logo shows (about 6 s)."),
                wtr("At once, hold PREVIOUS and NEXT until \"Do not disconnect\" shows.")};
    return {wtr("Hold MENU and the center button until the Apple logo shows (about 6 s)."),
            wtr("At once, hold the center button and PLAY until \"Do not disconnect\" shows.")};
}

QList<QPair<QString, QString>> InstallPage::beepTable()
{
    return {{wtr("one short high beep"), wtr("the program runs")},
            {wtr("low, then high"), wtr("done, the iPod restarts")},
            {wtr("one long low beep"), wtr("nothing changed, the iPod is as before")},
            {wtr("two long low beeps"), wtr("not enough room, nothing was written")},
            {wtr("three long low beeps"), wtr("the iPod may not start (see below)")}};
}

InstallPage::InstallPage(WizardRunner *runner, QWidget *parent)
    : QWidget(parent), runner_(runner), restartMs_(kRestartWindowMs)
{
    setObjectName(QStringLiteral("installPage"));
    clock_ = monotonicMs;
    opener_ = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, &InstallPage::refreshCountdown);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    title_ = makeLabel(QStringLiteral("installTitle"), this);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);
    version_ = makeLabel(QStringLiteral("installVersion"), this, "secondary");
    version_->hide();
    message_ = makeLabel(QStringLiteral("installMessage"), this);
    message_->hide();

    // Section 7.2: Disk Mode.
    diskBlock_ = new QWidget(this);
    diskBlock_->setObjectName(QStringLiteral("installDiskMode"));
    auto *disk = new QHBoxLayout(diskBlock_);
    disk->setContentsMargins(0, 0, 0, 0);
    auto *diskText = new QVBoxLayout;
    diskSteps_ = makeLabel(QStringLiteral("diskModeSteps"), diskBlock_);
    diskMode_ = makeButton(QStringLiteral("diskModeDone"), diskBlock_);
    diskMode_->setText(wtr("The iPod is in Disk Mode"));
    diskMode_->setAccessibleName(wtr("The iPod is in Disk Mode"));
    diskText->addWidget(diskSteps_);
    diskText->addSpacing(16);
    diskText->addWidget(diskMode_, 0, Qt::AlignLeft);
    diskDrawing_ = new QLabel(diskBlock_);
    diskDrawing_->setObjectName(QStringLiteral("diskModeDrawing"));
    diskDrawing_->setAccessibleName(wtr("Drawing of the iPod"));
    disk->addLayout(diskText, 1);
    disk->addWidget(diskDrawing_, 0, Qt::AlignTop);
    connect(diskMode_, &QPushButton::clicked, this, [this] {
        if (!asking_)
            return;
        asking_ = false;
        diskBlock_->hide();
        title_->setText(wtr("Waiting for the iPod"));
        restartSince_ = now();
        restarting_ = true;
        restartBlock_->show();
        listen_->hide();
        beeps_->hide();
        tick_.start();
        refreshCountdown();
        runner_->answer(UserAnswer());
    });

    // Sections 7.1 and 7.2: the run.
    runBlock_ = new QWidget(this);
    runBlock_->setObjectName(QStringLiteral("installRun"));
    auto *run = new QVBoxLayout(runBlock_);
    run->setContentsMargins(0, 0, 0, 0);
    warning_ = makeLabel(QStringLiteral("installWarning"), runBlock_);
    warning_->setText(wtr("Do not unplug the iPod"));
    QFont warn = warning_->font();
    warn.setWeight(QFont::DemiBold);
    warning_->setFont(warn);
    bar_ = new QProgressBar(runBlock_);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Install progress"));
    barText_ = makeLabel(QStringLiteral("stepProgressText"), runBlock_);
    stageLine_ = makeLabel(QStringLiteral("installStage"), runBlock_, "secondary");
    run->addWidget(warning_);
    run->addSpacing(16);
    run->addWidget(bar_);
    run->addWidget(barText_);
    run->addWidget(stageLine_);

    // Section 7.3: the restart.
    restartBlock_ = new QWidget(this);
    restartBlock_->setObjectName(QStringLiteral("installRestart"));
    auto *restart = new QVBoxLayout(restartBlock_);
    restart->setContentsMargins(0, 0, 0, 0);
    listen_ = makeLabel(QStringLiteral("restartListen"), restartBlock_);
    listen_->setText(wtr("Listen: a low beep then a high one means it worked."));
    countdown_ = makeLabel(QStringLiteral("restartCountdown"), restartBlock_);
    beeps_ = makeLabel(QStringLiteral("restartBeeps"), restartBlock_, "secondary");
    QString table = wtr("What the beeps mean");
    for (const auto &row : beepTable())
        table += QStringLiteral("\n%1: %2").arg(row.first, row.second);
    beeps_->setText(table);
    restart->addWidget(listen_);
    restart->addWidget(countdown_);
    restart->addSpacing(16);
    restart->addWidget(beeps_);

    // Section 1.4 and 7.2: the offers.
    offerBlock_ = new QWidget(this);
    offerBlock_->setObjectName(QStringLiteral("installOffer"));
    auto *offer = new QHBoxLayout(offerBlock_);
    offer->setContentsMargins(0, 0, 0, 0);
    yes_ = makeButton(QStringLiteral("offerYes"), offerBlock_);
    no_ = makeButton(QStringLiteral("offerNo"), offerBlock_, "secondary");
    offer->addWidget(yes_);
    offer->addWidget(no_);
    offer->addStretch();
    connect(yes_, &QPushButton::clicked, this, [this] { answer(true); });
    connect(no_, &QPushButton::clicked, this, [this] { answer(false); });

    endBlock_ = new QWidget(this);
    endBlock_->setObjectName(QStringLiteral("installEnd"));
    auto *end = new QHBoxLayout(endBlock_);
    end->setContentsMargins(0, 0, 0, 0);
    folder_ = makeButton(QStringLiteral("openBackupFolder"), endBlock_, "secondary");
    folder_->setText(wtr("Open folder"));
    folder_->setAccessibleName(wtr("Open folder"));
    close_ = makeButton(QStringLiteral("installClose"), endBlock_, "secondary");
    close_->setText(wtr("Close"));
    close_->setAccessibleName(wtr("Close"));
    end->addWidget(folder_);
    end->addWidget(close_);
    end->addStretch();
    connect(folder_, &QPushButton::clicked, this, [this] {
        if (!folderPath_.isEmpty() && opener_)
            opener_(folderPath_);
    });
    connect(close_, &QPushButton::clicked, this, &InstallPage::closeRequested);

    layout->addWidget(title_);
    layout->addWidget(version_);
    layout->addWidget(message_);
    layout->addWidget(diskBlock_);
    layout->addWidget(runBlock_);
    layout->addWidget(restartBlock_);
    layout->addWidget(offerBlock_);
    layout->addWidget(endBlock_);
    layout->addStretch();
    for (QWidget *w : {diskBlock_, runBlock_, restartBlock_, offerBlock_, endBlock_})
        w->hide();

    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int, const QString &name) { onStarted(name); });
    connect(runner_, &WizardRunner::stepWaiting, this,
            [this](const QString &name, WaitKind kind, const QJsonObject &info) {
                onWaiting(name, int(kind), info);
            });
    connect(runner_, &WizardRunner::stepFirstWrite, this,
            [this](const QString &name) { onFirstWrite(name); });
    connect(runner_, &WizardRunner::stepProgress, this,
            [this](const QString &name, const QString &stage, qint64 done, qint64 total) {
                onProgress(name, stage, done, total);
            });
    connect(runner_, &WizardRunner::stepDone, this,
            [this](int, const QString &name) { onDone(name); });
    connect(runner_, &WizardRunner::stepFailed, this,
            [this](int, const QString &name, const QString &code, const QString &) {
                onFailed(name, code);
            });
}

bool InstallPage::mine(const QString &name) const
{
    return name == QLatin1String(step::firmware) || name == QLatin1String(step::diskMode) ||
           name == QLatin1String(step::bootloader) ||
           name == QLatin1String(step::classicBootloader);
}

qint64 InstallPage::now() const
{
    return clock_ ? clock_() : monotonicMs();
}

QString InstallPage::titleText() const
{
    return title_->text();
}

QString InstallPage::messageText() const
{
    return message_->text();
}

QString InstallPage::countdownText() const
{
    return countdown_->text();
}

QString InstallPage::versionText() const
{
    return version_->text();
}

QString InstallPage::stageText() const
{
    return stageLine_->text();
}

QStringList InstallPage::diskModeLines() const
{
    return diskSteps_->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void InstallPage::setVersion(const QString &version)
{
    version_->setText(version);
    version_->setVisible(!version.isEmpty() && step_ == QLatin1String(step::firmware));
}

void InstallPage::setBackupFolder(const QString &folder)
{
    folderPath_ = folder;
}

QString InstallPage::closeRefusal() const
{
    if (!writing_)
        return QString();
    return wtr("TacetOS is writing to the iPod. Wait for the end of this step, then close");
}

void InstallPage::announce()
{
    QAccessibleEvent alert(title_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void InstallPage::onStarted(const QString &name)
{
    if (!mine(name))
        return;
    step_ = name;
    active_ = true;
    asking_ = false;
    writing_ = false;
    failed_ = false;
    stopped_ = false;
    done_ = false;
    restarting_ = sent_ = false;
    reportMain_ = false;
    plain_ = false;
    offer_ = Offer::None;
    restartSince_ = -1;
    tick_.stop();
    message_->hide();
    version_->setVisible(!version_->text().isEmpty() && name == QLatin1String(step::firmware));
    for (QWidget *w : {diskBlock_, runBlock_, restartBlock_, offerBlock_, endBlock_})
        w->hide();
    bar_->setRange(0, 1000);
    bar_->setValue(0);
    barText_->clear();
    stageLine_->clear();
    if (name == QLatin1String(step::firmware)) {
        title_->setText(wtr("Putting TacetOS on the iPod"));
        runBlock_->show();
    } else if (name == QLatin1String(step::bootloader)) {
        title_->setText(update_ ? wtr("Updating the bootloader")
                                : wtr("Installing the bootloader"));
        runBlock_->show();
    } else if (name == QLatin1String(step::diskMode)) {
        title_->setText(wtr("Put the iPod in Disk Mode"));
    } else {
        // The DFU guide shows the steps before the restart: this page waits for it.
        title_->clear();
    }
}

void InstallPage::onWaiting(const QString &name, int kind, const QJsonObject &info)
{
    if (name != step_ || !mine(name))
        return;
    if (kind == int(WaitKind::DiskMode)) {
        model_ = info.value(QStringLiteral("model")).toString();
        diskSteps_->setText(QStringList([this] {
                                QStringList numbered;
                                int n = 1;
                                for (const QString &line : diskModeSteps(model_))
                                    numbered << QStringLiteral("%1. %2").arg(n++).arg(line);
                                return numbered;
                            }())
                                .join(QLatin1Char('\n')));
        diskDrawing_->setPixmap(modelPixmap(model_, 160, devicePixelRatioF()));
        title_->setText(wtr("Put the iPod in Disk Mode"));
        runBlock_->hide();
        diskBlock_->show();
        asking_ = true;
        diskMode_->setFocus();
        announce();
        return;
    }
    if (kind != int(WaitKind::Offer))
        return;
    const QString which = info.value(QStringLiteral("offer")).toString();
    if (which == QLatin1String("settings")) {
        offer_ = Offer::Settings;
        QString date = info.value(QStringLiteral("date")).toString();
        if (date.isEmpty())
            date = QFileInfo(info.value(QStringLiteral("backup")).toString()).fileName();
        message_->setText(wtr("Put back your settings and themes from %1").arg(date));
        yes_->setText(wtr("Put them back"));
        no_->setText(wtr("Start fresh"));
    } else if (which == QLatin1String("restore-backup")) {
        offer_ = Offer::RestoreBackup;
        message_->setText(wtr("The firmware partition was left half written. Write the backup "
                              "back?"));
        yes_->setText(wtr("Write the backup back"));
        no_->setText(wtr("Not now"));
    } else {
        return;
    }
    yes_->setProperty("role", QStringLiteral("main"));
    yes_->setAccessibleName(yes_->text());
    no_->setAccessibleName(no_->text());
    no_->show();
    message_->show();
    runBlock_->hide();
    offerBlock_->show();
    asking_ = true;
    yes_->setFocus();
    announce();
}

void InstallPage::answer(bool yes)
{
    if (!asking_)
        return;
    asking_ = false;
    const Offer which = offer_;
    offer_ = Offer::None;
    offerBlock_->hide();
    if (which == Offer::Retry) {
        message_->hide();
        if (yes)
            emit retryRequested();
        return;
    }
    message_->hide();
    runBlock_->show();
    UserAnswer reply;
    reply.yes = yes;
    runner_->answer(reply);
}

void InstallPage::onFirstWrite(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    writing_ = true;
}

void InstallPage::showRestart()
{
    restarting_ = true;
    restartSince_ = now();
    title_->setText(wtr("The iPod restarts"));
    runBlock_->hide();
    restartBlock_->show();
    listen_->show();
    beeps_->show();
    tick_.start();
    refreshCountdown();
    announce();
}

void InstallPage::onProgress(const QString &name, const QString &stage, qint64 done, qint64 total)
{
    if (name != step_ || !mine(name))
        return;
    if (name == QLatin1String(step::classicBootloader)) {
        if (stage == QLatin1String("check"))
            sent_ = false;
        else if (stage == QLatin1String("send"))
            sent_ = true;
        if (stage == QLatin1String("resume") && sent_ && !restarting_)
            showRestart();
        return;
    }
    stageLine_->setText(stageWords(name, stage));
    if (total > 0) {
        bar_->setRange(0, 1000);
        bar_->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
        if (name == QLatin1String(step::firmware) && stage == QLatin1String("unpack"))
            barText_->setText(
                wtr("%1 of %2 bytes").arg(QLocale().toString(done), QLocale().toString(total)));
        else
            barText_->clear();
    } else {
        bar_->setRange(0, 0);
    }
}

void InstallPage::refreshCountdown()
{
    if (restartSince_ < 0 || !restarting_ || !active_)
        return;
    const qint64 left = qMax<qint64>(0, restartMs_ - (now() - restartSince_));
    countdown_->setText(
        wtr("Waiting for the iPod: %1 s").arg(QLocale().toString((left + 999) / 1000)));
}

void InstallPage::onDone(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    writing_ = false;
    restarting_ = sent_ = false;
    done_ = true;
    bar_->setRange(0, 1000);
    bar_->setValue(1000);
}

void InstallPage::showFailure(const QString &text, bool plain)
{
    plain_ = plain;
    message_->setProperty("role", plain ? QStringLiteral("secondary") : QStringLiteral("danger"));
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    message_->setText(text);
    message_->show();
    runBlock_->hide();
    restartBlock_->setVisible(false);
    announce();
}

void InstallPage::onFailed(const QString &name, const QString &code)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    asking_ = false;
    writing_ = false;
    restarting_ = sent_ = false;
    offerBlock_->hide();
    endBlock_->hide();
    if (code == error::cancelled) {
        stopped_ = true;
        showFailure(wtr("Stopped. Resume from the card when you want."), true);
        return;
    }
    // Section 7.3: a development build stops before the first write, which is no failure.
    if (code == QLatin1String("dev-build")) {
        stopped_ = true;
        title_->setText(wtr("Stopped before the first write"));
        showFailure(wtr("This development build does not send anything to the iPod. Everything up "
                        "to this point was checked"),
                    true);
        return;
    }
    failed_ = true;
    const bool restored = code == error::bootloaderRestored || next_ == QLatin1String("restored");
    if (restored) {
        showFailure(wtr("The backup was written back. The iPod is as it was before the "
                        "bootloader."),
                    false);
        close_->show();
        folder_->hide();
        endBlock_->show();
        return;
    }
    if (next_ == QLatin1String("restore-failed")) {
        // Bootloader-legacy.md section 19: the plain words.
        showFailure(wtr("The backup could not be written back, and the iPod may not start. Disk "
                        "Mode is the way back: put the iPod in Disk Mode and restore it. Keep the "
                        "backup file, and ask on the Discord."),
                    false);
        reportMain_ = true;
        folder_->show();
        close_->hide();
        endBlock_->show();
        return;
    }
    showFailure(failureText(code, takeFailures_), false);
    const bool never = code == error::ipodNotBack || code == error::dfuFailed;
    if (never)
        return;
    if (next_ == QLatin1String("offer-retry") ||
        (code == error::bootloaderNotTaken && takeFailures_ == 0)) {
        offer_ = Offer::Retry;
        yes_->setText(code == error::bootloaderNotTaken ? wtr("Try once more") : wtr("Try again"));
        yes_->setProperty("role", QStringLiteral("main"));
        yes_->setAccessibleName(yes_->text());
        no_->hide();
        offerBlock_->show();
        asking_ = true;
        yes_->setFocus();
    }
}

} // namespace tacetos
