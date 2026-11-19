// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/restorepage.h"

#include "wizard/backuppage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>

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

// Screens section 13.2: the kind of a backup.
QString kindText(BackupKind kind)
{
    switch (kind) {
    case BackupKind::Full:
        return wtr("Full");
    case BackupKind::Keep:
        return wtr("Keep my music");
    case BackupKind::Settings:
        break;
    }
    return wtr("Settings");
}

// Wizard section 13: the message of a code of the restore.
QString failureText(const QString &code)
{
    if (code == error::backupDamaged)
        return wtr("This backup is damaged.");
    if (code == error::restoreWriteFailed)
        return wtr("Some files could not be written back to the iPod. Check that it has room, "
                   "then try again.");
    return wtr("The backup could not be put back.");
}

bool askWithDialog(QWidget *parent, const QString &title, const QString &text)
{
    return QMessageBox::question(parent, title, text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

} // namespace

RestorePage::RestorePage(const QString &backupRoot, const QString &serial, QWidget *parent)
    : QWidget(parent), root_(backupRoot), serial_(serial)
{
    setObjectName(QStringLiteral("restorePage"));
    asker_ = [this](const QString &title, const QString &text) {
        return askWithDialog(this, title, text);
    };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    banner_ = makeLabel(QStringLiteral("restoreBanner"), this, "danger");
    banner_->hide();
    title_ = makeLabel(QStringLiteral("restoreTitle"), this, "title");
    title_->setText(wtr("Backups of this iPod"));
    list_ = new QWidget(this);
    list_->setObjectName(QStringLiteral("backupList"));
    new QVBoxLayout(list_);
    list_->layout()->setContentsMargins(0, 0, 0, 0);
    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::buttonClicked, this, [this] { refresh(); });

    firmwareBox_ = new QCheckBox(wtr("Also write back the firmware partition"), this);
    firmwareBox_->setObjectName(QStringLiteral("restoreFirmwarePartition"));
    firmwareBox_->setAccessibleName(firmwareBox_->text());
    plugNote_ = makeLabel(QStringLiteral("restorePlugIn"), this, "secondary");
    plugNote_->setText(wtr("Plug in the iPod of this backup"));

    bar_ = new QProgressBar(this);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Restore progress"));
    bar_->hide();
    barText_ = makeLabel(QStringLiteral("stepProgressText"), this);
    summary_ = makeLabel(QStringLiteral("restoreSummary"), this);

    restore_ = new QPushButton(wtr("Restore"), this);
    restore_->setObjectName(QStringLiteral("restoreStart"));
    restore_->setProperty("role", QStringLiteral("primary"));
    restore_->setAccessibleName(restore_->text());
    connect(restore_, &QPushButton::clicked, this, &RestorePage::onRestore);

    layout->addWidget(banner_);
    layout->addWidget(title_);
    layout->addSpacing(16);
    layout->addWidget(list_);
    layout->addWidget(firmwareBox_);
    layout->addWidget(plugNote_);
    layout->addWidget(bar_);
    layout->addWidget(barText_);
    layout->addWidget(summary_);
    layout->addStretch();
    layout->addWidget(restore_, 0, Qt::AlignRight);

    reload();
}

RestorePage::~RestorePage()
{
    if (thread_) {
        stop_ = true;
        thread_->wait();
    }
}

void RestorePage::setVolume(const QString &mountRoot)
{
    volume_ = mountRoot;
    refresh();
}

void RestorePage::setFirmwarePartition(FirmwarePartition *partition)
{
    firmware_ = partition;
    refresh();
}

int RestorePage::chosen() const
{
    for (int i = 0; i < entries_.size(); ++i) {
        auto *b = list_->findChild<QRadioButton *>(QStringLiteral("backup-") + entries_[i].name);
        if (b && b->isChecked())
            return i;
    }
    return -1;
}

// Section 5.1: the backups of the serial are the dated folders of `<root>/<serial>/`.
void RestorePage::reload()
{
    entries_.clear();
    const QDir serialDir(QDir(root_).filePath(serialFolderName(serial_)));
    const QStringList names = serialDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        BackupEntry entry;
        entry.name = name;
        entry.folder = serialDir.filePath(name);
        const ManifestRead read = readManifest(entry.folder);
        entry.manifest = read.manifest;
        entry.usable = read.error.isEmpty() && read.manifest.complete;
        entries_ << entry;
    }
    // The newest first: the names sort as the dates do.
    std::sort(entries_.begin(), entries_.end(),
              [](const BackupEntry &a, const BackupEntry &b) { return a.name > b.name; });
    rebuild();
    refresh();
}

QString RestorePage::rowText(const BackupEntry &entry) const
{
    QDateTime when = QDateTime::fromString(entry.name, QStringLiteral("yyyy-MM-dd_HHmmss"));
    if (!when.isValid())
        when = entry.manifest.created.toLocalTime();
    QString text = QLocale().toString(when, QStringLiteral("d MMM yyyy, HH:mm"));
    if (!entry.usable)
        return text + QStringLiteral("   ") + wtr("Incomplete");
    qint64 bytes = 0;
    for (const BackupFile &file : entry.manifest.files)
        bytes += file.size;
    return text + QStringLiteral("   ") + kindText(entry.manifest.kind) + QStringLiteral("   ") +
           wtr("%1 files, %2")
               .arg(QLocale().toString(entry.manifest.files.size()), backupSizeText(bytes));
}

