// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md section 11: the steps `verify` and `finish`. A
// temporary folder plays the music volume, the package is made by the test, and
// the helper only answers `read-state`.

#include "constants.h"
#include "helperconstants.h"
#include "testpackage.h"
#include "verify.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::wizard;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
namespace device = tacetos::device;
namespace helper = tacetos::helper;

namespace {

const QString kTarget = QStringLiteral("ipod6g");
const QString kSerial = QStringLiteral("8K7VERIFY");

bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// The files of the firmware folder in the package of the test.
QMap<QString, QByteArray> firmwareFiles(const QString &target)
{
    return {{QStringLiteral("a.txt"), "a"},
            {QStringLiteral("b/c"), "c"},
            {QString(device::kFirmwareInfoFile), "Target: " + target.toUtf8() + "\n"}};
}

// The sha256 of every file under a folder, hidden ones included.
QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            files.insert(QDir(root).relativeFilePath(path),
                         QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256));
    }
    return files;
}

class StateOnly : public HelperCommands
{
public:
    HelperAnswer call(const QString &cmd, const QJsonObject &, const HelperWatch &) override
    {
        calls << cmd;
        if (!failCode.isEmpty())
            return {failCode, QStringLiteral("no answer"), {}};
        return {{}, {}, QJsonObject{{"state", state}}};
    }

    QString state = QStringLiteral("installed");
    QString failCode;
    QStringList calls;
};

class Sources : public VerifySources
{
public:
    HelperCommands &helper() const override { return commands; }
    QString volumeRoot() const override { return volume; }
    QString target() const override { return kTarget; }
    // The first `readyAfter` readings show the iPod still waiting, as after a
    // restart, before detection has its volume.
    device::DetectedIpod detected() const override
    {
        ++readings;
        if (readings <= readyAfter) {
            device::DetectedIpod waiting = ipod;
            waiting.state = device::IpodState::Waiting;
            return waiting;
        }
        return ipod;
    }
    qint64 readyWaitMs() const override { return readyWait; }

    mutable StateOnly commands;
    QString volume;
    device::DetectedIpod ipod;
    int readyAfter = 0;
    qint64 readyWait = 0;
    mutable int readings = 0;
};

class Context : public StepContext
{
public:
    QString serial() const override { return kSerial; }
    QString model() const override { return QStringLiteral("model"); }
    FlowShape shape() const override { return flow; }
    bool isResume() const override { return false; }
    bool stopRequested() const override { return false; }
    void progress(const QString &, qint64, qint64) override {}
    UserAnswer waitForUser(WaitKind, const QJsonObject &) override { return {true, false, {}}; }
    QJsonValue fact(const QString &step, const QString &key) const override
    {
        return others.value(step).toObject().value(key);
    }
    void setFact(const QString &key, const QJsonValue &value) override { facts.insert(key, value); }
    QString backupFolder() const override { return folder; }
    void setBackupFolder(const QString &path) override { folder = path; }
    QString packageManifest() const override { return package.manifest; }
    QString packageSha256() const override { return package.sha256; }

    FlowShape flow;
    TestPackage package;
    QString folder;
    QJsonObject others;
    QJsonObject facts;
};

} // namespace

class TstWizardVerify : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void installOlderIpodPasses();
    void installClassicPasses();
    void installFileDiffers_data();
    void installFileDiffers();
    void installWrongInfoTarget();
    void installBootloaderNotRead_data();
    void installBootloaderNotRead();
    void installClassicRunsApple();
    void installPackageGone();
    void notReady_data();
    void notReady();
    void waitsUntilReady();
    void waitsThenGivesUp();

    void uninstallOlderIpodPasses();
    void uninstallClassicPasses();
    void uninstallFolderStillThere();
    void uninstallOlderIpodStillInstalled();
    void uninstallClassicRunsTacetos();
    void helperFails();

    void writesNothing();

    void finishInstall();
    void finishUninstall();

private:
    QString newFolder(const QString &name);
    void install(Family family);
    void uninstall(Family family);
    StepOutcome verify() { return VerifyStep(m_sources).run(m_context); }
    QString firmwareFolder() const
    {
        return m_sources.volume + QLatin1Char('/') + QString(device::kFirmwareFolder);
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    int m_count = 0;
    Sources m_sources;
    Context m_context;
};

QString TstWizardVerify::newFolder(const QString &name)
{
    const QString path = m_tmp->filePath(QStringLiteral("%1-%2").arg(name).arg(++m_count));
    QDir().mkpath(path);
    return path;
}

void TstWizardVerify::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    m_sources = Sources();
    m_context = Context();
    m_sources.volume = newFolder(QStringLiteral("volume"));
    m_sources.ipod.serial = kSerial;
    m_sources.ipod.state = device::IpodState::Ready;
    QVERIFY(put(m_sources.volume + QStringLiteral("/music/a.mp3"), "not music"));
}

