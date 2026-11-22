// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/donepage.h"

#include "flow.h"
#include "runner.h"
#include "wizard/backuppage.h"
#include "wizard/installpage.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace tacetos {

using namespace wizard;

namespace {

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
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

QPushButton *makeButton(const QString &name, const QString &text, QWidget *parent, const char *role)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setAccessibleName(text);
    b->setProperty("role", QString::fromLatin1(role));
    return b;
}

// Bootloader-classic.md section 13.3, in our words (screens section 13.5).
QStringList classicLines()
{
    return {wtr("TacetOS: it starts by itself."),
            wtr("Apple's firmware: turn the iPod off, then turn it on while holding MENU for a "
                "second."),
            wtr("Back to TacetOS from Apple's firmware: hold MENU and the center button until the "
                "screen goes black, then let go at once.")};
}

} // namespace

DonePage::DonePage(WizardRunner *runner, QWidget *parent) : QWidget(parent), runner_(runner)
{
    setObjectName(QStringLiteral("donePage"));
    opener_ = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    auto *top = new QHBoxLayout;
    auto *head = new QVBoxLayout;
    title_ = makeLabel(QStringLiteral("doneTitle"), this);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);
    subtitle_ = makeLabel(QStringLiteral("doneSubtitle"), this);
    head->addWidget(title_);
    head->addWidget(subtitle_);
    drawing_ = new QLabel(this);
    drawing_->setObjectName(QStringLiteral("doneDrawing"));
    drawing_->setAccessibleName(wtr("Drawing of the iPod"));
    top->addLayout(head, 1);
    top->addWidget(drawing_, 0, Qt::AlignTop);
    bar_ = new QProgressBar(this);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(4);
    bar_->setAccessibleName(wtr("Checking the result"));
    failure_ = makeLabel(QStringLiteral("doneFailure"), this, "danger");
    failure_->hide();

    finish_ = new QWidget(this);
    finish_->setObjectName(QStringLiteral("doneFinish"));
    auto *fin = new QVBoxLayout(finish_);
    fin->setContentsMargins(0, 0, 0, 0);
    starting_ = makeLabel(QStringLiteral("doneStarting"), finish_);
    backup_ = makeLabel(QStringLiteral("doneBackup"), finish_, "secondary");
    folder_ =
        makeButton(QStringLiteral("openBackupFolder"), wtr("Open folder"), finish_, "secondary");
    music_ =
        makeButton(QStringLiteral("putMusicBack"), wtr("Put my music back"), finish_, "secondary");
    auto *backupRow = new QHBoxLayout;
    backupRow->addWidget(backup_, 1);
    backupRow->addWidget(folder_);
    ejectLine_ = makeLabel(QStringLiteral("doneEjectNote"), finish_);
    ejectLine_->setText(wtr("Eject the iPod before you unplug it."));
    eject_ = makeButton(QStringLiteral("finishEject"), wtr("Eject"), finish_, "main");
    close_ = makeButton(QStringLiteral("doneClose"), wtr("Close"), finish_, "secondary");
    auto *ejectRow = new QHBoxLayout;
    ejectRow->addWidget(ejectLine_, 1);
    ejectRow->addWidget(eject_);
    ejectRow->addWidget(close_);
    fin->addWidget(starting_);
    fin->addSpacing(16);
    fin->addLayout(backupRow);
    fin->addWidget(music_, 0, Qt::AlignLeft);
    fin->addSpacing(16);
    fin->addLayout(ejectRow);
    finish_->hide();

    layout->addLayout(top);
    layout->addWidget(bar_);
    layout->addWidget(failure_);
    layout->addWidget(finish_);
    layout->addStretch();
    bar_->hide();

    connect(folder_, &QPushButton::clicked, this, [this] {
        if (!folderPath_.isEmpty() && opener_)
            opener_(folderPath_);
    });
    connect(music_, &QPushButton::clicked, this,
            [this] { emit putMusicBackRequested(folderPath_); });
    connect(eject_, &QPushButton::clicked, this, &DonePage::eject);
    connect(close_, &QPushButton::clicked, this, &DonePage::closeRequested);

    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int, const QString &name) { onStarted(name); });
    connect(runner_, &WizardRunner::stepDone, this,
            [this](int, const QString &name) { onDone(name); });
    connect(runner_, &WizardRunner::stepFailed, this,
            [this](int, const QString &name, const QString &code, const QString &detail) {
                onFailed(name, code, detail);
            });
    connect(runner_, &WizardRunner::flowFinished, this,
            [this](const QJsonObject &summary) { showFinish(summary); });
}

