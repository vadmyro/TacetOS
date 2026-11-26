// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "backup.h"
#include "bootloader.h"
#include "classicdfu.h"
#include "detected.h"
#include "diagnose.h"
#include "download.h"
#include "firmware.h"
#include "format.h"
#include "helpercalls.h"
#include "linuxdisks.h"
#include "models.h"
#include "udisks.h"
#include "uninstall.h"
#include "verify.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

// The real sources of the wizard on Linux (docs/specs/install-wizard.md
// sections 4 to 9): what the steps are made from when the app runs them. No
// user interface here; the tests give the helper an image and the disk service
// a bus that answers from memory.

namespace tacetos::wizard {

// What the app knows when it starts a flow.
struct LinuxSourcesConfig
{
    // The iPod of the flow. Empty: the only iPod detection shows.
    QString serial;
    // Its model in `models.toml`: `bootloaderTarget`, `firmwareFolder` and `name`
    // are used.
    device::Model model;
    Family family = Family::Partition;
    // Section 5.1: the folder the user chose, without serial and date. Empty:
    // the default one.
    QString backupRoot;
    // Section 9.2 step 3: the data folder of the app.
    QString dataDir;
    // Section 4.4: the root the system release file is read under.
    QString osReleaseRoot = QStringLiteral("/");
    QString udevRuleFile = QString(kUdevRuleFile);
    QString polkitRuleFile = QString(kPolkitRuleFile);
    QString appVersion;
    // Section 9.2 step 2: the `install-record.json` files of the Classics.
    QStringList installRecords;
    // The port of the iPod in detection, for the Classic steps.
    QString portPath;
    // For the tests: the udisks2 probe and the hello of the helper. Empty: the
    // real ones.
    std::function<UdisksInfo()> udisksProbe;
    std::function<QJsonObject()> helloReader;
    // For the tests: shorter waits. Negative: the defaults of the steps.
    qint64 volumeReturnMs = -1;
    qint64 returnMs = -1;
    // Update section 4: the channel an update downloads from. The app gives the
    // one of this build (`channelOfThisBuild`).
    update::ChannelAddress channel = update::defaultChannel();
    // For the tests: how long a read of the channel may get no byte. Negative:
    // the default.
    qint64 channelTimeoutMs = -1;
};

// What detection says, kept for the steps. Fed by the signals of detection in
// the thread of the screen, read in the thread of the runner.
class IpodView
{
public:
    // `portPath` finds the iPod of the flow when its serial changed: the Classic
    // gives another one under TacetOS than under Apple's firmware.
    explicit IpodView(QString serial = QString(), QString portPath = QString())
        : m_serial(std::move(serial)), m_portPath(std::move(portPath))
    {
    }
    void seen(const device::DetectedIpod &ipod);
    void gone(const QString &portPath);
    // The iPod of the flow; empty `serial` when detection shows none.
    device::DetectedIpod current() const;
    // The other iPods detection shows (section 4.3, check `single-ipod`).
    int otherIpods() const;
    // The mount points of every iPod seen, for the backup folder check.
    QStringList volumeRoots() const;

private:
    mutable QMutex m_mutex;
    QString m_serial;
    QString m_portPath;
    // By port path, in the order of arrival.
    QList<device::DetectedIpod> m_ipods;
};

// Section 4.2: what the hello of the helper says.
HelperInfo helperInfoFromHello(const QJsonObject &hello);

// Section 4.3, check `apple-firmware`: `VisibleBuildID` of the device sheet,
// empty when the text has none.
QString visibleBuildId(const QString &sheetText);

// The disk service asked to mount the music volume of an iPod by its serial:
// the `Mounter` of `DetectionVolumeWatch`. It remembers the port path of each
// iPod detection shows, finds its disk in sysfs, and mounts it through udisks2
// (never `sudo`: the service asks the user itself).
class UdisksMounter
{
public:
    using DiskReader = std::function<QList<device::LinuxDisk>()>;
    explicit UdisksMounter(std::unique_ptr<device::UdisksBackend> backend,
                           DiskReader disks = DiskReader());
    ~UdisksMounter();