// A volume as the step `firmware` leaves it, and the package it came from.
void TstWizardVerify::install(Family family)
{
    m_context.flow = {family, FlowKind::Install, InstallPath::Keep, false};
    m_context.package = makeTestPackage(newFolder(QStringLiteral("package")), kTarget, "boot",
                                        firmwareFiles(kTarget));
    const QMap<QString, QByteArray> files = firmwareFiles(kTarget);
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        QVERIFY(put(firmwareFolder() + QLatin1Char('/') + it.key(), it.value()));
    // A file of the user's, kept by the install: not in the package.
    QVERIFY(put(firmwareFolder() + QStringLiteral("/config.cfg"), "volume: 3\n"));
    m_sources.commands.state = QStringLiteral("installed");
    m_sources.ipod.running =
        family == Family::Dfu ? device::RunningFirmware::Other : device::RunningFirmware::Unknown;
}

void TstWizardVerify::uninstall(Family family)
{
    m_context.flow = {family, FlowKind::Uninstall, InstallPath::None, false};
    m_sources.commands.state = QStringLiteral("clean");
    m_sources.ipod.running =
        family == Family::Dfu ? device::RunningFirmware::Apple : device::RunningFirmware::Unknown;
}

void TstWizardVerify::installOlderIpodPasses()
{
    install(Family::Partition);
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);
    QCOMPARE(m_sources.commands.calls, QStringList{QString(helper::cmd::readState)});
}

// The Classic is read from detection, never from the helper.
void TstWizardVerify::installClassicPasses()
{
    install(Family::Dfu);
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);
    QVERIFY(m_sources.commands.calls.isEmpty());
}

void TstWizardVerify::installFileDiffers_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("remove");
    QTest::newRow("changed") << QStringLiteral("a.txt") << false;
    QTest::newRow("missing") << QStringLiteral("b/c") << true;
}

void TstWizardVerify::installFileDiffers()
{
    QFETCH(QString, path);
    QFETCH(bool, remove);
    install(Family::Partition);
    const QString file = firmwareFolder() + QLatin1Char('/') + path;
    if (remove)
        QVERIFY(QFile::remove(file));
    else
        QVERIFY(put(file, "x"));
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.contains(QLatin1String("a file of the firmware")));
}

// The files match the package, but the package is not for this model: the info
// file read from the volume says so.
void TstWizardVerify::installWrongInfoTarget()
{
    install(Family::Partition);
    m_context.package = makeTestPackage(newFolder(QStringLiteral("package")), kTarget, "boot",
                                        firmwareFiles(QStringLiteral("ipodvideo")));
    QVERIFY(put(firmwareFolder() + QLatin1Char('/') + QString(device::kFirmwareInfoFile),
                "Target: ipodvideo\n"));
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.contains(QLatin1String("not for this model")));
}

void TstWizardVerify::installBootloaderNotRead_data()
{
    QTest::addColumn<QString>("state");
    QTest::newRow("clean") << QStringLiteral("clean");
    QTest::newRow("inconsistent") << QStringLiteral("inconsistent");
}

void TstWizardVerify::installBootloaderNotRead()
{
    QFETCH(QString, state);
    install(Family::Partition);
    m_sources.commands.state = state;
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.endsWith(state));
}

// The Classic under Apple's firmware: a failure, unless `classic-bootloader` saw it
// come back under TacetOS in this flow (double boot, Menu held at power on since).
void TstWizardVerify::installClassicRunsApple()
{
    install(Family::Dfu);
    m_sources.ipod.running = device::RunningFirmware::Apple;
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.contains(QLatin1String("TacetOS")));

    m_context.others.insert(QString(step::classicBootloader), QJsonObject{{"returned", "tacetos"}});
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);

    // A flow from before the fact: the install record of the step says the same.
    m_context.others.insert(QString(step::classicBootloader),
                            QJsonObject{{"record", "/backups/S/install-record.json"}});
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);

    // Neither: Apple's firmware alone does not tell.
    m_context.others.insert(QString(step::classicBootloader), QJsonObject{});
    QCOMPARE(verify().code, QString(error::verifyFailed));

    // An unknown firmware never passes.
    m_context.others.insert(QString(step::classicBootloader), QJsonObject{{"returned", "tacetos"}});
    m_sources.ipod.running = device::RunningFirmware::Unknown;
    QCOMPARE(verify().code, QString(error::verifyFailed));
}

// Section 7.1: the package of the flow, read again; gone, it fails with its own
// code and not as a difference on the iPod.
void TstWizardVerify::installPackageGone()
{
    install(Family::Partition);
    QVERIFY(QFile::remove(m_context.package.manifest));
    const StepOutcome out = verify();
    QCOMPARE(out.kind, StepOutcome::Kind::Failed);
    QVERIFY(out.code != QString(error::verifyFailed));
    QVERIFY(m_sources.commands.calls.isEmpty());
}

void TstWizardVerify::notReady_data()
{
    QTest::addColumn<bool>("gone");
    QTest::addColumn<bool>("noVolume");
    QTest::newRow("gone") << true << false;
    QTest::newRow("not-ready") << false << false;
    QTest::newRow("no-volume") << false << true;
}