void DonePage::setIpod(const QString &model, const QString &name)
{
    model_ = model;
    name_ = name;
    drawing_->setPixmap(modelPixmap(model, 112, devicePixelRatioF()));
}

void DonePage::setBackup(const QString &folder, qint64 files, qint64 bytes)
{
    folderPath_ = folder;
    files_ = files;
    bytes_ = bytes;
}

QString DonePage::titleText() const
{
    return title_->text();
}

QString DonePage::subtitleText() const
{
    return subtitle_->text();
}

QString DonePage::failureText() const
{
    return failure_->text();
}

QString DonePage::startingText() const
{
    return starting_->text();
}

QString DonePage::backupText() const
{
    return backup_->text();
}

QString DonePage::ejectNote() const
{
    return ejectLine_->text();
}

void DonePage::onStarted(const QString &name)
{
    if (name != QLatin1String(step::verify) && name != QLatin1String(step::finish))
        return;
    failed_ = false;
    failure_->hide();
    if (name == QLatin1String(step::verify)) {
        // Section 8: "Checking the result" with a thin indeterminate bar.
        checking_ = true;
        finished_ = false;
        title_->setText(wtr("Checking the result"));
        subtitle_->clear();
        bar_->show();
        finish_->hide();
    }
}

void DonePage::onDone(const QString &name)
{
    if (name == QLatin1String(step::verify)) {
        checking_ = false;
        bar_->hide();
    }
}

void DonePage::onFailed(const QString &name, const QString &code, const QString &detail)
{
    if (name != QLatin1String(step::verify) && name != QLatin1String(step::finish))
        return;
    checking_ = false;
    bar_->hide();
    if (code == error::cancelled)
        return;
    failed_ = true;
    // `verify-failed`: what differs, from the detail of the step.
    failure_->setText(code == error::verifyFailed
                          ? wtr("The iPod is not in the state it should be: %1").arg(detail)
                          : wtr("The result could not be checked."));
    failure_->show();
    QAccessibleEvent alert(failure_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

// Section 8: the end page of an install.
void DonePage::showFinish(const QJsonObject &summary)
{
    checking_ = false;
    finished_ = true;
    bar_->hide();
    failure_->hide();
    QString version = summary.value(QStringLiteral("version")).toString();
    if (version.isEmpty())
        version = version_;
    const QString folder = summary.value(QStringLiteral("backup")).toString();
    if (!folder.isEmpty())
        folderPath_ = folder;
    title_->setText(wtr("TacetOS is installed"));
    QString what = version.isEmpty() ? wtr("TacetOS") : wtr("TacetOS %1").arg(version);
    subtitle_->setText(name_.isEmpty() ? what + QLatin1Char('.')
                                       : wtr("%1 on %2.").arg(what, name_));
    const QStringList lines = classic_ ? classicLines() : lines_;
    if (lines.isEmpty()) {
        starting_->hide();
    } else {
        QString text = wtr("Starting each firmware");
        for (const QString &line : lines)
            text += QStringLiteral("\n· ") + line;
        starting_->setText(text);
        starting_->show();
    }
    const bool hasBackup = !folderPath_.isEmpty();
    backup_->setVisible(hasBackup);
    folder_->setVisible(hasBackup);
    if (hasBackup)
        backup_->setText(files_ == 0
                             ? wtr("The iPod was empty: nothing to back up")
                             : wtr("Backup checked: %1 files, %2")
                                   .arg(QLocale().toString(files_), backupSizeText(bytes_)));
    music_->setVisible(clean_ && hasBackup);
    ejected_ = false;
    ejectLine_->setText(wtr("Eject the iPod before you unplug it."));
    eject_->setEnabled(true);
    finish_->show();
    eject_->setFocus();
    QAccessibleEvent alert(title_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

// Section 8: Eject runs the eject of the card, then the note says the iPod can be unplugged.
void DonePage::eject()
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
