// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The diagnosis (docs/specs/install-wizard.md section 4), with fake sources:
// nothing of the real system is asked.

#include "diagnose.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <functional>

using namespace tacetos::wizard;

namespace {

// Every check passes: a Classic on Linux, keep my music, battery answered.
DiagnoseInput goodClassic()
{
    DiagnoseInput in;
    in.os = Os::Linux;
    in.osRelease = QStringLiteral("ID=fedora\n");
    in.udisks = {true, true, QStringLiteral("2.10.1")};
    in.helper = {true, true, true, true, true};
    in.udevRulePresent = true;
    in.backup = {QStringLiteral("/home/anna/Documents/TacetOS Backups"), true, false,
                 100LL * 1000 * 1000 * 1000};
    in.backupNeedBytes = 50LL * 1000 * 1000;
    in.family = Family::Dfu;
    in.kind = FlowKind::Install;
    in.path = InstallPath::Keep;
    IpodFacts &ipod = in.ipod;
    ipod.helperRead = true;
    ipod.modelSupported = true;
    ipod.methodAgrees = true;
    ipod.serial = QStringLiteral("8K1234ABCD");
    ipod.capacityGb = 120;
    ipod.volumeReady = true;
    ipod.filesystem = Filesystem::Fat32;
    ipod.volumeFree = 2LL * 1000 * 1000 * 1000;
    ipod.volumeSize = 120LL * 1000 * 1000 * 1000;
    ipod.appleVersion = QStringLiteral("2.0.1");
    in.firmwareNeedBytes = 40LL * 1000 * 1000;
    in.batteryCharged = true;
    return in;
}

DiagnoseInput goodOlder()
{
    DiagnoseInput in = goodClassic();
    in.family = Family::Partition;
    in.ipod.appleVersion.clear();
    in.ipod.firmwarePartitionOk = true;
    in.ipod.bootloaderState = QStringLiteral("clean");
    return in;
}

const Check *find(const QList<Check> &checks, const QString &id)
{
    for (const Check &check : checks) {
        if (check.id == id)
            return &check;
    }
    return nullptr;
}

QStringList ids(const QList<Check> &checks)
{
    QStringList list;
    for (const Check &check : checks)
        list << check.id;
    return list;
}

using Change = std::function<void(DiagnoseInput &)>;

// A step context that answers the battery question.
class FakeContext : public StepContext
{
public:
    FlowShape flowShape;
    std::optional<bool> battery;
    int asked = 0;
    QJsonObject facts;

    QString serial() const override { return QStringLiteral("8K1234ABCD"); }
    QString model() const override { return QStringLiteral("classic-6g"); }
    FlowShape shape() const override { return flowShape; }
    bool isResume() const override { return false; }
    bool stopRequested() const override { return false; }
    void progress(const QString &, qint64, qint64) override {}
    UserAnswer waitForUser(WaitKind kind, const QJsonObject &) override
    {
        if (kind != WaitKind::Battery)
            return {true, false, QString()};
        ++asked;
        if (!battery)
            return {true, false, QString()};
        return {false, *battery, QString()};
    }
    QJsonValue fact(const QString &, const QString &key) const override { return facts.value(key); }
    void setFact(const QString &key, const QJsonValue &value) override { facts.insert(key, value); }
    QString backupFolder() const override { return QString(); }
    void setBackupFolder(const QString &) override {}
    QString packageManifest() const override { return QStringLiteral("/p/m.json"); }
    QString packageSha256() const override { return flowSha256; }

    QString flowSha256;
};

class FakeSources : public DiagnoseSources
{
public:
    DiagnoseInput in = goodOlder();
    QString askedPackage;

