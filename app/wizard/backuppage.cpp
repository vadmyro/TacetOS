// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/backuppage.h"

#include "flow.h"
#include "runner.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
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

// Screens section 4.2: at most 20 names, then "and 37 more".
constexpr int kMostNames = 20;

qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

// Screens section 13.2: what each backup step copies.
QString whatText(const QString &step)
{
    if (step == QLatin1String(step::backupFull))
        return wtr("Copying the music. Each file is read back and checked");
    if (step == QLatin1String(step::backupKeep))
        return wtr("Copying your settings, themes and playlists");
    return wtr("Saving your settings and themes");
}

// Wizard section 13 and screens section 4.2: the message of a code of the backup.
QString failureText(const QString &code)
{
    if (code == error::backupFullDisk)
        return wtr("The backup folder is full. Choose another folder.");
    if (code == error::backupReadFailed)
        return wtr("Some files of the iPod could not be read.");
    if (code == error::backupVerifyFailed)
        return wtr("The copy on this computer does not match the iPod. Check the disk of this "
                   "computer, then try again.");
    if (code == QLatin1String("source-changed"))
        return wtr("Files changed on the iPod during the backup. Close the programs using the "
                   "iPod.");
    if (code == error::backupNameRefused)
        return wtr("Some names of the iPod cannot be used on this computer.");
    return wtr("The backup could not be finished.");
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

QString backupSizeText(qint64 bytes)
{
    const QLocale locale;
    if (bytes >= 1000LL * 1000 * 1000)
        return wtr("%1 GB").arg(locale.toString(double(bytes) / 1e9, 'f', 1));
    if (bytes >= 1000LL * 1000)
        return wtr("%1 MB").arg(locale.toString(double(bytes) / 1e6, 'f', 1));
    return wtr("%1 kB").arg(locale.toString(double(bytes) / 1e3, 'f', 1));
}

QString backupTimeLeftText(qint64 seconds)
{
    if (seconds < 60)
        return wtr("less than a minute left");
    const qint64 minutes = (seconds + 59) / 60;
    if (minutes < 60)
        return wtr("about %1 min left").arg(minutes);
    return wtr("about %1 h %2 min left")
        .arg(minutes / 60)
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

BackupPage::BackupPage(WizardRunner *runner, QWidget *parent) : QWidget(parent), runner_(runner)
{
    setObjectName(QStringLiteral("backupPage"));
    clock_ = monotonicMs;
    opener_ = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };
    pause_.setSingleShot(true);
    pause_.setInterval(1000);
    connect(&pause_, &QTimer::timeout, this, &BackupPage::readyToGoOn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    banner_ = makeLabel(QStringLiteral("backupBanner"), this, "danger");
    banner_->hide();
    title_ = makeLabel(QStringLiteral("backupTitle"), this, "title");
    title_->setText(wtr("Backing up your iPod"));
    what_ = makeLabel(QStringLiteral("backupWhat"), this, "secondary");

    bar_ = new QProgressBar(this);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Backup progress"));
    barText_ = makeLabel(QStringLiteral("stepProgressText"), this);
    fileLine_ = makeLabel(QStringLiteral("stepCurrentFile"), this, "secondary");
    fileLine_->setWordWrap(false);
    fileLine_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    note_ = makeLabel(QStringLiteral("backupNote"), this);

    namesTitle_ = makeLabel(QStringLiteral("backupNamesTitle"), this, "secondary");
    namesTitle_->hide();
    namesList_ = new QPlainTextEdit(this);
    namesList_->setObjectName(QStringLiteral("backupNames"));
    namesList_->setReadOnly(true);
    namesList_->setAccessibleName(wtr("Files concerned"));
    namesList_->hide();

    folderLine_ = makeLabel(QStringLiteral("backupFolderLine"), this, "secondary");
    folderLine_->hide();
    open_ = new QPushButton(wtr("Open folder"), this);
    open_->setObjectName(QStringLiteral("openBackupFolder"));
    open_->setProperty("role", QStringLiteral("secondary"));
    open_->setAccessibleName(wtr("Open folder"));
    open_->hide();
    connect(open_, &QPushButton::clicked, this, [this] {
        if (!folder_.isEmpty() && opener_)
            opener_(folder_);
    });
    last_ = makeLabel(QStringLiteral("backupNothingWritten"), this, "secondary");
    last_->hide();

    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folderLine_, 1);
    folderRow->addWidget(open_, 0, Qt::AlignRight);

    layout->addWidget(banner_);
    layout->addWidget(title_);
    layout->addWidget(what_);
    layout->addSpacing(16);
    layout->addWidget(bar_);
    layout->addWidget(barText_);
    layout->addWidget(fileLine_);
    layout->addWidget(note_);
    layout->addWidget(namesTitle_);
    layout->addWidget(namesList_);
    layout->addSpacing(16);
    layout->addLayout(folderRow);
    layout->addWidget(last_);
    layout->addStretch();

    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int, const QString &name) { onStarted(name); });
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

