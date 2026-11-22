// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "sources.h"

#include "helperclient.h"
#include "helperconstants.h"
#include "messages.h"
#include "package.h"
#include "udevrule.h"
#include "udisks.h"
#include "wizardconstants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStorageInfo>

#if defined(TACETOS_HAVE_DBUS)
#include "udisksbackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#endif

namespace tacetos::wizard {

namespace {

QString mountOf(const device::DetectedIpod &ipod)
{
    return ipod.volume ? ipod.volume->mountPoint : QString();
}

// The system describes the file system that holds a folder. That is the iPod
// only when the folder is where the iPod is mounted, not a folder on another
// disk.
bool describesVolume(const QStorageInfo &storage, const QString &mountPoint)
{
    return !mountPoint.isEmpty() && storage.isValid() && storage.bytesTotal() > 0 &&
           QDir::cleanPath(QDir::fromNativeSeparators(storage.rootPath())) ==
               QDir::cleanPath(QDir::fromNativeSeparators(mountPoint));
}

// The used bytes of a mounted volume, from the system first, then from what
// detection read.
qint64 usedBytes(const device::DetectedIpod &ipod)
{
    const QStorageInfo storage(mountOf(ipod));
    if (describesVolume(storage, mountOf(ipod)))
        return storage.bytesTotal() - storage.bytesFree();
    if (ipod.volume)
        return qMax<qint64>(0, ipod.volume->totalBytes - ipod.volume->freeBytes);
    return 0;
}

Filesystem filesystemOf(const device::DetectedIpod &ipod)
{
    if (ipod.state == device::IpodState::MacFormat)
        return Filesystem::HfsPlus;
    if (!ipod.volume || ipod.volume->filesystem.isEmpty())
        return Filesystem::Unknown;
    const QString fs = ipod.volume->filesystem.toLower();
    if (fs == QLatin1String("vfat") || fs == QLatin1String("fat32") ||
        fs == QLatin1String("msdos") || fs == QLatin1String("fat"))
        return Filesystem::Fat32;
    if (fs == QLatin1String("hfsplus") || fs == QLatin1String("hfs+") || fs == QLatin1String("hfs"))
        return Filesystem::HfsPlus;
    return Filesystem::Other;
}

int capacityOf(const device::Model &model, const device::DetectedIpod &ipod)
{
    for (const device::Variant &variant : model.variants) {
        if (!ipod.variant.key.isEmpty() && variant.key == ipod.variant.key)
            return variant.capacityGb;
    }
    return 0;
}

#if defined(TACETOS_HAVE_DBUS)
UdisksInfo probeUdisks()
{
    UdisksInfo info;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || !bus.interface())
        return info;
    const QString service = QString::fromLatin1(device::kUdisksService);
    info.running = bus.interface()->isServiceRegistered(service);
    const QStringList activatable = bus.interface()->activatableServiceNames().value();
    info.present = info.running || activatable.contains(service);
    if (info.running) {
        QDBusInterface manager(service, QStringLiteral("/org/freedesktop/UDisks2/Manager"),
                               QStringLiteral("org.freedesktop.UDisks2.Manager"), bus);
        info.version = manager.property("Version").toString();
    }
    return info;
}
#else
UdisksInfo probeUdisks()
{
    return {};
}
#endif

} // namespace

// IpodView

void IpodView::seen(const device::DetectedIpod &ipod)
{
    QMutexLocker lock(&m_mutex);
    for (device::DetectedIpod &known : m_ipods) {
        if (known.portPath == ipod.portPath) {
            known = ipod;
            return;
        }
    }
    m_ipods << ipod;
}

void IpodView::gone(const QString &portPath)
{
    QMutexLocker lock(&m_mutex);
    for (int i = 0; i < m_ipods.size(); ++i) {
        if (m_ipods.at(i).portPath == portPath) {
            m_ipods.removeAt(i);
            return;
        }
    }
}