    Os os() const override { return in.os; }
    QString osRelease() const override { return in.osRelease; }
    UdisksInfo udisks() const override { return in.udisks; }
    HelperInfo helper() const override { return in.helper; }
    bool udevRulePresent() const override { return in.udevRulePresent; }
    FolderInfo backupFolder() const override { return in.backup; }
    qint64 backupNeedBytes(InstallPath) const override { return in.backupNeedBytes; }
    IpodFacts ipod() const override { return in.ipod; }
    PackageCheck package(const QString &manifestPath) const override
    {
        const_cast<FakeSources *>(this)->askedPackage = manifestPath;
        return in.package;
    }
    qint64 firmwareNeedBytes(const PackageCheck &) const override { return in.firmwareNeedBytes; }
};

} // namespace

Q_DECLARE_METATYPE(Change)

class TstWizardDiagnose : public QObject
{
    Q_OBJECT

private slots:
    void allPass();
    void order();
    void rows_data();
    void rows();
    void ipodSkippedWithoutHelper();
    void uninstallLeavesOut();
    void commands_data();
    void commands();
    void versions();
    void osReleaseFile();
    void folder();
    void stepAsksTheBattery();
    void stepStopsOnABlockingCheck();
    void stepSeesAChangedPackage();
};

void TstWizardDiagnose::allPass()
{
    for (const DiagnoseInput &in : {goodClassic(), goodOlder()}) {
        const QList<Check> checks = diagnose(in);
        for (const Check &check : checks)
            QVERIFY2(check.result == CheckResult::Pass, qPrintable(check.id));
        QVERIFY(canGoOn(checks));
    }
}

void TstWizardDiagnose::order()
{
    // Section 4.1: the computer first, then the iPod, in the order of the tables.
    QCOMPARE(ids(diagnose(goodClassic())).join(QLatin1Char(' ')),
             QStringLiteral("system helper writes udisks classic-build udev-rule backup-folder "
                            "backup-space model serial single-ipod volume format apple-firmware "
                            "package firmware-room battery"));
    QCOMPARE(ids(diagnose(goodOlder())).join(QLatin1Char(' ')),
             QStringLiteral("system helper writes udisks backup-folder backup-space model serial "
                            "volume format apple-firmware bootloader-state package firmware-room "
                            "battery"));
    // Not on Linux: no udisks2, no udev rule.
    DiagnoseInput mac = goodClassic();
    mac.os = Os::MacOs;
    QVERIFY(!find(diagnose(mac), QStringLiteral("udisks")));
    QVERIFY(!find(diagnose(mac), QStringLiteral("udev-rule")));
}