bool BackupPage::mine(const QString &name) const
{
    return name == QLatin1String(step::backupFull) || name == QLatin1String(step::backupKeep) ||
           name == QLatin1String(step::backupSettings);
}

qint64 BackupPage::now() const
{
    return clock_ ? clock_() : monotonicMs();
}

void BackupPage::setBackupFolder(const QString &folder)
{
    folder_ = folder;
    folderLine_->setText(wtr("Saved to %1").arg(QDir::toNativeSeparators(folder)));
    folderLine_->setVisible(!folder.isEmpty());
    open_->setVisible(!folder.isEmpty());
}

void BackupPage::setCurrentFile(const QString &path)
{
    file_ = path;
    elideFile();
}

void BackupPage::setFileNames(const QStringList &names)
{
    names_ = names;
    if (failed_)
        showNames();
}

void BackupPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    elideFile();
}

// Screens section 4.1: the path is cut in the middle with "..." when it is too long.
void BackupPage::elideFile()
{
    const QFontMetrics metrics(fileLine_->font());
    QString text = metrics.elidedText(file_, Qt::ElideMiddle, qMax(100, fileLine_->width()));
    text.replace(QChar(0x2026), QStringLiteral("..."));
    fileLine_->setText(text);
    fileLine_->setToolTip(file_);
}

void BackupPage::onStarted(const QString &name)
{
    if (!mine(name))
        return;
    step_ = name;
    active_ = true;
    failed_ = false;
    stopped_ = false;
    checkedLine_.clear();
    pause_.stop();
    timeLeft_.clear();
    started_ = -1;
    total_ = 0;
    names_.clear();
    file_.clear();
    banner_->hide();
    namesTitle_->hide();
    namesList_->hide();
    last_->hide();
    title_->setText(wtr("Backing up your iPod"));
    what_->setText(whatText(name));
    bar_->setValue(0);
    barText_->clear();
    fileLine_->show();
    elideFile();
    note_->clear();
}

void BackupPage::onProgress(const QString &name, const QString &stage, qint64 done, qint64 total)
{
    if (!mine(name))
        return;
    // Screens section 4.1: the stage of the firmware partition replaces the file line.
    if (stage == QLatin1String("firmware-partition")) {
        fileLine_->setText(wtr("Copying the firmware partition"));
        fileLine_->setToolTip(QString());
        return;
    }
    if (stage != QLatin1String("copy"))
        return;
    elideFile();
    total_ = total;
    const qint64 at = now();
    if (started_ < 0)
        started_ = at;
    timeLeft_.add(at, done);
    bar_->setValue(total > 0 ? int(qBound(qint64(0), done * 1000 / total, qint64(1000))) : 0);

    QString text = wtr("%1 of %2").arg(backupSizeText(done), backupSizeText(total));
    // The first 10 s only tell the speed of the caches.
    const qint64 left = timeLeft_.secondsLeft(total);
    if (at - started_ < kSpeedWindowMs || left < 0)
        text += QStringLiteral(" · ") + wtr("Computing the time left");
    else
        text += QStringLiteral(" · ") + backupTimeLeftText(left);
    barText_->setText(text);
}

void BackupPage::onDone(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    active_ = false;
    bar_->setValue(1000);
    fileLine_->clear();
    barText_->clear();
    // Screens section 4.1: the line is kept on the end page (section 8).
    const QString size = backupSizeText(total_);
    checkedLine_ = files_ >= 0
                       ? wtr("Backup checked: %1 files, %2").arg(QLocale().toString(files_), size)
                       : wtr("Backup checked: %1").arg(size);
    note_->setText(checkedLine_);
    QAccessibleEvent alert(note_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
    pause_.start();
}

void BackupPage::onFailed(const QString &name, const QString &code)
{
    if (name != step_ || !mine(name))
        return;
    active_ = false;
    if (code == error::cancelled) {
        // Cancel writes nothing on the iPod and the checked files stay in the folder.
        stopped_ = true;
        note_->setText(wtr("Stopped. The files already checked are kept: the backup goes on "
                           "from there at the next try."));
        return;
    }
    failed_ = true;
    showFailure(code);
}

// Screens sections 1.4 and 4.2: the banner, then what the page had (the bar stays where it
// stopped), the names, and as the last line that nothing was written to the iPod.
void BackupPage::showFailure(const QString &code)
{
    banner_->setText(failureText(code));
    banner_->show();
    QAccessibleEvent alert(banner_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
    fileLine_->clear();
    note_->clear();
    showNames();
    last_->setText(wtr("Nothing was written to the iPod."));
    last_->show();
}

void BackupPage::showNames()
{
    if (names_.isEmpty()) {
        namesTitle_->hide();
        namesList_->hide();
        return;
    }
    QStringList shown = names_.mid(0, kMostNames);
    if (names_.size() > kMostNames)
        shown << wtr("and %1 more").arg(names_.size() - kMostNames);
    namesTitle_->setText(wtr("Files concerned"));
    namesTitle_->show();
    namesList_->setPlainText(shown.join(QLatin1Char('\n')));
    namesList_->show();
}

} // namespace tacetos
