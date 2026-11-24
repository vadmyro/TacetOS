// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <optional>

// The flows of the wizard and their steps (docs/specs/install-wizard.md
// section 2).

namespace tacetos::wizard {

// Section 1: how the bootloader is installed, from `method` in models.toml.
enum class Family { Partition, Dfu };

enum class FlowKind { Install, Uninstall };

// Section 2.2: the choice of the first screen. `None` for an uninstall.
enum class InstallPath { None, Clean, Keep };

// Section 2.3: the inputs the list of steps comes from.
struct FlowShape
{
    Family family = Family::Partition;
    FlowKind kind = FlowKind::Install;
    InstallPath path = InstallPath::None;
    bool macFormat = false;
    // Branding section 8.3 step 1: an iPod Classic whose old firmware folder moves, with a
    // bootloader that may not know the new one. Only read for Keep my music on a Classic.
    bool bootloaderFirst = false;

    bool operator==(const FlowShape &other) const
    {
        return family == other.family && kind == other.kind && path == other.path &&
               macFormat == other.macFormat && bootloaderFirst == other.bootloaderFirst;
    }
};

// Section 2.3: the names of the steps, as in the progress file and the tests.
namespace step {
inline constexpr QLatin1String diagnose("diagnose");
inline constexpr QLatin1String backupKeep("backup-keep");
inline constexpr QLatin1String backupFull("backup-full");
inline constexpr QLatin1String backupSettings("backup-settings");
inline constexpr QLatin1String diskMode("disk-mode");
inline constexpr QLatin1String format("format");
inline constexpr QLatin1String convert("convert");
inline constexpr QLatin1String firmware("firmware");
inline constexpr QLatin1String bootloader("bootloader");
inline constexpr QLatin1String classicBootloader("classic-bootloader");
inline constexpr QLatin1String removeBootloader("remove-bootloader");
inline constexpr QLatin1String classicRemove("classic-remove");
inline constexpr QLatin1String removeFirmware("remove-firmware");
inline constexpr QLatin1String verify("verify");
inline constexpr QLatin1String finish("finish");
} // namespace step

// Section 2.3: the list of steps of a flow. Empty for a shape that section 2.2
// does not allow: an install without a path, an uninstall with one, or Keep my
// music on a Mac iPod.
QStringList stepsFor(const FlowShape &shape);

// The words of the progress file (section 3.2).
QString familyName(Family family);
QString kindName(FlowKind kind);
QString pathName(InstallPath path);
std::optional<Family> familyFromName(const QString &name);
std::optional<FlowKind> kindFromName(const QString &name);
std::optional<InstallPath> pathFromName(const QString &name);

} // namespace tacetos::wizard