void TstWizardDiagnose::rows_data()
{
    QTest::addColumn<bool>("classic");
    QTest::addColumn<Change>("change");
    QTest::addColumn<QString>("id");
    QTest::addColumn<int>("result");
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("command");

    const int F = int(CheckResult::Fail);
    const int W = int(CheckResult::Warn);
    const int A = int(CheckResult::Ask);
    const QString none;

    // Section 4.2, the computer.
    QTest::newRow("windows") << true << Change([](DiagnoseInput &i) { i.os = Os::Windows; })
                             << "system" << F << "later version" << none;
    QTest::newRow("no helper") << true << Change([](DiagnoseInput &i) { i.helper.found = false; })
                               << "helper" << F << "helper program is missing" << none;
    QTest::newRow("helper of another version")
        << true << Change([](DiagnoseInput &i) { i.helper.protocolOk = false; }) << "helper" << F
        << "does not match" << none;
    QTest::newRow("dev build") << true
                               << Change([](DiagnoseInput &i) { i.helper.realWrites = false; })
                               << "writes" << W << "development build" << none;
    QTest::newRow("no udisks2") << true
                                << Change([](DiagnoseInput &i) { i.udisks.present = false; })
                                << "udisks" << F << "udisks2 is missing"
                                << "sudo dnf install udisks2";
    QTest::newRow("udisks2 stopped")
        << true << Change([](DiagnoseInput &i) { i.udisks.running = false; }) << "udisks" << F
        << "not running" << "sudo systemctl enable --now udisks2";
    QTest::newRow("udisks2 too old")
        << true << Change([](DiagnoseInput &i) { i.udisks.version = QStringLiteral("2.6.0"); })
        << "udisks" << F << "too old (2.6.0, 2.7.3" << "sudo dnf install udisks2";
    QTest::newRow("no usb in the build")
        << true << Change([](DiagnoseInput &i) { i.helper.usb = false; }) << "classic-build" << F
        << "not available" << none;
    QTest::newRow("no Classic parts")
        << true << Change([](DiagnoseInput &i) { i.helper.classic = false; }) << "classic-build"
        << F << "not available" << none;
    QTest::newRow("no udev rule") << true
                                  << Change([](DiagnoseInput &i) { i.udevRulePresent = false; })
                                  << "udev-rule" << F << "USB permission" << none;
    QTest::newRow("backup folder read only")
        << true << Change([](DiagnoseInput &i) { i.backup.writable = false; }) << "backup-folder"
        << F << "Cannot write" << none;
    QTest::newRow("backup folder on the iPod")
        << true << Change([](DiagnoseInput &i) { i.backup.onIpod = true; }) << "backup-folder" << F
        << "is on an iPod" << none;
    QTest::newRow("backup folder full") << true << Change([](DiagnoseInput &i) {
        i.backupNeedBytes = 92LL * 1000 * 1000 * 1000;
        i.backup.freeBytes = 40LL * 1000 * 1000 * 1000;
    }) << "backup-space" << F << "92 GB needed, 40 GB free"
                                        << none;

    // Section 4.3, the iPod.
    QTest::newRow("not read") << true << Change([](DiagnoseInput &i) {
        i.ipod.helperRead = false;
        i.ipod.readError = QStringLiteral("permission-denied");
    }) << "model" << F << "could not be read (permission-denied)"
                              << none;
    QTest::newRow("unsupported") << true
                                 << Change([](DiagnoseInput &i) { i.ipod.modelSupported = false; })
                                 << "model" << F << "does not support" << none;
    QTest::newRow("method disagrees")
        << true << Change([](DiagnoseInput &i) { i.ipod.methodAgrees = false; }) << "model" << F
        << "does not support" << none;
    QTest::newRow("no serial") << true << Change([](DiagnoseInput &i) { i.ipod.serial.clear(); })
                               << "serial" << F << "serial number" << none;
    QTest::newRow("two iPods") << true << Change([](DiagnoseInput &i) { i.ipod.otherIpods = 1; })
                               << "single-ipod" << F << "Unplug the other iPod" << none;
    QTest::newRow("volume not ready") << true << Change([](DiagnoseInput &i) {
        i.ipod.volumeReady = false;
        i.ipod.volumeMessage = QStringLiteral("The iPod is not mounted");
    }) << "volume" << F << "not mounted"
                                      << none;
    QTest::newRow("Mac, clean") << false << Change([](DiagnoseInput &i) {
        i.ipod.filesystem = Filesystem::HfsPlus;
        i.path = InstallPath::Clean;
    }) << "format" << W << "formatted for Mac"
                                << none;
    QTest::newRow("Mac, keep") << false << Change([](DiagnoseInput &i) {
        i.ipod.filesystem = Filesystem::HfsPlus;
        i.path = InstallPath::Keep;
    }) << "format" << F << "erases the iPod"
                               << none;
    QTest::newRow("Classic, Mac") << true << Change([](DiagnoseInput &i) {
        i.ipod.filesystem = Filesystem::HfsPlus;
        i.path = InstallPath::Clean;
    }) << "format" << F << "cannot change yet"
                                  << none;
    QTest::newRow("other file system")
        << true << Change([](DiagnoseInput &i) { i.ipod.filesystem = Filesystem::Other; })
        << "format" << F << "cannot be used" << none;
    QTest::newRow("older, no Apple firmware")
        << false << Change([](DiagnoseInput &i) { i.ipod.firmwarePartitionOk = false; })
        << "apple-firmware" << F << "Restore it" << none;
    QTest::newRow("Classic, version not read")
        << true << Change([](DiagnoseInput &i) { i.ipod.appleVersion.clear(); }) << "apple-firmware"
        << F << "could not be read" << none;
    QTest::newRow("Classic, old version")
        << true << Change([](DiagnoseInput &i) { i.ipod.appleVersion = QStringLiteral("1.0.2"); })
        << "apple-firmware" << F << "version 2.0.1 needed, 1.0.2 found" << none;
    QTest::newRow("Classic 160, three revisions") << true << Change([](DiagnoseInput &i) {
        i.ipod.capacityGb = 160;
        i.ipod.appleVersion = QStringLiteral("2.0.1");
    }) << "apple-firmware" << F << "1.1.2 or 2.0.4 or 2.0.5 needed"
                                                  << none;
    QTest::newRow("inconsistent") << false << Change([](DiagnoseInput &i) {
        i.ipod.bootloaderState = QStringLiteral("inconsistent");
    }) << "bootloader-state" << F << "not as expected"
                                  << none;
    QTest::newRow("state not read")
        << false << Change([](DiagnoseInput &i) { i.ipod.bootloaderState.clear(); })
        << "bootloader-state" << F << "could not be read" << none;
    QTest::newRow("package for another model") << true << Change([](DiagnoseInput &i) {
        i.package = {QStringLiteral("package-wrong-model"),
                     QStringLiteral("This package is for another iPod model"), 0, QString()};
    }) << "package" << F << "another iPod model"
                                               << none;
    QTest::newRow("no room on the iPod")
        << true << Change([](DiagnoseInput &i) { i.ipod.volumeFree = 12LL * 1000 * 1000; })
        << "firmware-room" << F << "40 MB needed, 12 MB free" << none;
    QTest::newRow("battery not answered")
        << true << Change([](DiagnoseInput &i) { i.batteryCharged.reset(); }) << "battery" << A
        << "Is the iPod charged" << none;
    QTest::newRow("battery empty")
        << true << Change([](DiagnoseInput &i) { i.batteryCharged = false; }) << "battery" << F
        << "Charge the iPod" << none;
}

