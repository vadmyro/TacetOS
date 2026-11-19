// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/uninstallpage.h"

#include "flow.h"
#include "runner.h"
#include "wizard/installpage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
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

QString tilde(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && (path == home || path.startsWith(home + QLatin1Char('/'))))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

// Screens section 13.2: the stage of the helper in words. A stage the table does not know shows
// no text, never its code.
QString stageWords(const QString &stage)
{
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
// failures that never offer to try again (bootloader-classic.md sections 12 and 16.3).
QString resetSteps()
{
    return wtr("Reset the iPod: slide the Hold switch on and off, then hold MENU and the center "
               "button until the screen goes black. If it still does not start, put it in DFU "
               "mode and restore it with Apple's software. Your backup is kept.");
}

// Wizard section 13: the message of a code of these steps. `stillFailures` counts the times that
// `still-tacetos` came back, this one included.
QString failureText(const QString &code, int stillFailures)
{
    if (code == error::stillTacetos) {
        if (stillFailures <= 1)
            return wtr("The iPod still starts TacetOS. Try again.");
        return wtr("The iPod still starts TacetOS after a second try. Charge it, plug it straight "
                   "into the computer, and ask on the Discord if it keeps happening. Your backup "
                   "is kept.");
    }
    if (code == error::backupNotVerified)
        return wtr("No verified backup of your settings. Run the backup again.");
    if (code == error::ipodGone)
        return wtr("The iPod was unplugged. Plug it in again and press Resume.");
    if (code == error::volumeNotBack)
        return wtr("The iPod's volume did not come back. Plug it in again and press Resume.");
    if (code == error::eraseFailed)
        return wtr("Some TacetOS files could not be removed. Close the programs that use the "
                   "iPod, then press Resume.");
    if (code == error::verifyFailed)
        return wtr("The iPod is not in the state it should be after the removal. Nothing more "
                   "was written.");
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

QPushButton *makeButton(const QString &name, const QString &text, QWidget *parent,
                        const char *role = nullptr)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setAccessibleName(text);
    if (role)
        b->setProperty("role", QString::fromLatin1(role));
    return b;
}

} // namespace

