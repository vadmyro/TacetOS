// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/diagnosepage.h"

#include "runner.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

namespace tacetos {

using namespace wizard;

namespace {

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
}

// Section 13.3 of the screens spec: the name of a check.
QString nameOf(const QString &id)
{
    struct Entry
    {
        const char *id;
        const char *name;
    };
    static const Entry names[] = {
        {"system", QT_TRANSLATE_NOOP("wizard", "System")},
        {"helper", QT_TRANSLATE_NOOP("wizard", "Helper")},
        {"writes", QT_TRANSLATE_NOOP("wizard", "Writes")},
        {"udisks", QT_TRANSLATE_NOOP("wizard", "Disk service")},
        {"classic-build", QT_TRANSLATE_NOOP("wizard", "Classic support")},
        {"udev-rule", QT_TRANSLATE_NOOP("wizard", "USB permission")},
        {"backup-folder", QT_TRANSLATE_NOOP("wizard", "Backup folder")},
        {"backup-space", QT_TRANSLATE_NOOP("wizard", "Space for the backup")},
        {"model", QT_TRANSLATE_NOOP("wizard", "Model")},
        {"serial", QT_TRANSLATE_NOOP("wizard", "Serial number")},
        {"single-ipod", QT_TRANSLATE_NOOP("wizard", "One iPod")},
        {"volume", QT_TRANSLATE_NOOP("wizard", "Music volume")},
        {"format", QT_TRANSLATE_NOOP("wizard", "Format")},
        {"apple-firmware", QT_TRANSLATE_NOOP("wizard", "Apple's firmware")},
        {"bootloader-state", QT_TRANSLATE_NOOP("wizard", "Firmware partition")},
        {"package", QT_TRANSLATE_NOOP("wizard", "Package")},
        {"firmware-room", QT_TRANSLATE_NOOP("wizard", "Space on the iPod")},
        {"battery", QT_TRANSLATE_NOOP("wizard", "Battery")},
    };
    for (const Entry &entry : names) {
        if (id == QLatin1String(entry.id))
            return QCoreApplication::translate("wizard", entry.name);
    }
    return id;
}

// The sign and the word of the result, and the role of the message (the colours of the
// palette: nothing is written here, only a role of the style sheet).
struct Look
{
    QChar sign;
    const char *result;
    const char *messageRole;
};

Look lookOf(CheckResult result)
{
    switch (result) {
    case CheckResult::Pass:
        return {QChar(0x2713), "pass", "secondary"};
    case CheckResult::Warn:
        return {QLatin1Char('!'), "warn", ""};
    case CheckResult::Fail:
        return {QChar(0x2715), "fail", "danger"};
    case CheckResult::Ask:
        return {QLatin1Char('?'), "ask", ""};
    case CheckResult::Skipped:
        break;
    }
    return {QLatin1Char('-'), "skipped", "secondary"};
}

QString errorText(const QString &code)
{
    if (code == QLatin1String("cancelled"))
        return wtr("The permission was not given. Nothing was changed.");
    if (code == QLatin1String("permission-denied"))
        return wtr("The permission was refused. Nothing was changed.");
    if (code == QLatin1String("unsupported-system"))
        return wtr("This system cannot ask for the permission here. Type the commands below "
                   "in a terminal.");
    return wtr("The rule could not be put in place. Type the commands below in a terminal.");
}

// Helper section 11.5: the two commands for those who prefer a terminal. The rule is the one
// of the Classic in DFU mode, and of nothing else.
QString udevCommands()
{
    return QStringLiteral("echo 'SUBSYSTEM==\"usb\", ATTR{idVendor}==\"05ac\", "
                          "ATTR{idProduct}==\"1223\", TAG+=\"uaccess\"' | sudo tee %1\n"
                          "sudo udevadm control --reload-rules")
        .arg(QString(kUdevRuleFile));
}

QPushButton *makeButton(const QString &text, const char *role, const QString &name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setProperty("role", QString::fromLatin1(role));
    b->setAccessibleName(text);
    return b;
}

} // namespace

