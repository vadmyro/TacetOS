// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "stepfactory.h"

#include "live.h"
#include "udisks.h"

#include <QObject>

#if defined(TACETOS_HAVE_DBUS)
#include "udisksbackend.h"
#endif

namespace tacetos::wizard {

namespace {

// The disk service when this build has no D-Bus: every call says so.
class MissingBackend : public device::UdisksBackend
{
public:
    std::optional<device::UdisksObjects> managedObjects(device::UdisksReply *error) override
    {
        if (error)
            *error = missing();
        return std::nullopt;
    }
    device::UdisksReply mount(const QString &, bool) override { return missing(); }
    device::UdisksReply unmount(const QString &) override { return missing(); }
    device::UdisksReply eject(const QString &) override { return missing(); }
    device::UdisksReply powerOff(const QString &) override { return missing(); }

private:
    static device::UdisksReply missing()
    {
        return {QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"),
                QStringLiteral("this build has no D-Bus"),
                {}};
    }
};

std::unique_ptr<device::UdisksBackend> realBackend()
{
#if defined(TACETOS_HAVE_DBUS)
    return std::make_unique<device::UdisksDBusBackend>();
#else
    return std::make_unique<MissingBackend>();
#endif
}

} // namespace

LinuxStepFactory::LinuxStepFactory(const LinuxSourcesConfig &config, helper::HelperClient &client,
                                   const QJsonObject &target, const QString &port,
                                   std::unique_ptr<device::UdisksBackend> backend,
                                   UdisksMounter::DiskReader disks,
                                   std::unique_ptr<HelperCommands> commands)
{
    m_view = std::make_unique<IpodView>(config.serial, port);
    if (commands) {
        m_commands = std::move(commands);
    } else {
        auto real = std::make_unique<ClientHelperCommands>(client, target, port);
        if (target.value(QStringLiteral("kind")).toString() == QLatin1String("disk")) {
            // The disk as detection shows it now: an iPod that restarted may come
            // back under another name.
            IpodView *view = m_view.get();
            real->setTargetSource([view, target] {
                QJsonObject now = target;
                const device::DetectedIpod ipod = view->current();
                if (ipod.disk && !ipod.disk->node.isEmpty()) {
                    now.insert(QStringLiteral("node"), ipod.disk->node);
                    now.insert(QStringLiteral("sizeBytes"), double(ipod.disk->sizeBytes));
                }
                return now;
            });
        }
        m_commands = std::move(real);
    }
    m_partition = std::make_unique<HelperFirmwarePartition>(client, target, port);
    m_mounter = std::make_shared<UdisksMounter>(backend ? std::move(backend) : realBackend(),
                                                std::move(disks));
    const std::shared_ptr<UdisksMounter> mounter = m_mounter;
    m_volumes = std::make_unique<DetectionVolumeWatch>(
        [mounter](const QString &serial) { mounter->mount(serial); });
    m_sight = std::make_unique<DetectionSightWatch>();
    m_diagnose = std::make_unique<LinuxDiagnoseSources>(config, *m_view, *m_commands, client);
    m_sources = std::make_unique<LinuxStepSources>(config, *m_view, *m_commands, m_partition.get(),
                                                   *m_volumes, *m_sight, *m_diagnose);
}

LinuxStepFactory::~LinuxStepFactory() = default;

StepFactory LinuxStepFactory::factory()
{
    return [this](const QString &name) { return make(name); };
}

std::unique_ptr<Step> LinuxStepFactory::make(const QString &name)
{
    if (name == step::diagnose)
        return std::make_unique<DiagnoseStep>(*m_diagnose);
    if (name == step::backupFull)
        return std::make_unique<BackupStep>(BackupKind::Full, *m_sources);
    if (name == step::backupKeep)
        return std::make_unique<BackupStep>(BackupKind::Keep, *m_sources);
    if (name == step::backupSettings)
        return std::make_unique<BackupStep>(BackupKind::Settings, *m_sources);
    if (name == step::diskMode)
        return std::make_unique<DiskModeStep>(*m_sources);
    if (name == step::format)
        return std::make_unique<FormatStep>(false, *m_sources);
    if (name == step::convert)
        return std::make_unique<FormatStep>(true, *m_sources);
    if (name == step::firmware)
        return std::make_unique<FirmwareStep>(*m_sources);
    if (name == step::bootloader)
        return std::make_unique<BootloaderStep>(*m_sources);
    if (name == step::classicBootloader)
        return std::make_unique<ClassicDfuStep>(*m_sources);
    if (name == step::removeBootloader)
        return std::make_unique<RemoveBootloaderStep>(*m_sources);
    if (name == step::classicRemove)
        return std::make_unique<ClassicRemoveStep>(*m_sources);
    if (name == step::removeFirmware)
        return std::make_unique<RemoveFirmwareStep>(*m_sources);
    if (name == step::verify)
        return std::make_unique<VerifyStep>(*m_sources);
    if (name == step::finish)
        return std::make_unique<FinishStep>(*m_sources);
    return nullptr;
}

void LinuxStepFactory::feed(const device::DetectedIpod &ipod)
{
    m_mounter->seen(ipod);
    m_view->seen(ipod);
    m_volumes->seen(ipod);
    m_sight->seen(ipod);
}

void LinuxStepFactory::connectDetection(device::LiveTracker &tracker)
{
    const std::shared_ptr<UdisksMounter> mounter = m_mounter;
    IpodView *view = m_view.get();
    DetectionVolumeWatch *volumes = m_volumes.get();
    DetectionSightWatch *sight = m_sight.get();
    const auto seen = [mounter, view, volumes, sight](const device::DetectedIpod &ipod) {
        mounter->seen(ipod);
        view->seen(ipod);
        volumes->seen(ipod);
        sight->seen(ipod);
    };
    QObject::connect(&tracker, &device::LiveTracker::ipodAdded, volumes, seen);
    QObject::connect(&tracker, &device::LiveTracker::ipodChanged, volumes, seen);
    QObject::connect(&tracker, &device::LiveTracker::ipodRemoved, volumes,
                     [view, sight](const QString &portPath) {
                         view->gone(portPath);
                         sight->gone(portPath);
                     });
}

} // namespace tacetos::wizard