UninstallPage::UninstallPage(WizardRunner *runner, QWidget *parent)
    : QWidget(parent), runner_(runner), restartMs_(kRestartWindowMs)
{
    setObjectName(QStringLiteral("uninstallPage"));
    clock_ = monotonicMs;
    opener_ = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, &UninstallPage::refreshCountdown);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    auto *top = new QHBoxLayout;
    auto *head = new QVBoxLayout;
    title_ = makeLabel(QStringLiteral("uninstallTitle"), this);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);
    subtitle_ = makeLabel(QStringLiteral("uninstallSubtitle"), this);
    subtitle_->hide();
    head->addWidget(title_);
    head->addWidget(subtitle_);
    drawing_ = new QLabel(this);
    drawing_->setObjectName(QStringLiteral("uninstallDrawing"));
    drawing_->setAccessibleName(wtr("Drawing of the iPod"));
    drawing_->hide();
    top->addLayout(head, 1);
    top->addWidget(drawing_, 0, Qt::AlignTop);
    message_ = makeLabel(QStringLiteral("uninstallMessage"), this);
    message_->hide();

    // Section 9, the first screen.
    firstBlock_ = new QWidget(this);
    firstBlock_->setObjectName(QStringLiteral("uninstallFirst"));
    auto *first = new QVBoxLayout(firstBlock_);
    first->setContentsMargins(0, 0, 0, 0);
    firstText_ = makeLabel(QStringLiteral("uninstallFirstText"), firstBlock_);
    folderLine_ = makeLabel(QStringLiteral("uninstallBackupFolder"), firstBlock_, "secondary");
    change_ = makeButton(QStringLiteral("uninstallChangeFolder"), wtr("Change..."), firstBlock_,
                         "secondary");
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folderLine_, 1);
    folderRow->addWidget(change_);
    start_ = makeButton(QStringLiteral("uninstallStart"), wtr("Uninstall"), firstBlock_, "main");
    firstClose_ =
        makeButton(QStringLiteral("uninstallFirstClose"), wtr("Close"), firstBlock_, "secondary");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(start_);
    buttons->addWidget(firstClose_);
    buttons->addStretch();
    first->addWidget(firstText_);
    first->addSpacing(16);
    first->addLayout(folderRow);
    first->addSpacing(16);
    first->addLayout(buttons);
    connect(change_, &QPushButton::clicked, this, &UninstallPage::changeFolderRequested);
    connect(firstClose_, &QPushButton::clicked, this, &UninstallPage::closeRequested);
    connect(start_, &QPushButton::clicked, this, [this] {
        if (started_)
            return;
        started_ = true;
        removeAnyway_ = first_ == First::RemoveAnyway;
        start_->setEnabled(false);
        change_->setEnabled(false);
        emit startRequested();
    });

    // The run of `remove-bootloader` and `remove-firmware`.
    runBlock_ = new QWidget(this);
    runBlock_->setObjectName(QStringLiteral("uninstallRun"));
    auto *run = new QVBoxLayout(runBlock_);
    run->setContentsMargins(0, 0, 0, 0);
    warning_ = makeLabel(QStringLiteral("uninstallWarning"), runBlock_);
    warning_->setText(wtr("Do not unplug the iPod"));
    QFont warn = warning_->font();
    warn.setWeight(QFont::DemiBold);
    warning_->setFont(warn);
    bar_ = new QProgressBar(runBlock_);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Uninstall progress"));
    stageLine_ = makeLabel(QStringLiteral("uninstallStage"), runBlock_, "secondary");
    run->addWidget(warning_);
    run->addSpacing(16);
    run->addWidget(bar_);
    run->addWidget(stageLine_);

    // Section 9: the restart of the Classic, with Apple's firmware as the success.
    restartBlock_ = new QWidget(this);
    restartBlock_->setObjectName(QStringLiteral("uninstallRestart"));
    auto *restart = new QVBoxLayout(restartBlock_);
    restart->setContentsMargins(0, 0, 0, 0);
    listen_ = makeLabel(QStringLiteral("restartListen"), restartBlock_);
    listen_->setText(wtr("Listen: a low beep then a high one means it worked. The iPod comes "
                         "back with Apple's firmware."));
    countdown_ = makeLabel(QStringLiteral("restartCountdown"), restartBlock_);
    beeps_ = makeLabel(QStringLiteral("restartBeeps"), restartBlock_, "secondary");
    QString table = wtr("What the beeps mean");
    for (const auto &row : InstallPage::beepTable())
        table += QStringLiteral("\n%1: %2").arg(row.first, row.second);
    beeps_->setText(table);
    restart->addWidget(listen_);
    restart->addWidget(countdown_);
    restart->addSpacing(16);
    restart->addWidget(beeps_);

    // Section 1.4: the offers.
    offerBlock_ = new QWidget(this);
    offerBlock_->setObjectName(QStringLiteral("uninstallOffer"));
    auto *offer = new QHBoxLayout(offerBlock_);
    offer->setContentsMargins(0, 0, 0, 0);
    yes_ = makeButton(QStringLiteral("offerYes"), wtr("Remove anyway"), offerBlock_, "main");
    no_ = makeButton(QStringLiteral("offerNo"), wtr("Not now"), offerBlock_, "secondary");
    offer->addWidget(yes_);
    offer->addWidget(no_);
    offer->addStretch();
    connect(yes_, &QPushButton::clicked, this, [this] { answer(true); });
    connect(no_, &QPushButton::clicked, this, [this] { answer(false); });

    stopBlock_ = new QWidget(this);
    stopBlock_->setObjectName(QStringLiteral("uninstallStop"));
    auto *stop = new QHBoxLayout(stopBlock_);
    stop->setContentsMargins(0, 0, 0, 0);
    stopClose_ =
        makeButton(QStringLiteral("uninstallClose"), wtr("Close"), stopBlock_, "secondary");
    stop->addWidget(stopClose_);
    stop->addStretch();
    connect(stopClose_, &QPushButton::clicked, this, &UninstallPage::closeRequested);

    // Section 9, the end: Apple software only.
    finishBlock_ = new QWidget(this);
    finishBlock_->setObjectName(QStringLiteral("uninstallFinish"));
    auto *fin = new QVBoxLayout(finishBlock_);
    fin->setContentsMargins(0, 0, 0, 0);
    backup_ = makeLabel(QStringLiteral("uninstallBackup"), finishBlock_, "secondary");
    folder_ = makeButton(QStringLiteral("openBackupFolder"), wtr("Open folder"), finishBlock_,
                         "secondary");
    auto *backupRow = new QHBoxLayout;
    backupRow->addWidget(backup_, 1);
    backupRow->addWidget(folder_);
    ejectLine_ = makeLabel(QStringLiteral("uninstallEjectNote"), finishBlock_);
    ejectLine_->setText(wtr("Eject the iPod before you unplug it."));
    eject_ = makeButton(QStringLiteral("finishEject"), wtr("Eject"), finishBlock_, "main");
    close_ =
        makeButton(QStringLiteral("uninstallDoneClose"), wtr("Close"), finishBlock_, "secondary");
    auto *ejectRow = new QHBoxLayout;
    ejectRow->addWidget(ejectLine_, 1);
    ejectRow->addWidget(eject_);
    ejectRow->addWidget(close_);
    fin->addLayout(backupRow);
    fin->addSpacing(16);
    fin->addLayout(ejectRow);
    connect(folder_, &QPushButton::clicked, this, [this] {
        if (!folderPath_.isEmpty() && opener_)
            opener_(folderPath_);
    });
    connect(eject_, &QPushButton::clicked, this, &UninstallPage::eject);
    connect(close_, &QPushButton::clicked, this, &UninstallPage::closeRequested);

    layout->addLayout(top);
    layout->addWidget(message_);
    layout->addWidget(firstBlock_);
    layout->addWidget(runBlock_);
    layout->addWidget(restartBlock_);
    layout->addWidget(offerBlock_);
    layout->addWidget(stopBlock_);
    layout->addWidget(finishBlock_);
    layout->addStretch();
    for (QWidget *w : {runBlock_, restartBlock_, offerBlock_, stopBlock_, finishBlock_})
        w->hide();
    showFirst(First::Confirm);

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
            [this](int, const QString &name, const QString &code, const QString &detail) {
                onFailed(name, code, detail);
            });
    connect(runner_, &WizardRunner::flowFinished, this,
            [this](const QJsonObject &summary) { showFinish(summary); });
}

