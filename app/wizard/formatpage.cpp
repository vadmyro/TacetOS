// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/formatpage.h"

#include "flow.h"
#include "runner.h"
#include "wizard/backuppage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

// The rule of the typed name (bootloader-legacy.md section 22), from the installer, whose
// header has the same name as the one of the wizard.
namespace tacetos::installer {
bool confirmationMatches(const QString &shown, const QString &typed);
}

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

// Screens section 13.2: the stage of the helper in words. A stage the table does not know
// shows no text, never its code.
QString stageText(const QString &stage)
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

// Wizard section 13: the message of a code of these steps.
QString failureText(const QString &code)
{
    if (code == error::backupNotVerified)
        return wtr("No verified backup of this iPod. Run the backup again.");
    if (code == error::eraseFailed)
        return wtr("Some files of the iPod could not be erased. Unplug it, plug it in again, then "
                   "press Resume.");
    if (code == error::volumeNotBack)
        return wtr("The iPod was formatted, but its volume did not come back. Unplug it, plug it "
                   "in again, press Resume.");
    if (code == error::ipodGone)
        return wtr("The iPod was unplugged. Plug it in again and press Resume.");
    if (code == QLatin1String("in-use"))
        return wtr("Another program is using the iPod. Close it, then try again.");
    if (code == QLatin1String("permission-denied"))
        return wtr("TacetOS was not given the permission to write to the iPod.");
    return wtr("The iPod could not be erased.");
}

QString filesText(qint64 files)
{
    if (files == 1)
        return wtr("1 file");
    return wtr("%1 files").arg(QLocale().toString(files));
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

} // namespace

FormatPage::FormatPage(WizardRunner *runner, QWidget *parent)
    : QWidget(parent), runner_(runner), volumeReturnMs_(kVolumeReturnMs)
{
    setObjectName(QStringLiteral("formatPage"));
    clock_ = monotonicMs;
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, &FormatPage::refreshCountdown);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    banner_ = makeLabel(QStringLiteral("formatBanner"), this, "danger");
    banner_->hide();
    title_ = makeLabel(QStringLiteral("formatTitle"), this, "danger");
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);

    // Screens section 5.1: the confirmation.
    confirmation_ = new QWidget(this);
    confirmation_->setObjectName(QStringLiteral("formatConfirmation"));
    auto *ask = new QVBoxLayout(confirmation_);
    ask->setContentsMargins(0, 0, 0, 0);
    summary_ = makeLabel(QStringLiteral("formatSummary"), confirmation_);
    folderLine_ = makeLabel(QStringLiteral("formatBackupFolder"), confirmation_, "secondary");
    folderLine_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderLine_->hide();
    convertNote_ = makeLabel(QStringLiteral("formatConvertNote"), confirmation_);
    convertNote_->setText(
        wtr("This iPod is formatted for Mac. It becomes a Windows iPod, which TacetOS needs."));
    convertNote_->hide();
    prompt_ = makeLabel(QStringLiteral("typedNamePrompt"), confirmation_);
    prompt_->setText(wtr("To confirm, type the name of the iPod:"));
    expectedLabel_ = makeLabel(QStringLiteral("typedNameExpected"), confirmation_);
    expectedLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont semibold = expectedLabel_->font();
    semibold.setWeight(QFont::DemiBold);
    expectedLabel_->setFont(semibold);
    field_ = new QLineEdit(confirmation_);
    field_->setObjectName(QStringLiteral("typedNameField"));
    field_->setAccessibleName(wtr("Name of the iPod"));
    confirm_ = new QPushButton(wtr("Erase the iPod"), confirmation_);
    confirm_->setObjectName(QStringLiteral("typedNameConfirm"));
    confirm_->setProperty("role", QStringLiteral("danger"));
    confirm_->setAccessibleName(wtr("Erase the iPod"));
    confirm_->setEnabled(false);
    ask->addWidget(summary_);
    ask->addWidget(folderLine_);
    ask->addWidget(convertNote_);
    ask->addSpacing(16);
    ask->addWidget(prompt_);
    ask->addWidget(expectedLabel_);
    ask->addWidget(field_);
    ask->addSpacing(16);
    ask->addWidget(confirm_, 0, Qt::AlignRight);
    connect(field_, &QLineEdit::textChanged, this, [this](const QString &text) {
        confirm_->setEnabled(asking_ && installer::confirmationMatches(expected_, text));
    });
    // Enter presses the button when it is on.
    connect(field_, &QLineEdit::returnPressed, this, [this] {
        if (confirm_->isEnabled())
            onTyped();
    });
    connect(confirm_, &QPushButton::clicked, this, &FormatPage::onTyped);

    // Screens section 5.2: the progress.
    progress_ = new QWidget(this);
    progress_->setObjectName(QStringLiteral("formatProgress"));
    auto *run = new QVBoxLayout(progress_);
    run->setContentsMargins(0, 0, 0, 0);
    warning_ = makeLabel(QStringLiteral("formatWarning"), progress_);
    warning_->setText(wtr("Do not unplug the iPod"));
    QFont warn = warning_->font();
    warn.setWeight(QFont::DemiBold);
    warning_->setFont(warn);
    bar_ = new QProgressBar(progress_);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Erase progress"));
    barText_ = makeLabel(QStringLiteral("stepProgressText"), progress_);
    stageLine_ = makeLabel(QStringLiteral("formatStage"), progress_, "secondary");
    note_ = makeLabel(QStringLiteral("formatNote"), progress_);
    run->addWidget(warning_);
    run->addSpacing(16);
    run->addWidget(bar_);
    run->addWidget(barText_);
    run->addWidget(stageLine_);
    run->addWidget(note_);

    layout->addWidget(banner_);
    layout->addWidget(title_);
    layout->addWidget(confirmation_);
    layout->addWidget(progress_);
    layout->addStretch();
    confirmation_->hide();
    progress_->hide();

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

