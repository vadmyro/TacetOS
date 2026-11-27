// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QLatin1String>

// The one file of constants for the names that the firmware and the
// bootloader look for on the iPod (docs/specs/detection.md sections 7 and 11).
// These names are written here and nowhere else.

namespace tacetos::device {

// Section 7.1 rule 3: a marker file is read for at most this many bytes.
inline constexpr qint64 kMarkerMaxBytes = 64 * 1024;

// Section 7.2: the files of Apple's software, from the root of the music volume.
inline constexpr QLatin1String kAppleRootFolder("iPod_Control");
inline constexpr QLatin1String kAppleModelFile("iPod_Control/Device/SysInfo");
inline constexpr QLatin1String kAppleDatabase("iPod_Control/iTunes/iTunesDB");

// Section 8.4: the key of the model number string in the Apple model file.
inline constexpr QLatin1String kSysInfoModelKey("ModelNumStr");

// Section 8.4: a variant matches a disk when its size in GB (bytes / 10^9) is
// between these two factors times the capacity on the box.
inline constexpr double kCapacityLow = 0.90;
inline constexpr double kCapacityHigh = 1.05;

// Section 7.6: the key of the Apple model file that holds the build of Apple's
// firmware, as `0x01168000 (1.1.6)`: the version is the part in brackets.
inline constexpr QLatin1String kSysInfoFirmwareKey("visibleBuildID");

// Section 7.6: the only name strings that are ever tested. They are Apple's
// own; the strings of any other firmware are never compared to anything.
inline constexpr QLatin1String kUsbManufacturerApple("Apple Inc.");
inline constexpr QLatin1String kUsbProductApple("iPod");

// Section 7.6: a value of the distribution line may start with this name, which
// is then dropped so that the line does not read "TacetOS TacetOS 0.3".
inline constexpr QLatin1String kDistributionName("TacetOS");

// Section 8.2 rule 5: how long a normal mode reading is remembered for the
// same port, to carry its fields over to the DFU mode that follows.
inline constexpr qint64 kModeMemoryMs = 60 * 1000;

// Section 9.2: how long a new USB device may show no disk before the state
// becomes no-disk.
inline constexpr qint64 kDiskWaitMs = 10 * 1000;

// Section 9.2: once the disk is there and no volume is mounted, how long the
// wait lasts at most, counted from the moment the disk appeared. Then the
// state becomes no-volume (on Linux the system may not mount it by itself).
inline constexpr qint64 kMountGraceMs = 2 * 1000;

// Section 9.4: a device with the same port path that appears within this
// time is the same iPod in another mode. Measured: 8.1 s into DFU mode, 9.6 s
// out of it, 10.25 s for a restart of the firmware.
inline constexpr qint64 kSamePortWindowMs = 15 * 1000;

// Section 9.3: a state that goes down because the volume or the disk went away
// waits this long before it is shown, so that an unplug (volume, disk, then
// USB device, measured up to 1.16 s apart) shows nothing but the removal.
inline constexpr qint64 kDropSettleMs = 1500;

// Section 9.1: the polling rates used where no event source exists.
inline constexpr qint64 kPollUsbMs = 500;
inline constexpr qint64 kPollDisksMs = 1000;

// Section 9.1: the slow reading kept next to the event sources. A source can
// open and still hear nothing (udev in a sandbox), so events are never the
// only way to see a change.
inline constexpr qint64 kPollBackupMs = 2 * 1000;

// Section 2.5 of device-state.md: a finished operation is shown as `done` for
// this long, then dropped.
inline constexpr qint64 kDoneShownMs = 5 * 1000;

// Section 2.7 of device-state.md: a good eject is shown as `released` for this
// long (starting value).
inline constexpr qint64 kReleasedKeepMs = 30 * 1000;

// Section 2.8 of device-state.md: the setting "Mount iPods when they are plugged
// in". On by default; Settings come in v0.7, and until then it is always on.
inline constexpr bool kMountOnArrival = true;

// Section 7.3: the hidden folder of the firmware and the info file inside it.
// The folder is the default one: a model can have its own (`firmware_folder` in
// models/models.toml), and older installs of every model use this one.
inline constexpr QLatin1String kFirmwareFolder(".rockbox");
inline constexpr QLatin1String kFirmwareInfoFile("rockbox-info.txt");

// docs/specs/boot-screen.md section 3.1: the boot logo file, kept by backups
// and updates.
inline constexpr QLatin1String kBootLogoFile("boot-logo.bin");

// Section 7.3: the key of the info file line that names the distribution and
// its own version (D60). Firmware patch 0015 writes it, with the version of
// firmware/version.sh.
inline constexpr QLatin1String kDistributionKey("TacetOS");

// docs/specs/update-channel.md section 10: the channel of the firmware updates.
// The default address is the latest release of the repository, followed by a
// file name; the variable replaces it outside the packaged builds (section 4).
inline constexpr QLatin1String
    kUpdateChannel("https://github.com/vadmyro/TacetOS/releases/latest/download/");
inline constexpr QLatin1String kUpdateChannelVariable("TACETOS_UPDATE_CHANNEL");
inline constexpr QLatin1String kUpdateIndexFile("tacetos-index.json");

// Section 3 and 4 of update-channel.md: the largest index, how long a read may
// get no byte, and how many redirects a read follows.
inline constexpr qint64 kIndexMaxBytes = 64 * 1024;
inline constexpr qint64 kChannelTimeoutMs = 15 * 1000;
inline constexpr int kRedirectMax = 5;

// Section 6 of update-channel.md: the free space kept on top of the download.
inline constexpr qint64 kDownloadMarginBytes = 50 * 1000 * 1000;

// docs/specs/health.md section 11: the two files the firmware writes in its
// folder (patch 0016), the limits of the disk and battery verdicts, and the
// folder of the history in the data folder of the app.
inline constexpr QLatin1String kHealthFile("health.txt");
inline constexpr QLatin1String kChargeFile("charge.txt");
inline constexpr quint32 kReallocatedFailing = 100;
inline constexpr quint32 kPendingFailing = 10;
inline constexpr quint32 kUncorrectableFailing = 10;
inline constexpr int kFullLevel = 95;
inline constexpr int kMinDrop = 30;
inline constexpr qint64 kMinRuntimeS = 60 * 60;
inline constexpr int kMinEstimates = 6;
inline constexpr double kWornRatio = 0.5;
inline constexpr int kHistoryMax = 500;
inline constexpr QLatin1String kHealthFolder("health");

// docs/specs/install-wizard.md section 5.3: the playlist folder at the root of
// the music volume, kept by the backup of "Keep my music".
inline constexpr QLatin1String kPlaylistsFolder("Playlists");

// docs/specs/install-wizard.md section 7.3: what a new install keeps of the old
// firmware folder, and what the backup of an uninstall copies. Compared without
// case, as FAT32 does.
inline constexpr const char *kKeptFirmwareFiles[] = {
    "config.cfg",        "fixed.cfg",          ".resume.cfg",   ".playlist_control",
    "most-recent.bmark", "battery_levels.cfg", "boot-logo.bin",
};
inline constexpr const char *kKeptFirmwareFolders[] = {
    "themes", "wps", "backdrops", "icons", "fonts", "eqs", "recpresets", "rocks.data",
};

// docs/specs/firmware-branding.md section 8.3 step 4: the files whose paths are
// rewritten when the firmware folder moves. The settings files at the root of
// the folder and in its themes folder, and the screen files of its wps folder.
inline constexpr QLatin1String kSettingsFilePattern("*.cfg");
inline constexpr QLatin1String kThemesFolder("themes");
inline constexpr QLatin1String kScreensFolder("wps");
inline constexpr const char *kScreenFilePatterns[] = {"*.wps", "*.sbs", "*.fms"};

} // namespace tacetos::device