bool UninstallPage::mine(const QString &name) const
{
    if (name == QLatin1String(step::removeBootloader) ||
        name == QLatin1String(step::classicRemove) || name == QLatin1String(step::removeFirmware))
        return true;
    return uninstall_ &&
           (name == QLatin1String(step::verify) || name == QLatin1String(step::finish));
}

qint64 UninstallPage::now() const
{
    return clock_ ? clock_() : monotonicMs();
}

QString UninstallPage::titleText() const
{
    return title_->text();
}

QString UninstallPage::messageText() const
{
    return message_->text();
}

QString UninstallPage::subtitleText() const
{
    return subtitle_->text();
}

QString UninstallPage::folderText() const
{
    return folderLine_->text();
}

QString UninstallPage::countdownText() const
{
    return countdown_->text();
}

QString UninstallPage::stageText() const
{
    return stageLine_->text();
}

QString UninstallPage::backupText() const
{
    return backup_->text();
}

QString UninstallPage::ejectNote() const
{
    return ejectLine_->text();
}

void UninstallPage::setIpod(const QString &model, const QString &name)
{
    model_ = model;
    name_ = name;
    drawing_->setPixmap(modelPixmap(model_, 112, devicePixelRatioF()));
}

void UninstallPage::setBackupFolder(const QString &folder)
{
    folderLine_->setText(wtr("Backup folder: %1").arg(tilde(folder)));
    if (folderPath_.isEmpty() || !finished_)
        folderPath_ = folder;
}

QString UninstallPage::closeRefusal() const
{
    if (!writing_)
        return QString();
    return wtr("TacetOS is writing to the iPod. Wait for the end of this step, then close");
}