device::DetectedIpod IpodView::current() const
{
    QMutexLocker lock(&m_mutex);
    for (const device::DetectedIpod &ipod : m_ipods) {
        if (m_serial.isEmpty() || ipod.serial == m_serial)
            return ipod;
    }
    for (const device::DetectedIpod &ipod : m_ipods) {
        if (!m_portPath.isEmpty() && ipod.portPath == m_portPath)
            return ipod;
    }
    return {};
}

int IpodView::otherIpods() const
{
    QMutexLocker lock(&m_mutex);
    int others = 0;
    bool first = true;
    for (const device::DetectedIpod &ipod : m_ipods) {
        const bool mine =
            m_serial.isEmpty()
                ? first
                : ipod.serial == m_serial || (!m_portPath.isEmpty() && ipod.portPath == m_portPath);
        first = false;
        if (!mine)
            ++others;
    }
    return others;
}

QStringList IpodView::volumeRoots() const
{
    QMutexLocker lock(&m_mutex);
    QStringList roots;
    for (const device::DetectedIpod &ipod : m_ipods) {
        if (!mountOf(ipod).isEmpty())
            roots << mountOf(ipod);
    }
    return roots;
}

HelperInfo helperInfoFromHello(const QJsonObject &hello)
{
    HelperInfo info;
    info.found = !hello.isEmpty();
    info.protocolOk = hello.value(QStringLiteral("protocol")).toInt(-1) == helper::kProtocol;
    info.realWrites = hello.value(QStringLiteral("realWrites")).toBool();
    info.usb = hello.value(QStringLiteral("usb")).toBool();
    info.classic = hello.value(QStringLiteral("classic")).toBool();
    return info;
}

