// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "flow.h"
#include "step.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <optional>

// The diagnosis before any write (docs/specs/install-wizard.md section 4).

namespace tacetos::wizard {

enum class Side { Computer, Ipod };

enum class CheckResult { Pass, Warn, Fail, Ask, Skipped };

struct Check
{
    QString id;
    Side side = Side::Computer;
    bool blocking = true;
    CheckResult result = CheckResult::Pass;
    QString message;
    // Section 4.4: the command to type for what is missing on the computer.
    QString command;
    // An action the screen offers for this check (`install-udev-rule`,
    // `mount-volume`).
    QString action;
};

enum class Os { Linux, Windows, MacOs };

struct UdisksInfo
{
    bool present = false;
    bool running = false;
    QString version;
};

// What the hello of the helper said (helper.md section 3.2).
struct HelperInfo
{
    bool found = false;
    bool protocolOk = false;
    bool realWrites = false;
    bool usb = false;
    bool classic = false;
};

struct FolderInfo
{
    QString path;
    bool writable = false;
    bool onIpod = false;
    qint64 freeBytes = -1;
};

enum class Filesystem { Fat32, HfsPlus, Other, Unknown };

// What detection and the helper said about the iPod.
struct IpodFacts
{
    bool helperRead = false;
    QString readError;
    bool modelSupported = false;
    // `method` of read-state agrees with the family of the model.
    bool methodAgrees = false;
    QString serial;
    int capacityGb = 0;
    int otherIpods = 0;
    bool volumeReady = false;
    QString volumeMessage;
    // The disk is there and its volume is not mounted (detection state
    // `no-volume`): the screen offers to mount it.
    bool volumeMountable = false;
    Filesystem filesystem = Filesystem::Unknown;
    qint64 volumeFree = 0;
    qint64 volumeSize = 0;
    // Older iPods: the OSOS entry was found, and the state of read-state.
    bool firmwarePartitionOk = false;
    QString bootloaderState;
    QString inconsistentMessage;
    // Classic: `VisibleBuildID` of the device sheet, empty when not read.
    QString appleVersion;
};

// The result of the checks of a package (section 7.1). `code` is empty when
// the package can be used.
struct PackageCheck
{
    QString code;
    QString message;
    qint64 unpackedBytes = 0;
    // The sha256 of the manifest, kept in the progress file of a flow.
    QString manifestSha256;
};

// Everything the checks are made from.
struct DiagnoseInput
{
    Os os = Os::Linux;
    QString osRelease;
    UdisksInfo udisks;
    HelperInfo helper;
    bool udevRulePresent = false;
    FolderInfo backup;
    qint64 backupNeedBytes = 0;
    Family family = Family::Partition;
    FlowKind kind = FlowKind::Install;
    InstallPath path = InstallPath::None;
    IpodFacts ipod;
    PackageCheck package;
    qint64 firmwareNeedBytes = 0;
    std::optional<bool> batteryCharged;
};

// Section 4: the checks, in their order. A pure function.
QList<Check> diagnose(const DiagnoseInput &input);

// Section 4.1 rule 4: false while a blocking check fails or waits.
bool canGoOn(const QList<Check> &checks);

// The first blocking check that stops the flow, or null.
const Check *firstBlocking(const QList<Check> &checks);

// A size for a message: "92 GB", "4.5 GB", "40 MB", in decimal units as the
// systems show free space.
QString sizeText(qint64 bytes);

// Section 6.4: why an iPod Classic formatted for Mac cannot be installed yet.
QString macClassicRefusal();

// Section 4.4: the command for udisks2, from the text of the system release
// file. Empty when the system is not known.
QString udisksCommand(const QString &osRelease, bool notRunning);

// Section 4.2: true when `version` is at least `oldest` (numbers between dots).
bool versionAtLeast(const QString &version, const QString &oldest);

// Section 4.4: reads `etc/os-release`, then `usr/lib/os-release`, under `root`
// (`/` on a real system). Empty when there is none.
QString readOsRelease(const QString &root);

// Section 4.2: what the backup folder is. `ipodRoots` are the mount points of
// the detected iPods.
FolderInfo folderInfo(const QString &path, const QStringList &ipodRoots);

// Section 4.1 rule 6: one interface for every source, so that the tests give
// their own answers.
class DiagnoseSources
{
public:
    virtual ~DiagnoseSources() = default;
    virtual Os os() const = 0;
    virtual QString osRelease() const = 0;
    virtual UdisksInfo udisks() const = 0;
    virtual HelperInfo helper() const = 0;
    virtual bool udevRulePresent() const = 0;
    virtual FolderInfo backupFolder() const = 0;
    virtual qint64 backupNeedBytes(InstallPath path) const = 0;
    virtual IpodFacts ipod() const = 0;
    virtual PackageCheck package(const QString &manifestPath) const = 0;
    virtual qint64 firmwareNeedBytes(const PackageCheck &package) const = 0;
};

// The step `diagnose`: gathers the sources, asks the battery question, and
// fails with `check-blocking` and the id of the check while one stops the
// flow. On an older iPod it keeps the state read by the helper as a fact.
class DiagnoseStep : public Step
{
public:
    explicit DiagnoseStep(const DiagnoseSources &sources) : m_sources(sources) {}
    StepOutcome run(StepContext &context) override;

    // The checks of the last run, for the screen.
    QList<Check> checks() const { return m_checks; }

private:
    const DiagnoseSources &m_sources;
    QList<Check> m_checks;
};

} // namespace tacetos::wizard

Q_DECLARE_METATYPE(tacetos::wizard::Check)