void RestorePage::rebuild()
{
    for (QAbstractButton *b : group_->buttons()) {
        group_->removeButton(b);
        b->deleteLater();
    }
    auto *box = qobject_cast<QVBoxLayout *>(list_->layout());
    while (QLayoutItem *item = box->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (entries_.isEmpty()) {
        auto *none = makeLabel(QStringLiteral("backupNone"), list_, "secondary");
        none->setText(wtr("No backup of this iPod was found"));
        box->addWidget(none);
        return;
    }
    for (const BackupEntry &entry : std::as_const(entries_)) {
        auto *radio = new QRadioButton(rowText(entry), list_);
        radio->setObjectName(QStringLiteral("backup-") + entry.name);
        radio->setAccessibleName(radio->text());
        radio->setToolTip(QDir::toNativeSeparators(entry.folder));
        radio->setEnabled(entry.usable);
        group_->addButton(radio);
        box->addWidget(radio);
    }
}

void RestorePage::refresh()
{
    const int at = chosen();
    const bool plugged = !volume_.isEmpty();
    const bool hasFirmware = at >= 0 && firmware_ && entries_[at].manifest.firmwarePartition;
    // A running restore keeps the list as it is; otherwise only a usable line can be chosen.
    for (int i = 0; i < entries_.size(); ++i) {
        if (auto *b =
                list_->findChild<QRadioButton *>(QStringLiteral("backup-") + entries_[i].name))
            b->setEnabled(entries_[i].usable && !running_ && !done_);
    }
    firmwareBox_->setVisible(hasFirmware);
    if (!hasFirmware)
        firmwareBox_->setChecked(false);
    firmwareBox_->setEnabled(!running_ && !done_);
    plugNote_->setVisible(!plugged && !done_);
    restore_->setEnabled(at >= 0 && plugged && !running_ && !done_);
}

void RestorePage::onRestore()
{
    const int at = chosen();
    if (at < 0 || volume_.isEmpty() || running_)
        return;
    const BackupEntry entry = entries_[at];
    const bool full = entry.manifest.kind == BackupKind::Full;
    const bool withFirmware = firmwareBox_->isVisible() && firmwareBox_->isChecked();

    // Section 4.3: the confirmation of an install first, naming the iPod.
    if (withFirmware &&
        !asker_(wtr("Write back the firmware partition"),
                wtr("The firmware partition of %1 is written back from this backup.")
                    .arg(ipodName_.isEmpty() ? wtr("the iPod") : ipodName_)))
        return;
    if (full) {
        qint64 bytes = 0;
        for (const BackupFile &file : entry.manifest.files)
            bytes += file.size;
        if (!asker_(
                wtr("Put my music back"),
                wtr("Copy %1 files back to the iPod (%2)? Files already there are skipped.")
                    .arg(QLocale().toString(entry.manifest.files.size()), backupSizeText(bytes))))
            return;
    }

    running_ = true;
    stop_ = false;
    banner_->hide();
    summary_->clear();
    title_->setText(full ? wtr("Putting your music back") : wtr("Restoring your backup"));
    bar_->setRange(0, full ? 1000 : 0);
    bar_->setValue(0);
    bar_->show();
    barText_->clear();
    refresh();

    RestoreOptions options;
    options.firmwarePartition = withFirmware;
    options.fault = fault_;
    options.stop = [this] { return stop_.load(); };
    if (full) {
        options.progress = [this](qint64 done, qint64 total) {
            QMetaObject::invokeMethod(
                this, [this, done, total] { onProgress(done, total); }, Qt::QueuedConnection);
        };
    }
    const QString folder = entry.folder;
    const QString volume = volume_;
    const QString serial = serial_;
    FirmwarePartition *firmware = firmware_;
    thread_ = QThread::create([this, folder, volume, serial, firmware, options] {
        const RestoreResult result = restoreFromBackup(folder, volume, serial, firmware, options);
        QMetaObject::invokeMethod(
            this, [this, result] { onResult(result.code, result.restored, result.skipped); },
            Qt::QueuedConnection);
    });
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void RestorePage::onProgress(qint64 done, qint64 total)
{
    if (!running_ || total <= 0)
        return;
    bar_->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
    barText_->setText(wtr("%1 of %2").arg(backupSizeText(done), backupSizeText(total)));
}

void RestorePage::onResult(const QString &code, int restored, int skipped)
{
    if (thread_) {
        thread_->wait();
        thread_ = nullptr;
    }
    running_ = false;
    const QString count = QLocale().toString(restored);
    if (code.isEmpty()) {
        done_ = true;
        bar_->setRange(0, 1000);
        bar_->setValue(1000);
        QString text = wtr("Done: %1 files put back").arg(count);
        if (skipped > 0)
            text += QStringLiteral(" ") + wtr("(%1 were already there)").arg(skipped);
        summary_->setText(text);
        QAccessibleEvent alert(summary_, QAccessible::Alert);
        QAccessible::updateAccessibility(&alert);
    } else if (code == error::cancelled) {
        summary_->setText(
            wtr("Stopped. %1 files were put back. The next restore skips them.").arg(count));
    } else {
        // Wizard section 13: how many files were put back.
        banner_->setText(failureText(code));
        banner_->show();
        summary_->setText(wtr("%1 files were put back.").arg(count));
        QAccessibleEvent alert(banner_, QAccessible::Alert);
        QAccessible::updateAccessibility(&alert);
    }
    refresh();
    emit restoreFinished(code);
}

} // namespace tacetos