DiagnosePage::DiagnosePage(WizardRunner *runner, const QString &serial, const QString &model,
                           Family family, QWidget *parent)
    : QWidget(parent), runner_(runner), serial_(serial), model_(model), family_(family)
{
    setObjectName(QStringLiteral("diagnosePage"));
    pause_.setSingleShot(true);
    pause_.setInterval(1000);
    connect(&pause_, &QTimer::timeout, this, &DiagnosePage::readyToGoOn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    title_ = new QLabel(wtr("Checking this computer and the iPod"), this);
    title_->setObjectName(QStringLiteral("diagnoseTitle"));
    title_->setProperty("role", QStringLiteral("title"));
    title_->setWordWrap(true);
    list_ = new QWidget(this);
    list_->setObjectName(QStringLiteral("diagnoseList"));
    new QVBoxLayout(list_);
    list_->layout()->setContentsMargins(0, 0, 0, 0);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("diagnoseNote"));
    note_->setWordWrap(true);
    again_button_ = makeButton(wtr("Check again"), "secondary", QStringLiteral("checkAgain"), this);
    again_button_->setEnabled(false);
    connect(again_button_, &QPushButton::clicked, this, &DiagnosePage::checkAgain);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(note_, 1);
    bottom->addWidget(again_button_, 0, Qt::AlignRight);
    layout->addWidget(title_);
    layout->addWidget(list_);
    layout->addLayout(bottom);
    layout->addStretch();

    connect(runner_, &WizardRunner::checksReported, this, &DiagnosePage::setChecks);
    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int, const QString &name) { onStarted(name); });
    connect(
        runner_, &WizardRunner::stepWaiting, this,
        [this](const QString &name, WaitKind kind, const QJsonObject &) { onWaiting(name, kind); });
    connect(runner_, &WizardRunner::stepFailed, this,
            [this](int, const QString &name, const QString &code, const QString &) {
                onFailed(name, code);
            });
    connect(runner_, &WizardRunner::stepDone, this,
            [this](int, const QString &name) { onDone(name); });
    connect(runner_, &WizardRunner::flowStopped, this, [this](const QString &) { onStopped(); });
}

bool DiagnosePage::canGoOn() const
{
    return !checks_.isEmpty() && wizard::canGoOn(checks_);
}

void DiagnosePage::setChecks(const QList<Check> &checks)
{
    checks_ = checks;
    rebuild();
    refresh();
}

void DiagnosePage::rebuild()
{
    auto *outer = qobject_cast<QVBoxLayout *>(list_->layout());
    while (QLayoutItem *item = outer->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (const Side side : {Side::Computer, Side::Ipod}) {
        auto *group = new QWidget(list_);
        auto *box = new QVBoxLayout(group);
        box->setContentsMargins(0, 0, 0, 0);
        auto *heading =
            new QLabel(side == Side::Computer ? wtr("THIS COMPUTER") : wtr("THIS IPOD"), group);
        heading->setObjectName(side == Side::Computer ? QStringLiteral("groupComputer")
                                                      : QStringLiteral("groupIpod"));
        heading->setProperty("role", QStringLiteral("secondary"));
        box->addWidget(heading);
        bool any = false;
        for (const Check &check : std::as_const(checks_)) {
            if (check.side != side)
                continue;
            addRow(box, check);
            any = true;
        }
        if (!any) {
            auto *none = new QLabel(wtr("Checked once the computer is ready"), group);
            none->setProperty("role", QStringLiteral("secondary"));
            box->addWidget(none);
        }
        outer->addWidget(group);
        outer->addSpacing(24);
    }
}

void DiagnosePage::addRow(QVBoxLayout *group, const Check &check)
{
    const Look look = lookOf(check.result);
    auto *row = new QFrame(group->parentWidget());
    row->setObjectName(QStringLiteral("check-") + check.id);
    row->setProperty("result", QString::fromLatin1(look.result));
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 2, 0, 2);

    auto *sign = new QLabel(QString(look.sign), row);
    sign->setObjectName(QStringLiteral("sign-") + check.id);
    sign->setFixedWidth(16);
    sign->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    // The accent is a colour of the palette, never written here. It is for the tick only.
    if (check.result == CheckResult::Pass)
        sign->setStyleSheet(QStringLiteral("QLabel { color: palette(highlight); }"));
    else if (check.result == CheckResult::Fail)
        sign->setProperty("role", QStringLiteral("danger"));
    else if (check.result == CheckResult::Skipped || check.result == CheckResult::Warn)
        sign->setProperty("role", QStringLiteral("secondary"));
    sign->setAccessibleName(QString::fromLatin1(look.result));

    auto *name = new QLabel(nameOf(check.id), row);
    name->setObjectName(QStringLiteral("name-") + check.id);
    name->setFixedWidth(160);
    QFont bold = name->font();
    bold.setWeight(QFont::DemiBold);
    name->setFont(bold);
    name->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *message = new QLabel(check.message, row);
    message->setObjectName(QStringLiteral("message-") + check.id);
    message->setWordWrap(true);
    if (*look.messageRole)
        message->setProperty("role", QString::fromLatin1(look.messageRole));

    line->addWidget(sign);
    line->addWidget(name);
    line->addWidget(message, 1);

    if (check.id == QLatin1String("battery") && check.result == CheckResult::Ask) {
        auto *yes = makeButton(wtr("Yes, it is"), "primary", QStringLiteral("batteryYes"), row);
        yes->setEnabled(asking_);
        connect(yes, &QPushButton::clicked, this, [this] {
            asking_ = false;
            runner_->answer(UserAnswer{false, true, QString()});
            refresh();
        });
        line->addWidget(yes, 0, Qt::AlignTop);
    }
    if (check.action == QLatin1String("install-udev-rule")) {
        auto *install =
            makeButton(installing_ ? wtr("Waiting for your permission") : wtr("Install the rule"),
                       "secondary", QStringLiteral("action-") + check.id, row);
        install->setEnabled(!installing_ && bool(installer_));
        connect(install, &QPushButton::clicked, this, &DiagnosePage::installRule);
        line->addWidget(install, 0, Qt::AlignTop);
    }
    group->addWidget(row);

    if (check.action == QLatin1String("install-udev-rule"))
        addCommand(group, check.id, udevCommands());
    else if (!check.command.isEmpty() && check.result != CheckResult::Pass)
        addCommand(group, check.id, check.command);
}