void TstWizardDiagnose::rows()
{
    QFETCH(bool, classic);
    QFETCH(Change, change);
    QFETCH(QString, id);
    QFETCH(int, result);
    QFETCH(QString, message);
    QFETCH(QString, command);

    DiagnoseInput in = classic ? goodClassic() : goodOlder();
    change(in);
    const QList<Check> checks = diagnose(in);
    const Check *check = find(checks, id);
    QVERIFY(check);
    QCOMPARE(int(check->result), result);
    QVERIFY2(check->message.contains(message), qPrintable(check->message));
    QCOMPARE(check->command, command);
    if (id == QLatin1String("udev-rule"))
        QCOMPARE(check->action, QStringLiteral("install-udev-rule"));
    // Section 4.1 rule 4: a warning lets the flow go on, nothing else does.
    QCOMPARE(canGoOn(checks), result == int(CheckResult::Warn));
    if (result != int(CheckResult::Warn))
        QCOMPARE(firstBlocking(checks)->id, id);
}

void TstWizardDiagnose::ipodSkippedWithoutHelper()
{
    DiagnoseInput in = goodClassic();
    in.helper.found = false;
    const QList<Check> checks = diagnose(in);
    for (const Check &check : checks) {
        if (check.side == Side::Ipod)
            QCOMPARE(check.result, CheckResult::Skipped);
    }
    QCOMPARE(find(checks, QStringLiteral("classic-build"))->result, CheckResult::Skipped);
    QCOMPARE(firstBlocking(checks)->id, QStringLiteral("helper"));

    // Section 4.1 rule 3: a check that needs a failed one is skipped.
    in = goodOlder();
    in.ipod.modelSupported = false;
    const QList<Check> older = diagnose(in);
    QCOMPARE(find(older, QStringLiteral("bootloader-state"))->result, CheckResult::Skipped);
    QCOMPARE(find(older, QStringLiteral("package"))->result, CheckResult::Skipped);
    QCOMPARE(find(older, QStringLiteral("serial"))->result, CheckResult::Pass);
}