    void seen(const device::DetectedIpod &ipod);
    // What the last mount said, for the journal.
    device::DiskResult mount(const QString &serial);
    device::DiskResult last() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

// Section 4: the sources of the diagnosis.
class LinuxDiagnoseSources : public DiagnoseSources
{
public:
    LinuxDiagnoseSources(const LinuxSourcesConfig &config, const IpodView &view,
                         HelperCommands &commands, helper::HelperClient &client);

    Os os() const override { return Os::Linux; }
    QString osRelease() const override;
    UdisksInfo udisks() const override;
    HelperInfo helper() const override;
    bool udevRulePresent() const override;
    FolderInfo backupFolder() const override;
    qint64 backupNeedBytes(InstallPath path) const override;
    IpodFacts ipod() const override;
    PackageCheck package(const QString &manifestPath) const override;
    qint64 firmwareNeedBytes(const PackageCheck &package) const override;

private:
    LinuxSourcesConfig m_config;
    const IpodView &m_view;
    HelperCommands &m_commands;
    helper::HelperClient &m_client;
};

// The size of the firmware partition of an older iPod, from `read-state`
// (its first slot). 0 when it cannot be read.
qint64 firmwarePartitionSize(HelperCommands &commands);

// Sections 5 to 10: the sources of every other step.
class LinuxStepSources : public BackupSources,
                         public FormatSources,
                         public FirmwareSources,
                         public BootloaderSources,
                         public UninstallSources,
                         public ClassicDfuSources,
                         public VerifySources,
                         public DownloadSources
{
public:
    LinuxStepSources(const LinuxSourcesConfig &config, const IpodView &view,
                     HelperCommands &commands, FirmwarePartition *partition, VolumeWatch &volumes,
                     IpodSightWatch &sight, const DiagnoseSources &diagnose);

    // Shared by several of the interfaces.
    QString volumeRoot() const override;
    QString volumeLabel() const override;
    QString target() const override { return m_config.model.bootloaderTarget; }
    QString firmwareFolder() const override { return m_config.model.firmwareFolder; }
    HelperCommands &helper() const override { return m_commands; }
    VolumeWatch &volumes() const override { return m_volumes; }
    qint64 freeBytes() const override;

    // BackupSources.
    QString backupRoot() const override;
    qint64 freeBytes(const QString &folder) const override;
    qint64 volumeUsedBytes() const override;
    FirmwarePartition *firmwarePartition() const override;
    qint64 firmwarePartitionBytes() const override;
    QString appVersion() const override { return m_config.appVersion; }

    // FormatSources.
    QString modelName() const override { return m_config.model.name; }
    qint64 volumeReturnMs() const override;

    // FirmwareSources.
    QString settingsBackup() const override;

    // BootloaderSources and ClassicDfuSources.
    qint64 returnMs() const override;
    IpodSightWatch &sight() const override { return m_sight; }
    const DiagnoseSources &diagnose() const override { return m_diagnose; }
    QString dataDir() const override { return m_config.dataDir; }
    QString portPath() const override { return m_config.portPath; }
    QStringList installRecords() const override { return m_config.installRecords; }

    // VerifySources.
    device::DetectedIpod detected() const override { return m_view.current(); }
    qint64 readyWaitMs() const override { return returnMs(); }

    // UninstallSources.
    qint64 volumeWaitMs() const override { return returnMs(); }

    // DownloadSources, and the clean up of `finish`.
    QString ipodVersion() const override;
    update::ChannelAddress channel() const override { return m_config.channel; }
    QString updatesFolder() const override;
    qint64 channelTimeoutMs() const override;

private:
    LinuxSourcesConfig m_config;
    const IpodView &m_view;
    HelperCommands &m_commands;
    FirmwarePartition *m_partition;
    VolumeWatch &m_volumes;
    IpodSightWatch &m_sight;
    const DiagnoseSources &m_diagnose;
};

} // namespace tacetos::wizard