void DiagnosePage::addCommand(QVBoxLayout *group, const QString &id, const QString &command)
{
    auto *box = new QWidget(group->parentWidget());
    auto *line = new QHBoxLayout(box);
    line->setContentsMargins(16 + 160 + 12, 0, 0, 6);
    // A field that can be selected and not changed: the app never runs the command.
    auto *field = new QPlainTextEdit(command, box);
    field->setObjectName(QStringLiteral("command-") + id);
    field->setReadOnly(true);
    field->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    field->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    field->setAccessibleName(wtr("Command to type"));
    const int lines = command.count(QLatin1Char('\n')) + 1;
    field->setFixedHeight(field->fontMetrics().lineSpacing() * (lines + 1) + 12);
    auto *copyButton = makeButton(wtr("Copy"), "secondary", QStringLiteral("copy-") + id, box);
    connect(copyButton, &QPushButton::clicked, this, [this, id, command] { copy(id, command); });
    line->addWidget(field, 1);
    line->addWidget(copyButton, 0, Qt::AlignTop);
    group->addWidget(box);
}

void DiagnosePage::copy(const QString &id, const QString &text)
{
    QApplication::clipboard()->setText(text);
    auto *b = findChild<QPushButton *>(QStringLiteral("copy-") + id);
    if (!b)
        return;
    const QString was = wtr("Copy");
    b->setText(wtr("Copied"));
    b->setAccessibleName(wtr("Copied"));
    QPointer<QPushButton> guard(b);
    QTimer::singleShot(2000, b, [guard, was] {
        if (guard) {
            guard->setText(was);
            guard->setAccessibleName(was);
        }
    });
}

void DiagnosePage::onStarted(const QString &name)
{
    if (name != step::diagnose)
        return;
    pause_.stop();
    ready_ = false;
    failed_ = false;
    stopped_ = false;
    asking_ = false;
    note_->clear();
    refresh();
}

void DiagnosePage::onWaiting(const QString &name, WaitKind kind)
{
    if (name != step::diagnose || kind != WaitKind::Battery)
        return;
    asking_ = true;
    rebuild();
    refresh();
}

void DiagnosePage::onFailed(const QString &name, const QString &code)
{
    if (name != step::diagnose || code != QString(error::checkBlocking))
        return;
    failed_ = true;
    refresh();
}

void DiagnosePage::onStopped()
{
    stopped_ = true;
    if (again_ && failed_) {
        again_ = false;
        resumeStep();
    }
    refresh();
}

void DiagnosePage::onDone(const QString &name)
{
    if (name != step::diagnose)
        return;
    ready_ = true;
    note_->setText(wtr("Everything is ready"));
    note_->setAccessibleName(note_->text());
    pause_.start();
    refresh();
}

void DiagnosePage::checkAgain()
{
    note_->clear();
    if (asking_) {
        // The step waits for the answer: it gets a no, fails, and runs again at once.
        again_ = true;
        asking_ = false;
        runner_->answer(UserAnswer{false, false, QString()});
    } else if (stopped_ && failed_) {
        resumeStep();
    }
    refresh();
}

void DiagnosePage::resumeStep()
{
    const QString code = runner_->resume(serial_, model_, family_);
    if (!code.isEmpty()) {
        note_->setText(wtr("The checks could not run again (%1).").arg(code));
        return;
    }
    stopped_ = false;
}

void DiagnosePage::installRule()
{
    if (installing_ || !installer_)
        return;
    installing_ = true;
    note_->clear();
    rebuild();
    refresh();

    auto result = std::make_shared<QString>();
    QThread *thread = QThread::create([installer = installer_, result] { *result = installer(); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result] {
        installing_ = false;
        if (result->isEmpty()) {
            // Section 3: the checks run again by themselves.
            note_->clear();
            rebuild();
            if (stopped_ && failed_)
                resumeStep();
            else
                again_ = true;
        } else {
            note_->setText(errorText(*result));
            rebuild();
        }
        refresh();
    });
    thread->start();
}

void DiagnosePage::refresh()
{
    again_button_->setEnabled(!installing_ && ((stopped_ && failed_) || asking_));
    const bool on = canGoOn();
    if (on != lastCanGoOn_) {
        lastCanGoOn_ = on;
        emit canGoOnChanged(on);
    }
}

} // namespace tacetos