void TstWizardDiagnose::uninstallLeavesOut()
{
    // Section 4.3: no package, no room, no Apple version for the Classic, and a
    // Mac volume is fine, since only the kept files are read.
    DiagnoseInput in = goodClassic();
    in.kind = FlowKind::Uninstall;
    in.path = InstallPath::None;
    in.ipod.appleVersion = QStringLiteral("1.0.0");
    in.ipod.filesystem = Filesystem::HfsPlus;
    const QList<Check> checks = diagnose(in);
    QVERIFY(!find(checks, QStringLiteral("package")));
    QVERIFY(!find(checks, QStringLiteral("firmware-room")));
    QVERIFY(!find(checks, QStringLiteral("apple-firmware")));
    QVERIFY(canGoOn(checks));

    in = goodOlder();
    in.kind = FlowKind::Uninstall;
    in.path = InstallPath::None;
    QVERIFY(find(diagnose(in), QStringLiteral("apple-firmware")));
}

void TstWizardDiagnose::commands_data()
{
    QTest::addColumn<QString>("release");
    QTest::addColumn<QString>("command");
    QTest::newRow("Fedora") << "NAME=\"Fedora Linux\"\nID=fedora\nVERSION_ID=44\n"
                            << "sudo dnf install udisks2";
    QTest::newRow("Ubuntu") << "NAME=\"Ubuntu\"\nID=ubuntu\nID_LIKE=debian\n"
                            << "sudo apt install udisks2";
    QTest::newRow("Debian") << "PRETTY_NAME=\"Debian GNU/Linux 12\"\nID=debian\n"
                            << "sudo apt install udisks2";
    QTest::newRow("Arch") << "NAME=\"Arch Linux\"\nID=arch\n" << "sudo pacman -S udisks2";
    QTest::newRow("Mint, by ID_LIKE") << "ID=linuxmint\nID_LIKE=\"ubuntu debian\"\n"
                                      << "sudo apt install udisks2";
    QTest::newRow("Manjaro, by ID_LIKE") << "ID=manjaro\nID_LIKE=arch\n"
                                         << "sudo pacman -S udisks2";
    QTest::newRow("Rocky, quoted") << "ID=\"rocky\"\nID_LIKE=\"rhel centos fedora\"\n"
                                   << "sudo dnf install udisks2";
    QTest::newRow("unknown") << "ID=gentoo\n" << QString();
    QTest::newRow("no file") << QString() << QString();
}

void TstWizardDiagnose::commands()
{
    QFETCH(QString, release);
    QFETCH(QString, command);
    QCOMPARE(udisksCommand(release, false), command);
    QCOMPARE(udisksCommand(release, true), QStringLiteral("sudo systemctl enable --now udisks2"));
}

void TstWizardDiagnose::versions()
{
    QVERIFY(versionAtLeast(QStringLiteral("2.7.3"), QStringLiteral("2.7.3")));
    QVERIFY(versionAtLeast(QStringLiteral("2.10.0"), QStringLiteral("2.7.3")));
    QVERIFY(versionAtLeast(QStringLiteral("3"), QStringLiteral("2.7.3")));
    QVERIFY(!versionAtLeast(QStringLiteral("2.7"), QStringLiteral("2.7.3")));
    QVERIFY(!versionAtLeast(QStringLiteral("2.6.9"), QStringLiteral("2.7.3")));
    QVERIFY(!versionAtLeast(QString(), QStringLiteral("2.7.3")));
    QVERIFY(!versionAtLeast(QStringLiteral("2.x"), QStringLiteral("2.7.3")));
}

