// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "backup.h"
#include "format.h"
#include "sources.h"
#include "step.h"

#include <QJsonObject>
#include <QString>
#include <memory>

// The steps of a flow, made from the real sources of Linux: one object per
// flow that holds the sources, and gives the runner the `StepFactory` of
// section 2.3 (docs/specs/install-wizard.md).

namespace tacetos::device {
class LiveTracker;
}

namespace tacetos::wizard {

// Must outlive the runner it gives its factory to: the steps hold references
// to the sources kept here.
class LinuxStepFactory
{
public:
    // `target` and `port` are those of the iPod for the helper (`target` is
    // `HelperClient::imageTarget()` for an image). `backend` is the disk
    // service; null: the real one over D-Bus. `disks` is what the mounter
    // reads the disks from; empty: sysfs. `commands` is what the steps send
    // their commands to; null: the helper through `client`. The tests give one
    // that runs `classic-dfu` in their own process.
    LinuxStepFactory(const LinuxSourcesConfig &config, helper::HelperClient &client,
                     const QJsonObject &target, const QString &port = QString(),
                     std::unique_ptr<device::UdisksBackend> backend = nullptr,
                     UdisksMounter::DiskReader disks = UdisksMounter::DiskReader(),
                     std::unique_ptr<HelperCommands> commands = nullptr);
    ~LinuxStepFactory();

    // The step of a name of section 2.3, null for a name it does not know.
    StepFactory factory();
    std::unique_ptr<Step> make(const QString &name);

    // Section 9.2 steps 4 and 5, and section 6.3 step 3: feeds the sources and
    // the watches from the signals of detection, and gives the volume watch
    // the mounter. Call it from the thread of the screen.
    void connectDetection(device::LiveTracker &tracker);

    IpodView &view() { return *m_view; }
    DetectionVolumeWatch &volumes() { return *m_volumes; }
    DetectionSightWatch &sight() { return *m_sight; }
    UdisksMounter &mounter() { return *m_mounter; }
    HelperCommands &commands() { return *m_commands; }
    FirmwarePartition &partition() { return *m_partition; }
    const DiagnoseSources &diagnoseSources() const { return *m_diagnose; }
    LinuxStepSources &stepSources() { return *m_sources; }

private:
    std::unique_ptr<IpodView> m_view;
    std::unique_ptr<HelperCommands> m_commands;
    std::unique_ptr<HelperFirmwarePartition> m_partition;
    std::shared_ptr<UdisksMounter> m_mounter;
    std::unique_ptr<DetectionVolumeWatch> m_volumes;
    std::unique_ptr<DetectionSightWatch> m_sight;
    std::unique_ptr<LinuxDiagnoseSources> m_diagnose;
    std::unique_ptr<LinuxStepSources> m_sources;
};

} // namespace tacetos::wizard