QString visibleBuildId(const QString &sheetText)
{
    static const QRegularExpression re(
        QStringLiteral("<key>VisibleBuildID</key>\\s*<string>([^<]*)</string>"));
    const QRegularExpressionMatch match = re.match(sheetText);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

qint64 firmwarePartitionSize(HelperCommands &commands)
{
    const HelperAnswer answer = commands.call(QString(helper::cmd::readState), QJsonObject());
    if (!answer.ok())
        return 0;
    const QJsonArray list = answer.data.value(QStringLiteral("slots")).toArray();
    return list.isEmpty()
               ? 0
               : qint64(list.first().toObject().value(QStringLiteral("size")).toDouble());
}

// UdisksMounter

struct UdisksMounter::Private
{
    Private(std::unique_ptr<device::UdisksBackend> backend, DiskReader reader)
        : control(std::move(backend)), disks(std::move(reader))
    {
    }
    QMutex mutex;
    device::DiskControl control;
    DiskReader disks;
    // By serial: the port path of the iPod.
    QHash<QString, QString> ports;
    device::DiskResult last;
};

UdisksMounter::UdisksMounter(std::unique_ptr<device::UdisksBackend> backend, DiskReader disks)
{
    if (!disks)
        disks = [] { return device::readLinuxDisks().disks; };
    d = std::make_unique<Private>(std::move(backend), std::move(disks));
}

UdisksMounter::~UdisksMounter() = default;

void UdisksMounter::seen(const device::DetectedIpod &ipod)
{
    if (ipod.serial.isEmpty() || ipod.portPath.isEmpty())
        return;
    QMutexLocker lock(&d->mutex);
    d->ports.insert(ipod.serial, ipod.portPath);
}

device::DiskResult UdisksMounter::mount(const QString &serial)
{
    QMutexLocker lock(&d->mutex);
    device::DiskResult result;
    result.error = device::DiskError::NotAnIpodDisk;
    result.detail = QStringLiteral("no disk for this iPod");
    const QString port = d->ports.value(serial);
    if (!port.isEmpty()) {
        for (const device::LinuxDisk &disk : d->disks()) {
            if (disk.portPath == port) {
                result = d->control.mount(disk);
                break;
            }
        }
    }
    d->last = result;
    return result;
}

device::DiskResult UdisksMounter::last() const
{
    QMutexLocker lock(&d->mutex);
    return d->last;
}

// LinuxDiagnoseSources

LinuxDiagnoseSources::LinuxDiagnoseSources(const LinuxSourcesConfig &config, const IpodView &view,
                                           HelperCommands &commands, helper::HelperClient &client)
    : m_config(config), m_view(view), m_commands(commands), m_client(client)
{
}

QString LinuxDiagnoseSources::osRelease() const
{
    return readOsRelease(m_config.osReleaseRoot);
}

UdisksInfo LinuxDiagnoseSources::udisks() const
{
    return m_config.udisksProbe ? m_config.udisksProbe() : probeUdisks();
}

HelperInfo LinuxDiagnoseSources::helper() const
{
    if (m_config.helloReader)
        return helperInfoFromHello(m_config.helloReader());
    // The hello comes some time after the start. A `cancel` of no request waits
    // for it (or for the helper to be missing, silent or of another version)
    // and changes nothing.
    if (m_client.hello().isEmpty())
        callHelper(m_client, QString(helper::cmd::cancel), {{QStringLiteral("request"), 0}});
    return helperInfoFromHello(m_client.hello());
}

bool LinuxDiagnoseSources::udevRulePresent() const
{
    // Helper section 11.5 step 1: the rule of this helper, or the same without its last line
    // on a system with no folder of polkit rules. A rule of an older build is missing.
    QFile file(m_config.udevRuleFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QString inPlace = QString::fromUtf8(file.readAll());
    if (inPlace == helper::udevRuleText())
        return true;
    return inPlace == helper::udevRuleTextWithoutPolkitLine() &&
           !QFileInfo(m_config.polkitRuleFile).dir().exists();
}

FolderInfo LinuxDiagnoseSources::backupFolder() const
{
    const QString root = m_config.backupRoot.isEmpty() ? defaultBackupRoot() : m_config.backupRoot;
    return folderInfo(root, m_view.volumeRoots());
}

qint64 LinuxDiagnoseSources::backupNeedBytes(InstallPath path) const
{
    const device::DetectedIpod ipod = m_view.current();
    const qint64 partition =
        m_config.family == Family::Partition ? firmwarePartitionSize(m_commands) : 0;
    if (path == InstallPath::Clean)
        return backupSpaceNeeded(BackupKind::Full, usedBytes(ipod), 0, partition);
    const BackupKind kind = path == InstallPath::Keep ? BackupKind::Keep : BackupKind::Settings;
    return backupSpaceNeeded(kind, 0, planBackup(mountOf(ipod), kind).bytes, partition);
}

IpodFacts LinuxDiagnoseSources::ipod() const
{
    const device::DetectedIpod ipod = m_view.current();
    IpodFacts facts;
    facts.serial = ipod.serial;
    facts.modelSupported =
        !ipod.model.isEmpty() && ipod.state != device::IpodState::UnsupportedModel;
    facts.capacityGb = capacityOf(m_config.model, ipod);
    facts.otherIpods = m_view.otherIpods();
    facts.volumeReady = ipod.state == device::IpodState::Ready && ipod.volume.has_value();
    if (ipod.state == device::IpodState::ReadOnly)
        facts.volumeMessage =
            device::stateMessage(ipod, {}) + QLatin1Char(' ') + device::readOnlyAdvice();
    facts.volumeMountable = ipod.state == device::IpodState::NoVolume && ipod.disk.has_value();
    facts.filesystem = filesystemOf(ipod);
    if (ipod.volume) {
        const QStorageInfo storage(ipod.volume->mountPoint);
        const bool valid = describesVolume(storage, ipod.volume->mountPoint);
        facts.volumeSize = valid ? storage.bytesTotal() : ipod.volume->totalBytes;
        facts.volumeFree = valid ? storage.bytesAvailable() : ipod.volume->freeBytes;
    }

    const HelperAnswer state = m_commands.call(QString(helper::cmd::readState), QJsonObject());
    facts.helperRead = state.ok();
    if (!state.ok()) {
        facts.readError = state.code;
        return facts;
    }
    const QString method = state.data.value(QStringLiteral("method")).toString();
    facts.methodAgrees =
        method ==
        (m_config.family == Family::Partition ? QLatin1String("partition") : QLatin1String("dfu"));
    if (m_config.family == Family::Partition) {
        facts.bootloaderState = state.data.value(QStringLiteral("state")).toString();
        for (const QJsonValue &entry : state.data.value(QStringLiteral("entries")).toArray()) {
            if (entry.toObject().value(QStringLiteral("type")).toString() == QLatin1String("OSOS"))
                facts.firmwarePartitionOk = true;
        }
    } else if (facts.methodAgrees) {
        const HelperAnswer sheet =
            m_commands.call(QString(helper::cmd::readDeviceSheet), QJsonObject());
        if (sheet.ok())
            facts.appleVersion =
                visibleBuildId(sheet.data.value(QStringLiteral("text")).toString());
    }
    return facts;
}

PackageCheck LinuxDiagnoseSources::package(const QString &manifestPath) const
{
    return checkPackage(manifestPath, m_config.model.bootloaderTarget);
}

qint64 LinuxDiagnoseSources::firmwareNeedBytes(const PackageCheck &package) const
{
    return package.unpackedBytes + kVolumeMargin;
}

// LinuxStepSources

LinuxStepSources::LinuxStepSources(const LinuxSourcesConfig &config, const IpodView &view,
                                   HelperCommands &commands, FirmwarePartition *partition,
                                   VolumeWatch &volumes, IpodSightWatch &sight,
                                   const DiagnoseSources &diagnose)
    : m_config(config), m_view(view), m_commands(commands), m_partition(partition),
      m_volumes(volumes), m_sight(sight), m_diagnose(diagnose)
{
}

QString LinuxStepSources::volumeRoot() const
{
    return mountOf(m_view.current());
}

QString LinuxStepSources::volumeLabel() const
{
    const device::DetectedIpod ipod = m_view.current();
    return ipod.volume ? ipod.volume->label : QString();
}

qint64 LinuxStepSources::freeBytes() const
{
    const QString root = volumeRoot();
    const QStorageInfo storage(root);
    return !root.isEmpty() && storage.isValid() ? storage.bytesAvailable() : -1;
}

QString LinuxStepSources::backupRoot() const
{
    return m_config.backupRoot.isEmpty() ? defaultBackupRoot() : m_config.backupRoot;
}

qint64 LinuxStepSources::freeBytes(const QString &folder) const
{
    return folderInfo(folder, QStringList()).freeBytes;
}

qint64 LinuxStepSources::volumeUsedBytes() const
{
    return usedBytes(m_view.current());
}

FirmwarePartition *LinuxStepSources::firmwarePartition() const
{
    return m_config.family == Family::Partition ? m_partition : nullptr;
}

qint64 LinuxStepSources::firmwarePartitionBytes() const
{
    return m_config.family == Family::Partition ? firmwarePartitionSize(m_commands) : 0;
}

qint64 LinuxStepSources::volumeReturnMs() const
{
    return m_config.volumeReturnMs >= 0 ? m_config.volumeReturnMs : kVolumeReturnMs;
}

qint64 LinuxStepSources::returnMs() const
{
    if (m_config.returnMs >= 0)
        return m_config.returnMs;
    return m_config.family == Family::Dfu ? kRestartWindowMs : kRestartWindowMs + kVolumeReturnMs;
}

QString LinuxStepSources::settingsBackup() const
{
    const QString serial = m_config.serial.isEmpty() ? m_view.current().serial : m_config.serial;
    if (serial.isEmpty())
        return QString();
    QDir folder(QDir(backupRoot()).filePath(serialFolderName(serial)));
    const QStringList dated = folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = dated.size() - 1; i >= 0; --i) {
        const QString path = folder.filePath(dated.at(i));
        const ManifestRead read = readManifest(path);
        if (read.error.isEmpty() && read.manifest.kind == BackupKind::Settings &&
            read.manifest.complete)
            return path;
    }
    return QString();
}

} // namespace tacetos::wizard