void UninstallPage::announce(QWidget *what)
{
    QAccessibleEvent alert(what, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void UninstallPage::showOnly(QWidget *block)
{
    for (QWidget *w :
         {firstBlock_, runBlock_, restartBlock_, offerBlock_, stopBlock_, finishBlock_})
        w->setVisible(w == block);
}

// Section 9: the first screen. It is the page until the flow starts.
void UninstallPage::showFirst(First kind)
{
    first_ = kind;
    started_ = false;
    start_->setEnabled(true);
    change_->setEnabled(true);
    title_->setText(wtr("Back to Apple's software only"));
    message_->hide();
    switch (kind) {
    case First::Confirm:
        firstText_->setText(wtr("TacetOS and its bootloader are removed. Your settings and themes "
                                "are saved on this computer first, and a later install offers to "
                                "put them back. Your music stays on the iPod"));
        start_->setText(wtr("Uninstall"));
        break;
    case First::NothingToDo:
        firstText_->setText(wtr("This iPod has no TacetOS. There is nothing to remove"));
        break;
    case First::RemoveAnyway:
        firstText_->setText(wtr("TacetOS does not seem to be on this iPod. Remove it anyway?"));
        start_->setText(wtr("Remove anyway"));
        break;
    }
    start_->setAccessibleName(start_->text());
    start_->setVisible(kind != First::NothingToDo);
    change_->setVisible(kind != First::NothingToDo);
    folderLine_->setVisible(kind != First::NothingToDo);
    showOnly(firstBlock_);
    if (kind == First::NothingToDo)
        firstClose_->setFocus();
    else
        start_->setFocus();
}

void UninstallPage::onStarted(const QString &name)
{
    if (!mine(name))
        return;
    const bool only = name != QLatin1String(step::verify) && name != QLatin1String(step::finish);
    if (only)
        uninstall_ = true;
    step_ = name;
    started_ = true;
    active_ = true;
    asking_ = false;
    writing_ = false;
    failed_ = false;
    stopped_ = false;
    done_ = false;
    restarting_ = false;
    checking_ = false;
    plain_ = false;
    offer_ = Offer::None;
    restartSince_ = -1;
    tick_.stop();
    message_->hide();
    subtitle_->hide();
    bar_->setRange(0, 1000);
    bar_->setValue(0);
    stageLine_->clear();
    if (name == QLatin1String(step::removeBootloader)) {
        title_->setText(wtr("Removing the bootloader"));
        showOnly(runBlock_);
    } else if (name == QLatin1String(step::removeFirmware)) {
        title_->setText(wtr("Removing the TacetOS files"));
        bar_->setRange(0, 0);
        showOnly(runBlock_);
    } else if (name == QLatin1String(step::verify)) {
        // Section 8: "Checking the result" with a thin indeterminate bar.
        checking_ = true;
        finished_ = false;
        title_->setText(wtr("Checking the result"));
        bar_->setRange(0, 0);
        warning_->hide();
        showOnly(runBlock_);
    } else if (name == QLatin1String(step::finish)) {
        title_->setText(wtr("Back to Apple's software only"));
        showOnly(nullptr);
    } else {
        // The DFU guide shows the steps before the restart: this page waits for it.
        title_->clear();
        showOnly(nullptr);
    }
    if (name != QLatin1String(step::verify))
        warning_->show();
}

void UninstallPage::onWaiting(const QString &name, int kind, const QJsonObject &info)
{
    if (name != step_ || !mine(name) || kind != int(WaitKind::Offer))
        return;
    if (info.value(QStringLiteral("offer")).toString() != QLatin1String("remove-anyway"))
        return;
    // The user already said it on the first screen.
    if (removeAnyway_) {
        UserAnswer reply;
        reply.yes = true;
        runner_->answer(reply);
        return;
    }
    offer_ = Offer::RemoveAnyway;
    title_->setText(wtr("Back to Apple's software only"));
    message_->setProperty("role", QStringLiteral("secondary"));
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    message_->setText(wtr("TacetOS does not seem to be on this iPod. Remove it anyway?"));
    message_->show();
    yes_->setText(wtr("Remove anyway"));
    no_->setText(wtr("Not now"));
    yes_->setAccessibleName(yes_->text());
    no_->setAccessibleName(no_->text());
    no_->show();
    showOnly(offerBlock_);
    asking_ = true;
    yes_->setFocus();
    announce(title_);
}

void UninstallPage::answer(bool yes)
{
    if (!asking_)
        return;
    asking_ = false;
    const Offer which = offer_;
    offer_ = Offer::None;
    message_->hide();
    if (which == Offer::Retry) {
        showOnly(nullptr);
        if (yes) {
            failed_ = false;
            emit retryRequested();
        }
        return;
    }
    UserAnswer reply;
    reply.yes = yes;
    showOnly(nullptr);
    runner_->answer(reply);
}

void UninstallPage::onFirstWrite(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    writing_ = true;
}

void UninstallPage::showRestart()
{
    restarting_ = true;
    restartSince_ = now();
    title_->setText(wtr("The iPod restarts"));
    showOnly(restartBlock_);
    tick_.start();
    refreshCountdown();
    announce(title_);
}

void UninstallPage::onProgress(const QString &name, const QString &stage, qint64 done, qint64 total)
{
    if (name != step_ || !mine(name))
        return;
    if (name == QLatin1String(step::classicRemove)) {
        if (stage == QLatin1String("resume") && !restarting_)
            showRestart();
        return;
    }
    if (name != QLatin1String(step::removeBootloader))
        return;
    stageLine_->setText(stageWords(stage));
    if (total > 0) {
        bar_->setRange(0, 1000);
        bar_->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
    } else {
        bar_->setRange(0, 0);
    }
}

void UninstallPage::refreshCountdown()
{
    if (restartSince_ < 0 || !restarting_ || !active_)
        return;
    const qint64 left = qMax<qint64>(0, restartMs_ - (now() - restartSince_));
    countdown_->setText(
        wtr("Waiting for the iPod: %1 s").arg(QLocale().toString((left + 999) / 1000)));
}

void UninstallPage::onDone(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    writing_ = false;
    restarting_ = false;
    checking_ = false;
    done_ = true;
    bar_->setRange(0, 1000);
    bar_->setValue(1000);
}

void UninstallPage::showFailure(const QString &text, bool plain)
{
    plain_ = plain;
    message_->setProperty("role", plain ? QStringLiteral("secondary") : QStringLiteral("danger"));
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    message_->setText(text);
    message_->show();
    showOnly(stopBlock_);
    announce(message_);
}

// Section 9: "Try again" is offered once for `still-tacetos`, and for the offer of the helper.
void UninstallPage::offerRetry()
{
    offer_ = Offer::Retry;
    yes_->setText(wtr("Try again"));
    yes_->setAccessibleName(yes_->text());
    no_->hide();
    showOnly(offerBlock_);
    asking_ = true;
    yes_->setFocus();
}

void UninstallPage::onFailed(const QString &name, const QString &code, const QString &)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    asking_ = false;
    writing_ = false;
    restarting_ = false;
    checking_ = false;
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
    // Wizard section 10: nothing to remove is not a failure, and nothing was written.
    if (code == error::nothingToDo) {
        stopped_ = true;
        showFailure(wtr("This iPod has no TacetOS. There is nothing to remove"), true);
        return;
    }
    failed_ = true;
    if (code == error::stillTacetos)
        stillFailures_++;
    showFailure(failureText(code, stillFailures_), false);
    const bool never = code == error::ipodNotBack || code == error::dfuFailed;
    if (never)
        return;
    if ((code == error::stillTacetos && stillFailures_ == 1) ||
        next_ == QLatin1String("offer-retry"))
        offerRetry();
}

// Section 9, the end: "Apple software only", the model, where the settings were saved.
void UninstallPage::showFinish(const QJsonObject &summary)
{
    if (!uninstall_)
        return;
    checking_ = false;
    finished_ = true;
    active_ = false;
    const QString folder = summary.value(QStringLiteral("backup")).toString();
    if (!folder.isEmpty())
        folderPath_ = folder;
    title_->setText(wtr("Apple software only"));
    subtitle_->setText(name_);
    subtitle_->setVisible(!name_.isEmpty());
    drawing_->setVisible(true);
    message_->hide();
    const bool hasBackup = !folderPath_.isEmpty();
    backup_->setVisible(hasBackup);
    folder_->setVisible(hasBackup);
    if (hasBackup)
        backup_->setText(wtr("Your settings were saved in %1").arg(tilde(folderPath_)));
    ejected_ = false;
    ejectLine_->setText(wtr("Eject the iPod before you unplug it."));
    eject_->setEnabled(true);
    showOnly(finishBlock_);
    eject_->setFocus();
    announce(title_);
}

// Section 8: Eject runs the eject of the card, then the note says the iPod can be unplugged.
void UninstallPage::eject()
{
    if (ejected_ || !ejecter_)
        return;
    if (!ejecter_()) {
        ejectLine_->setText(wtr("The iPod could not be ejected. Close the programs that use it."));
        return;
    }
    ejected_ = true;
    eject_->setEnabled(false);
    ejectLine_->setText(wtr("You can unplug the iPod"));
}

} // namespace tacetos