bool FormatPage::mine(const QString &name) const
{
    return name == QLatin1String(step::format) || name == QLatin1String(step::convert);
}

qint64 FormatPage::now() const
{
    return clock_ ? clock_() : monotonicMs();
}

void FormatPage::setBackupFolder(const QString &folder)
{
    folder_ = folder;
    folderLine_->setText(QDir::toNativeSeparators(folder));
    folderLine_->setVisible(!folder.isEmpty());
}

QString FormatPage::closeRefusal() const
{
    if (!writing_)
        return QString();
    return wtr("TacetOS is writing to the iPod. Wait for the end of this step, then close");
}

void FormatPage::closeEvent(QCloseEvent *event)
{
    if (writing_)
        event->ignore();
    else
        QWidget::closeEvent(event);
}

void FormatPage::showConfirmation(bool on)
{
    confirmation_->setVisible(on);
    progress_->setVisible(!on);
}

void FormatPage::onStarted(const QString &name)
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
    classic_ = false;
    volumeSince_ = -1;
    tick_.stop();
    banner_->hide();
    bar_->setValue(0);
    barText_->clear();
    stageLine_->clear();
    note_->clear();
    title_->setText(name == QLatin1String(step::convert) ? wtr("Converting the iPod")
                                                         : wtr("Erasing the iPod"));
    showConfirmation(false);
}

// Screens section 5.1.
void FormatPage::onWaiting(const QString &name, int kind, const QJsonObject &info)
{
    if (name != step_ || !mine(name) || kind != int(WaitKind::TypedName))
        return;
    expected_ = info.value(QStringLiteral("ipodName")).toString();
    const qint64 files = qint64(info.value(QStringLiteral("files")).toDouble());
    const qint64 bytes = qint64(info.value(QStringLiteral("bytes")).toDouble());
    title_->setText(wtr("All music on this iPod will be erased"));
    summary_->setText(wtr("%1, %2. They are in the backup checked a moment ago:")
                          .arg(filesText(files), backupSizeText(bytes)));
    expectedLabel_->setText(expected_);
    convertNote_->setVisible(name == QLatin1String(step::convert));
    field_->setEnabled(true);
    field_->clear();
    confirm_->setEnabled(false);
    asking_ = true;
    showConfirmation(true);
    field_->setFocus();
    QAccessibleEvent alert(title_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void FormatPage::onTyped()
{
    if (!asking_ || !installer::confirmationMatches(expected_, field_->text()))
        return;
    asking_ = false;
    UserAnswer answer;
    answer.text = field_->text();
    lock();
    title_->setText(step_ == QLatin1String(step::convert) ? wtr("Converting the iPod")
                                                          : wtr("Erasing the iPod"));
    showConfirmation(false);
    runner_->answer(answer);
}

// Nothing on the confirmation can be clicked once the answer is sent.
void FormatPage::lock()
{
    field_->setEnabled(false);
    confirm_->setEnabled(false);
}

void FormatPage::onFirstWrite(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    writing_ = true;
    lock();
}

void FormatPage::onProgress(const QString &name, const QString &stage, qint64 done, qint64 total)
{
    if (name != step_ || !mine(name))
        return;
    if (stage == QLatin1String("volume")) {
        // Wizard section 6.3 step 3: the volume comes back within `VOLUME_RETURN`.
        bar_->setValue(1000);
        barText_->clear();
        if (volumeSince_ < 0)
            volumeSince_ = now();
        tick_.start();
        refreshCountdown();
        return;
    }
    if (stage == QLatin1String("erase")) {
        // Wizard section 6.4: an iPod Classic is emptied by files.
        classic_ = true;
        stageLine_->setText(wtr("Erasing the files"));
        barText_->setText(wtr("%1 of %2 entries deleted")
                              .arg(QLocale().toString(done), QLocale().toString(total)));
    } else {
        stageLine_->setText(stageText(stage));
        barText_->clear();
    }
    if (total > 0)
        bar_->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
}

void FormatPage::refreshCountdown()
{
    if (volumeSince_ < 0 || !active_)
        return;
    const qint64 left = qMax<qint64>(0, volumeReturnMs_ - (now() - volumeSince_));
    stageLine_->setText(wtr("Waiting for the iPod to come back: %1 s")
                            .arg(QLocale().toString((left + 999) / 1000)));
}

void FormatPage::onDone(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    writing_ = false;
    done_ = true;
    bar_->setValue(1000);
    stageLine_->clear();
    barText_->clear();
}

void FormatPage::onFailed(const QString &name, const QString &code)
{
    if (name != step_ || !mine(name))
        return;
    tick_.stop();
    active_ = false;
    asking_ = false;
    writing_ = false;
    if (code == error::cancelled) {
        stopped_ = true;
        showConfirmation(false);
        note_->setText(wtr("Stopped. Resume from the card when you want."));
        return;
    }
    failed_ = true;
    banner_->setText(failureText(code));
    banner_->show();
    stageLine_->clear();
    QAccessibleEvent alert(banner_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

} // namespace tacetos