void TstWizardVerify::notReady()
{
    QFETCH(bool, gone);
    QFETCH(bool, noVolume);
    install(Family::Partition);
    if (gone)
        m_sources.ipod = device::DetectedIpod();
    else if (!noVolume)
        m_sources.ipod.state = device::IpodState::Waiting;
    if (noVolume)
        m_sources.volume.clear();
    QCOMPARE(verify().code, QString(error::verifyFailed));
    QVERIFY(m_sources.commands.calls.isEmpty());
}

// The Classic back from its restart: detection sees it before its volume is mounted
// (6 s on the real one). The step waits for `ready`, then reads.
void TstWizardVerify::waitsUntilReady()
{
    install(Family::Dfu);
    m_sources.readyAfter = 3;
    m_sources.readyWait = 10000;
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);
    QCOMPARE(m_sources.readings, 4);
}

// Never ready within the wait: the step fails as before, without a command.
void TstWizardVerify::waitsThenGivesUp()
{
    install(Family::Dfu);
    m_sources.readyAfter = 1000000;
    m_sources.readyWait = 300;
    QElapsedTimer clock;
    clock.start();
    QCOMPARE(verify().code, QString(error::verifyFailed));
    QVERIFY(clock.elapsed() >= 300);
    QVERIFY(m_sources.readings > 1);
    QVERIFY(m_sources.commands.calls.isEmpty());
}

void TstWizardVerify::uninstallOlderIpodPasses()
{
    uninstall(Family::Partition);
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);
}

void TstWizardVerify::uninstallClassicPasses()
{
    uninstall(Family::Dfu);
    QCOMPARE(verify().kind, StepOutcome::Kind::Done);
    QVERIFY(m_sources.commands.calls.isEmpty());
}

void TstWizardVerify::uninstallFolderStillThere()
{
    uninstall(Family::Partition);
    QVERIFY(put(firmwareFolder() + QStringLiteral("/config.cfg"), "volume: 3\n"));
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.contains(QLatin1String("still there")));
}

void TstWizardVerify::uninstallOlderIpodStillInstalled()
{
    uninstall(Family::Partition);
    m_sources.commands.state = QStringLiteral("installed");
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.endsWith(QLatin1String("installed")));
}

void TstWizardVerify::uninstallClassicRunsTacetos()
{
    uninstall(Family::Dfu);
    m_sources.ipod.running = device::RunningFirmware::Other;
    const StepOutcome out = verify();
    QCOMPARE(out.code, QString(error::verifyFailed));
    QVERIFY(out.detail.contains(QLatin1String("Apple")));
}

// The helper cannot read: its own code, not a difference on the iPod.
void TstWizardVerify::helperFails()
{
    uninstall(Family::Partition);
    m_sources.commands.failCode = QStringLiteral("device-gone");
    QCOMPARE(verify().code, QStringLiteral("device-gone"));
}

void TstWizardVerify::writesNothing()
{
    install(Family::Partition);
    const QMap<QString, QByteArray> before = snapshot(m_sources.volume);
    QVERIFY(put(firmwareFolder() + QStringLiteral("/a.txt"), "x"));
    const QMap<QString, QByteArray> changed = snapshot(m_sources.volume);
    QCOMPARE(verify().code, QString(error::verifyFailed));
    QCOMPARE(snapshot(m_sources.volume), changed);
    QVERIFY(before != changed);
    QVERIFY(m_context.facts.isEmpty());
}

void TstWizardVerify::finishInstall()
{
    install(Family::Dfu);
    m_context.folder = newFolder(QStringLiteral("backup"));
    QCOMPARE(FinishStep(m_sources).run(m_context).kind, StepOutcome::Kind::Done);
    QCOMPARE(m_context.facts.value(QStringLiteral("backup")).toString(), m_context.folder);
    QCOMPARE(m_context.facts.value(QStringLiteral("version")).toString(),
             QStringLiteral("0.2.0+gtest"));
    QVERIFY(!m_context.facts.contains(QStringLiteral("settings")));
}

void TstWizardVerify::finishUninstall()
{
    uninstall(Family::Partition);
    m_context.folder = newFolder(QStringLiteral("backup"));
    const QString settings = m_context.folder + QStringLiteral("/settings");
    m_context.others.insert(
        QString(step::backupSettings),
        QJsonObject{{QStringLiteral("manifest"), settings + QStringLiteral("/manifest.json")}});
    QCOMPARE(FinishStep(m_sources).run(m_context).kind, StepOutcome::Kind::Done);
    QCOMPARE(m_context.facts.value(QStringLiteral("backup")).toString(), m_context.folder);
    QCOMPARE(m_context.facts.value(QStringLiteral("settings")).toString(), settings);
    QVERIFY(!m_context.facts.contains(QStringLiteral("version")));
}

QTEST_GUILESS_MAIN(TstWizardVerify)
#include "tst_wizard_verify.moc"
