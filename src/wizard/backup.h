// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "filefault.h"
#include "step.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace tacetos::helper {
class HelperClient;
}

// The backups of the wizard and their restore (docs/specs/install-wizard.md
// section 5).

namespace tacetos::wizard {

// Section 5.3: the three backup steps.
enum class BackupKind { Full, Keep, Settings };

QString backupKindName(BackupKind kind);
std::optional<BackupKind> backupKindFromName(const QString &name);

// Section 5.4: one file of the volume, as the manifest names it.
struct BackupFile
{
    // Relative to the volume, `/` between folders.
    QString path;
    qint64 size = 0;
    QDateTime modified;
    QString sha256;
};

// Section 5.4: the copy of the firmware partition of an older iPod.
struct FirmwarePartitionCopy
{
    // The name the helper gave, in the backup folder.
    QString file;
    qint64 size = 0;
    QString sha256;
};

struct BackupManifest
{
    BackupKind kind = BackupKind::Keep;
    QString serial;
    QString model;
    QString appVersion;
    QDateTime created;
    QString volumeLabel;
    QList<BackupFile> files;
    QStringList folders;
    std::optional<FirmwarePartitionCopy> firmwarePartition;
    bool complete = true;
};

QJsonObject backupFileJson(const BackupFile &file);
std::optional<BackupFile> backupFileFromJson(const QJsonObject &object);
QByteArray manifestJson(const BackupManifest &manifest);

struct ManifestRead
{
    BackupManifest manifest;
    // Empty when the manifest can be used, otherwise why not (for the journal).
    QString error;
};

// Reads `manifest.json` of a backup folder.
ManifestRead readManifest(const QString &backupFolder);

// Section 5.1: the names of the folders of a backup.
QString serialFolderName(const QString &serial);
QString dateFolderName(const QDateTime &local);
QString datedBackupFolder(const QString &root, const QString &serial, const QDateTime &local);
QString defaultBackupRoot();

// Where the files of the volume are kept in a backup folder.
QString backupVolumeFolder(const QString &backupFolder);

// Section 5.3 and 5.5: what a backup copies, in the order of the copy: in each
// folder, the folders first, then the files, names sorted.
struct BackupPlan
{
    QStringList folders;
    QStringList files;
    qint64 bytes = 0;
    // Rule 2: links are not followed and not copied.
    int symlinks = 0;
};

BackupPlan planBackup(const QString &volumeRoot, BackupKind kind);

// Section 5.4: a verified backup. The sha256 are not read again.
bool isVerifiedBackup(const QString &backupFolder, const QString &serial, QString *why = nullptr);

// Section 5.6: the path of the copy of the firmware partition of a verified
// backup of this serial, its sha256 read again. Empty when there is none.
QString checkedFirmwarePartition(const QString &backupFolder, const QString &serial,
                                 QString *why = nullptr);

// The answer of the helper to one command.
struct HelperAnswer
{
    QString code;
    QString detail;
    QJsonObject data;
    bool ok() const { return code.isEmpty(); }
};

// Section 5.3 rule 1: the firmware partition is copied, and written back, by
// the helper only. An interface, so that the tests give their own.
class FirmwarePartition
{
public:
    virtual ~FirmwarePartition() = default;
    // `backup-firmware` into `dataDir`; `data.path` is the copy.
    virtual HelperAnswer backup(const QString &dataDir) = 0;
    // `restore-backup` of `file`, which is in `dataDir`.
    virtual HelperAnswer restore(const QString &file, const QString &dataDir) = 0;
};

// The real one: `read-state` for the scan token, then the command, through the
// client of the helper. May be called from the thread of the runner: the
// requests are then sent in the thread of the client, which must run its loop.
class HelperFirmwarePartition : public FirmwarePartition
{
public:
    HelperFirmwarePartition(helper::HelperClient &client, const QJsonObject &target,
                            const QString &port = QString());
    HelperAnswer backup(const QString &dataDir) override;
    HelperAnswer restore(const QString &file, const QString &dataDir) override;

private:
    HelperAnswer call(const QString &cmd, const QJsonObject &args);
    HelperAnswer scanToken(QString *token);

    helper::HelperClient &m_client;
    QJsonObject m_target;
    QString m_port;
};

// What a backup step is made from. The app gives the real ones, the tests
// their own.
class BackupSources
{
public:
    virtual ~BackupSources() = default;
    // The mount point of the music volume.
    virtual QString volumeRoot() const = 0;
    virtual QString volumeLabel() const = 0;
    // Section 5.1: the folder the user chose, without serial and date.
    virtual QString backupRoot() const = 0;
    // The free bytes of a folder of the computer, -1 when not known.
    virtual qint64 freeBytes(const QString &folder) const = 0;
    // Section 5.2: the used space of the volume, for a clean install.
    virtual qint64 volumeUsedBytes() const = 0;
    // Null for a Classic.
    virtual FirmwarePartition *firmwarePartition() const = 0;
    virtual qint64 firmwarePartitionBytes() const = 0;
    virtual QString appVersion() const { return QString(); }
    virtual QDateTime now() const { return QDateTime::currentDateTime(); }
    // For the tests: a write to the backup folder that fails as a full disk.
    virtual installer::FileFault fault() const { return {}; }
};

// What the last run of a backup step found, for the screen and the journal.
struct BackupReport
{
    QString folder;
    int copied = 0;
    int skipped = 0;
    int symlinks = 0;
    // Rule 3 and section 5.5: names for the screen, never for the journal.
    QStringList unread;
    QStringList refused;
};

// The steps `backup-full`, `backup-keep` and `backup-settings` (sections 5.2 to
// 5.5). The fact `manifest` is the path of the written manifest.
class BackupStep : public Step
{
public:
    BackupStep(BackupKind kind, const BackupSources &sources) : m_kind(kind), m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

    BackupReport report() const { return m_report; }

private:
    BackupKind m_kind;
    const BackupSources &m_sources;
    BackupReport m_report;
};

// Section 5.7: restore in one click, for `keep` and `settings` backups.
struct RestoreOptions
{
    // Write the firmware partition back too (`restore-backup`).
    bool firmwarePartition = false;
    installer::FileFault fault;
    std::function<bool()> stop;
};

struct RestoreResult
{
    QString code;
    QString detail;
    int restored = 0;
    bool ok() const { return code.isEmpty(); }
};

RestoreResult restoreFromBackup(const QString &backupFolder, const QString &volumeRoot,
                                const QString &serial, FirmwarePartition *firmware,
                                const RestoreOptions &options = RestoreOptions());

} // namespace tacetos::wizard