void TstWizardDiagnose::osReleaseFile()
{
    QTemporaryDir root;
    QCOMPARE(readOsRelease(root.path()), QString());
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("usr/lib")));
    QFile lib(root.filePath(QStringLiteral("usr/lib/os-release")));
    QVERIFY(lib.open(QIODevice::WriteOnly));
    lib.write("ID=arch\n");
    lib.close();
    QCOMPARE(readOsRelease(root.path()), QStringLiteral("ID=arch\n"));
    // The file of /etc comes first.
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("etc")));
    QFile etc(root.filePath(QStringLiteral("etc/os-release")));
    QVERIFY(etc.open(QIODevice::WriteOnly));
    etc.write("ID=fedora\n");
    etc.close();
    QCOMPARE(readOsRelease(root.path()), QStringLiteral("ID=fedora\n"));
}

void TstWizardDiagnose::folder()
{
    QTemporaryDir dir;
    QTemporaryDir ipod;
    QVERIFY(!folderInfo(QStringLiteral("relative/folder"), {}).writable);

    // A folder that does not exist yet is judged by its nearest parent.
    const FolderInfo later = folderInfo(dir.filePath(QStringLiteral("TacetOS Backups/x")), {});
    QVERIFY(later.writable);
    QVERIFY(!later.onIpod);
    QVERIFY(later.freeBytes > 0);

    QVERIFY(folderInfo(ipod.filePath(QStringLiteral("backups")), {ipod.path()}).onIpod);
    QVERIFY(folderInfo(ipod.path(), {ipod.path()}).onIpod);
}

void TstWizardDiagnose::stepAsksTheBattery()
{
    FakeSources sources;
    sources.in.batteryCharged.reset();
    FakeContext context;
    context.flowShape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
    context.battery = true;
    DiagnoseStep step(sources);
    QCOMPARE(step.run(context).kind, StepOutcome::Kind::Done);
    QCOMPARE(context.asked, 1);
    QCOMPARE(sources.askedPackage, QStringLiteral("/p/m.json"));
    QCOMPARE(context.facts.value(QStringLiteral("state")).toString(), QStringLiteral("clean"));
    QVERIFY(canGoOn(step.checks()));

    context.battery = false;
    const StepOutcome no = step.run(context);
    QCOMPARE(no.kind, StepOutcome::Kind::Failed);
    QCOMPARE(no.code, QStringLiteral("check-blocking"));
    QCOMPARE(no.detail, QStringLiteral("battery"));

    context.battery.reset();
    QCOMPARE(step.run(context).kind, StepOutcome::Kind::Cancelled);
}

void TstWizardDiagnose::stepStopsOnABlockingCheck()
{
    FakeSources sources;
    sources.in.udisks.present = false;
    FakeContext context;
    context.flowShape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
    DiagnoseStep step(sources);
    const StepOutcome outcome = step.run(context);
    QCOMPARE(outcome.code, QStringLiteral("check-blocking"));
    QCOMPARE(outcome.detail, QStringLiteral("udisks"));
    // A failure on the computer never asks the battery question.
    QCOMPARE(context.asked, 0);
}

// Section 7.1: the package of the progress file is the one the flow started with.
void TstWizardDiagnose::stepSeesAChangedPackage()
{
    FakeSources sources;
    sources.in.package.manifestSha256 = QStringLiteral("aaaa");
    FakeContext context;
    context.battery = true;
    context.flowShape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
    context.flowSha256 = QStringLiteral("aaaa");
    DiagnoseStep same(sources);
    QCOMPARE(same.run(context).kind, StepOutcome::Kind::Done);
    QCOMPARE(sources.askedPackage, QStringLiteral("/p/m.json"));

    context.flowSha256 = QStringLiteral("bbbb");
    DiagnoseStep changed(sources);
    const StepOutcome outcome = changed.run(context);
    QCOMPARE(outcome.code, QStringLiteral("check-blocking"));
    QCOMPARE(outcome.detail, QStringLiteral("package"));
}

QTEST_GUILESS_MAIN(TstWizardDiagnose)
#include "tst_wizard_diagnose.moc"
