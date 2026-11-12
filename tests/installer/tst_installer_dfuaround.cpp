// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Around the DFU transfer of the iPod Classic (docs/specs/bootloader-classic.md
// sections 14.2, 15.1 and 16, and the tests of section 17 for the uninstall, the
// drivers and the error cases), against a fake system and a fake iPod. The
// Windows request mapping and the macOS entitlement are not here.

#include "constants.h"
#include "dfuaround.h"
#include "dfuimage.h"
#include "fakedfudevice.h"
#include "fakesystemlayer.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;

namespace {

constexpr int kPayload = 5400;
constexpr int kUninstallPayload = 6000;

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 7 + seed) & 0xFF);
    return b;
}

DfuParts fakeParts(int uninstallPayload = kUninstallPayload)
{
    DfuParts p;
    p.headerSignature = pattern(16, 0x31);
    p.certificate = pattern(698, 0x55);
    p.installerPayload = pattern(kPayload, 0x11);
    p.installerPayload.replace(kPayload - 0x50, 0x50, QByteArray(0x50, '\0'));
    p.uninstallerPayload = pattern(uninstallPayload, 0x77);
    return p;
}

QByteArray bootFile(const QByteArray &body)
{
    quint32 sum = 71;
    for (char c : body)
        sum += quint8(c);
    QByteArray f;
    for (int shift = 24; shift >= 0; shift -= 8)
        f.append(char((sum >> shift) & 0xFF));
    f.append("ip6g", 4);
    return f + body;
}

QByteArray installerImage()
{
    return buildInstallerImage(fakeParts(), bootFile(pattern(3000, 5))).image;
}

QByteArray uninstallerImage(int payload = kUninstallPayload)
{
    return buildUninstallerImage(fakeParts(payload)).image;
}

UsbDeviceInfo dfuAt(int address)
{
    UsbDeviceInfo d;
    d.id = {0x05AC, 0x1223};
    d.bus = 1;
    d.address = address;
    return d;
}

UsbDeviceInfo normalAt(int address)
{
    UsbDeviceInfo d = dfuAt(address);
    d.id.product = 0x1261;
    return d;
}

UsbDeviceInfo wtfAt(int address)
{
    UsbDeviceInfo d = dfuAt(address);
    d.id.product = 0x1241;
    return d;
}

FakeBusStep at(int ms, const QList<UsbDeviceInfo> &devices)
{
    FakeBusStep s;
    s.fromMs = ms;
    s.devices = devices;
    return s;
}

SystemProcess proc(qint64 id, const char *name)
{
    SystemProcess p;
    p.id = id;
    p.name = QString::fromLatin1(name);
    return p;
}

} // namespace

class TestDfuAround : public QObject
{
    Q_OBJECT

private slots:
    void ejectFailsTwiceThenWorks();
    void ejectWaitsForTheSystemToLetGo();
    void ejectCancelled();
    void spinDown();
    void pausedThenResumed();
    void resumedWhenPauseFails();
    void stateFileIsWrittenBeforeTheFirstPause();
    void resumedAfterACrash();
    void resumedAfterACrashSkipsOtherProcesses();
    void stuckProcessStaysInTheFile();

    void uninstallerImageLimits();
    void installerImageGivenToUninstallIsRefused();
    void flowRefusesWrongImageBeforeTouchingAnything();

    void quietWait();
    void resetDuringTheWaitStartsAgain();
    void newAddressStartsAgain();
    void threeResetsStop();
    void noDeviceAfter60Seconds();
    void twoDevicesAndWtf();

    void flowResumesOnSuccess();
    void flowResumesWhenTheTransferFails();
    void flowResumesWhenCancelled();
    void flowDoesNothingWhenPauseFails();

    void accessErrors_data();
    void accessErrors();
    void flowMapsAccessErrors_data();
    void flowMapsAccessErrors();

    void afterTransferTable_data();
    void afterTransferTable();
    void watchAfterTransfer_data();
    void watchAfterTransfer();
    void retries();
};

void TestDfuAround::ejectFailsTwiceThenWorks()
{
    FakeSystemLayer layer;
    layer.unmountFailures = 2;
    const EjectResult r = ejectIpod(layer, layer.options());
    QVERIFY(r.ok);
    QCOMPARE(layer.unmountCalls, 3);
    QCOMPARE(layer.sleeps, (QList<int>{3000, 3000}));
    QCOMPARE(layer.notices, (QStringList{"Close the programs using the iPod.",
                                         "Close the programs using the iPod."}));
}

void TestDfuAround::ejectWaitsForTheSystemToLetGo()
{
    FakeSystemLayer layer;
    layer.listedPolls = 3;
    QVERIFY(ejectIpod(layer, layer.options()).ok);
    QCOMPARE(layer.sleeps, (QList<int>{1000, 1000, 1000}));

    // It never lets go: the step is not skipped, it fails.
    FakeSystemLayer stuck;
    stuck.listedPolls = 1000;
    const EjectResult r = ejectIpod(stuck, stuck.options());
    QVERIFY(!r.ok);
    QVERIFY(r.message.contains(QLatin1String("still lists the iPod")));

    // Nor does a disk that cannot be unmounted loop for ever.
    FakeSystemLayer busy;
    busy.unmountFailures = 100000;
    const EjectResult b = ejectIpod(busy, busy.options());
    QVERIFY(!b.ok);
    QCOMPARE(b.attempts, kEjectMaxAttempts);
}

void TestDfuAround::ejectCancelled()
{
    FakeSystemLayer layer;
    layer.unmountFailures = 100;
    layer.cancelAtMs = 6000;
    const EjectResult r = ejectIpod(layer, layer.options());
    QVERIFY(r.cancelled);
    QVERIFY(!r.ok);
    QCOMPARE(layer.unmountCalls, 2);
}

void TestDfuAround::spinDown()
{
    QCOMPARE(spinDownWaitMs(true), 18000);
    QCOMPARE(spinDownWaitMs(false), 0);
}

void TestDfuAround::pausedThenResumed()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("paused.json"));
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA"), proc(13, "helperB")};
    {
        PausedProcesses paused(layer, file);
        const PauseResult r = paused.pauseAll();
        QVERIFY(r.ok);
        QCOMPARE(r.names, (QStringList{"helperA", "helperB"}));
        QCOMPARE(layer.events, (QStringList{"pause 12", "pause 13"}));
        QVERIFY(QFile::exists(file));
    }
    // The destructor resumes, and the file is gone.
    QCOMPARE(layer.events, (QStringList{"pause 12", "pause 13", "resume 12", "resume 13"}));
    QVERIFY(!QFile::exists(file));
}

void TestDfuAround::resumedWhenPauseFails()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("paused.json"));
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA"), proc(13, "helperB")};
    layer.pauseFails = {13};
    PausedProcesses paused(layer, file);
    const PauseResult r = paused.pauseAll();
    QVERIFY(!r.ok);
    QVERIFY(r.message.contains(QLatin1String("helperB")));
    // The one that was paused is resumed at once.
    QCOMPARE(layer.events, (QStringList{"pause 12", "resume 12"}));
    QVERIFY(!QFile::exists(file));
}

void TestDfuAround::stateFileIsWrittenBeforeTheFirstPause()
{
    // A state file that cannot be written means nothing is paused.
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA")};
    QTemporaryDir dir;
    PausedProcesses paused(layer, dir.filePath(QStringLiteral("no/such/folder/paused.json")));
    QVERIFY(!paused.pauseAll().ok);
    QVERIFY(layer.events.isEmpty());
}

void TestDfuAround::resumedAfterACrash()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("paused.json"));
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA"), proc(13, "helperB")};
    // The app stops without resuming: the destructor never runs (leaked on purpose).
    auto *crashed = new PausedProcesses(layer, file);
    QVERIFY(crashed->pauseAll().ok);
    QVERIFY(QFile::exists(file));
    layer.events.clear();

    // The next start.
    const ResumeResult r = resumeLeftover(layer, file);
    QCOMPARE(r.resumed.size(), 2);
    QCOMPARE(layer.events, (QStringList{"resume 12", "resume 13"}));
    QVERIFY(!QFile::exists(file));
    delete crashed; // the test only: it would resume them a second time, so empty it first
}

void TestDfuAround::resumedAfterACrashSkipsOtherProcesses()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("paused.json"));
    FakeSystemLayer first;
    first.running = {proc(12, "helperA"), proc(13, "helperB")};
    auto *crashed = new PausedProcesses(first, file);
    QVERIFY(crashed->pauseAll().ok);

    // After a restart of the computer, 12 is gone and 13 belongs to another program.
    FakeSystemLayer layer;
    layer.running = {proc(13, "something-else")};
    const ResumeResult r = resumeLeftover(layer, file);
    QVERIFY(r.resumed.isEmpty());
    QCOMPARE(r.gone.size(), 2);
    QVERIFY(layer.events.isEmpty());
    QVERIFY(!QFile::exists(file));
    delete crashed;

    // No file at all: nothing to do.
    const ResumeResult none = resumeLeftover(layer, file);
    QVERIFY(none.resumed.isEmpty() && none.gone.isEmpty() && none.failed.isEmpty());
}

void TestDfuAround::stuckProcessStaysInTheFile()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("paused.json"));
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA"), proc(13, "helperB")};
    {
        PausedProcesses paused(layer, file);
        QVERIFY(paused.pauseAll().ok);
        layer.resumeFails = {13};
        paused.resumeAll();
        QCOMPARE(paused.paused(), (QList<SystemProcess>{proc(13, "helperB")}));
        QVERIFY(QFile::exists(file));
        layer.resumeFails.clear();
    }
    // The destructor tried again and it worked.
    QVERIFY(!QFile::exists(file));
    QVERIFY(layer.events.contains(QStringLiteral("resume 13")));
}

void TestDfuAround::uninstallerImageLimits()
{
    const QByteArray ok = uninstallerImage();
    QVERIFY(checkUninstallerImageReadyToSend(ok, kUninstallPayload).ok);
    // The most that is allowed: 0x30C and 16 KiB of program.
    const int most = kUninstallerMaxPayloadBytes;
    QVERIFY(checkUninstallerImageReadyToSend(uninstallerImage(most), most).ok);
    QCOMPARE(uninstallerImage(most).size(), 0x30C + 16 * 1024);
    // One byte more is refused.
    const QByteArray big = uninstallerImage(most) + QByteArray(1, 'x');
    QVERIFY(!checkUninstallerImageReadyToSend(big, most + 1).ok);
    QVERIFY(!checkUninstallerImageReadyToSend(big, most).ok);
    // The size must be the one of the prefix and the program, nothing after.
    QVERIFY(!checkUninstallerImageReadyToSend(ok + QByteArray(1, 'x'), kUninstallPayload).ok);
    QVERIFY(!checkUninstallerImageReadyToSend(ok.left(ok.size() - 1), kUninstallPayload).ok);
    QVERIFY(!checkUninstallerImageReadyToSend(QByteArray(), 0).ok);
    // Without the image tag.
    QByteArray noTag = ok;
    noTag[0] = 'X';
    QVERIFY(!checkUninstallerImageReadyToSend(noTag, kUninstallPayload).ok);
}

void TestDfuAround::installerImageGivenToUninstallIsRefused()
{
    const QByteArray installer = installerImage();
    QVERIFY(checkInstallerImageReadyToSend(installer, kPayload).ok);
    // Whatever length the caller claims for the uninstaller program.
    QVERIFY(!checkUninstallerImageReadyToSend(installer, kPayload).ok);
    QVERIFY(!checkUninstallerImageReadyToSend(installer, kUninstallPayload).ok);
    // And the other way: an uninstaller image is not an installer image.
    QVERIFY(!checkInstallerImageReadyToSend(uninstallerImage(), kPayload).ok);

    // A small installer image, below the limit of an uninstaller: refused by its length.
    const QByteArray tiny = buildInstallerImage(fakeParts(), bootFile(QByteArray(1, 'x'))).image;
    QVERIFY(tiny.size() < 0x30C + kUninstallerMaxPayloadBytes);
    QVERIFY(!checkUninstallerImageReadyToSend(tiny, kUninstallPayload).ok);
}

void TestDfuAround::flowRefusesWrongImageBeforeTouchingAnything()
{
    FakeSystemLayer layer;
    layer.running = {proc(12, "helperA")};
    FakeDfuDevice device;
    layer.device = &device;
    ClassicDfuJob job;
    job.operation = ClassicOperation::Uninstall;
    job.image = installerImage();
    job.payloadLength = kUninstallPayload;
    const ClassicDfuOutcome o = runClassicDfu(layer, job, layer.options());
    QVERIFY(!o.ok);
    QCOMPARE(o.stage, ClassicStage::Check);
    QCOMPARE(layer.unmountCalls, 0);
    QVERIFY(layer.events.isEmpty());
    QCOMPARE(device.opens(), 0);

    // An install with the single boot byte at 1 never reaches the eject either.
    QByteArray patched = installerImage();
    patched[kDfuPrefixBytes + kPayload - kDfuHeaderBytes + kBootHeaderSingleBootByte] = 1;
    job.operation = ClassicOperation::Install;
    job.image = patched;
    job.payloadLength = kPayload;
    QCOMPARE(runClassicDfu(layer, job, layer.options()).stage, ClassicStage::Check);
    QCOMPARE(layer.unmountCalls, 0);
}

void TestDfuAround::quietWait()
{
    FakeSystemLayer layer;
    layer.bus = {at(0, {}), at(2500, {dfuAt(7)})};
    const DfuWaitResult r = waitForQuietDfu(layer, layer.options());
    QVERIFY(r.ok);
    QCOMPARE(r.device.address, 7);
    QCOMPARE(r.resets, 0);
    // Three polls of one second, then the countdown from 10 to 1.
    QCOMPARE(layer.notices.size(), 10);
    QCOMPARE(layer.notices.first(), QStringLiteral("Release the buttons now. 10"));
    QCOMPARE(layer.notices.last(), QStringLiteral("Release the buttons now. 1"));
    QCOMPARE(layer.now, 3000 + 10000);
}

void TestDfuAround::resetDuringTheWaitStartsAgain()
{
    FakeSystemLayer layer;
    // At 5 s the iPod is gone for a moment, then back at the same socket, as the bus
    // may show it after a quick reset and a new enumeration.
    layer.bus = {at(0, {dfuAt(7)}), at(4500, {}), at(7500, {dfuAt(7)})};
    const DfuWaitResult r = waitForQuietDfu(layer, layer.options());
    QVERIFY(r.ok);
    QCOMPARE(r.resets, 1);
    // The countdown ran twice: cut short the first time, whole the second.
    QVERIFY(layer.notices.size() > 10);
    QCOMPARE(layer.notices.count(QStringLiteral("Release the buttons now. 10")), 2);
    // Nothing was opened during the countdown.
    QVERIFY(layer.linkTimes.isEmpty());
}

void TestDfuAround::newAddressStartsAgain()
{
    FakeSystemLayer layer;
    layer.bus = {at(0, {dfuAt(7)}), at(3500, {dfuAt(8)})};
    const DfuWaitResult r = waitForQuietDfu(layer, layer.options());
    QVERIFY(r.ok);
    QCOMPARE(r.device.address, 8);
    QCOMPARE(r.resets, 1);
    QCOMPARE(layer.notices.count(QStringLiteral("Release the buttons now. 10")), 2);
}

void TestDfuAround::threeResetsStop()
{
    FakeSystemLayer layer;
    // A new address every 3 seconds: the iPod keeps resetting.
    for (int i = 0; i < 40; ++i)
        layer.bus << at(i * 3000, {dfuAt(10 + i)});
    const DfuWaitResult r = waitForQuietDfu(layer, layer.options());
    QVERIFY(!r.ok);
    QCOMPARE(r.resets, 3);
    QCOMPARE(r.message, QStringLiteral("The iPod keeps leaving DFU mode. Release the buttons as "
                                       "soon as the message appears."));
    QCOMPARE(layer.notices.count(QStringLiteral("Release the buttons now. 10")), 3);
}

void TestDfuAround::noDeviceAfter60Seconds()
{
    FakeSystemLayer layer;
    const DfuWaitResult r = waitForQuietDfu(layer, layer.options());
    QVERIFY(!r.ok);
    QVERIFY(r.message.startsWith(QLatin1String("The iPod did not appear in DFU mode.")));
    QCOMPARE(layer.now, 60000);
    // The iPod in normal mode is not a DFU device.
    FakeSystemLayer normal;
    normal.bus = {at(0, {normalAt(3)})};
    QVERIFY(!waitForQuietDfu(normal, normal.options()).ok);
}

void TestDfuAround::twoDevicesAndWtf()
{
    FakeSystemLayer two;
    two.bus = {at(0, {dfuAt(7), dfuAt(8)})};
    const DfuWaitResult a = waitForQuietDfu(two, two.options());
    QVERIFY(!a.ok);
    QVERIFY(a.message.contains(QLatin1String("Plug only one iPod")));

    FakeSystemLayer wtf;
    wtf.bus = {at(0, {wtfAt(7)})};
    const DfuWaitResult b = waitForQuietDfu(wtf, wtf.options());
    QVERIFY(!b.ok);
    QVERIFY(b.message.contains(QLatin1String("wrong mode")));
}

namespace {

// A layer with a fake iPod in DFU mode, and two programs of Apple running.
struct Rig
{
    Rig()
    {
        layer.running = {proc(12, "helperA"), proc(13, "helperB")};
        layer.bus = {at(0, {dfuAt(7)})};
        layer.device = &device;
        job.operation = ClassicOperation::Uninstall;
        job.image = uninstallerImage();
        job.payloadLength = kUninstallPayload;
        job.stateFile = dir.filePath(QStringLiteral("paused.json"));
        job.stateBefore = BootloaderState::Present;
        job.readState = [] { return BootloaderState::Absent; };
        job.send.sleep = [](int) {};
    }

    QTemporaryDir dir;
    FakeSystemLayer layer;
    FakeDfuDevice device;
    ClassicDfuJob job;
};

} // namespace

void TestDfuAround::flowResumesOnSuccess()
{
    Rig rig;
    rig.layer.unmountFailures = 1;
    // The payload runs, the iPod leaves USB, then comes back in normal mode.
    rig.layer.bus = {at(0, {dfuAt(7)}), at(33000, {}), at(40000, {normalAt(9)})};
    const ClassicDfuOutcome o = runClassicDfu(rig.layer, rig.job, rig.layer.options());
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.stage, ClassicStage::Done);
    QCOMPARE(o.message, QStringLiteral("Done"));
    QVERIFY(o.after.deleteRecord);
    QVERIFY(!o.after.writeRecord);
    // The order: eject, pause, send, resume.
    QCOMPARE(rig.layer.events,
             (QStringList{"unmounted", "pause 12", "pause 13", "resume 12", "resume 13"}));
    QVERIFY(rig.device.protocolErrors().isEmpty());
    QCOMPARE(rig.device.received().left(rig.job.image.size()), rig.job.image);
    // The disk stopped (18 s) before the iPod was looked for.
    QVERIFY(rig.layer.sleeps.contains(18000));
    QVERIFY(rig.layer.linkTimes.first() >= 18000 + 3000);
    QVERIFY(!QFile::exists(rig.job.stateFile));

    // A flash model does not wait for the disk.
    Rig flash;
    flash.job.hasHardDisk = false;
    flash.layer.bus = {at(0, {dfuAt(7)}), at(14000, {}), at(20000, {normalAt(9)})};
    QVERIFY(runClassicDfu(flash.layer, flash.job, flash.layer.options()).ok);
    QVERIFY(!flash.layer.sleeps.contains(18000));
}

void TestDfuAround::flowResumesWhenTheTransferFails()
{
    Rig rig;
    FakeDfuScript script;
    script.timeoutAtBlock = 1;
    FakeDfuDevice device(script);
    rig.layer.device = &device;
    const ClassicDfuOutcome o = runClassicDfu(rig.layer, rig.job, rig.layer.options());
    QVERIFY(!o.ok);
    QCOMPARE(o.stage, ClassicStage::Transfer);
    QCOMPARE(o.message, QStringLiteral("The transfer was interrupted. The iPod is not changed."));
    QCOMPARE(rig.layer.events.mid(1),
             (QStringList{"pause 12", "pause 13", "resume 12", "resume 13"}));
    QVERIFY(!QFile::exists(rig.job.stateFile));

    // The same when the iPod refuses the file.
    Rig refused;
    FakeDfuScript no;
    no.endStatus = kDfuStatusErrFirmware;
    FakeDfuDevice picky(no);
    refused.layer.device = &picky;
    const ClassicDfuOutcome r = runClassicDfu(refused.layer, refused.job, refused.layer.options());
    QCOMPARE(r.transfer.failure, DfuFailure::Refused);
    QCOMPARE(refused.layer.events.last(), QStringLiteral("resume 13"));
}

void TestDfuAround::flowResumesWhenCancelled()
{
    // Cancelled during the countdown, after the programs were paused.
    Rig rig;
    rig.layer.cancelAtMs = 18000 + 3000 + 4000;
    rig.layer.bus = {at(0, {dfuAt(7)})};
    const ClassicDfuOutcome o = runClassicDfu(rig.layer, rig.job, rig.layer.options());
    QVERIFY(o.cancelled);
    QVERIFY(!o.ok);
    QCOMPARE(o.stage, ClassicStage::WaitDevice);
    QCOMPARE(rig.layer.events.mid(1),
             (QStringList{"pause 12", "pause 13", "resume 12", "resume 13"}));
    QCOMPARE(rig.device.opens(), 0);
    QVERIFY(!QFile::exists(rig.job.stateFile));

    // Cancelled during the eject: nothing was paused.
    Rig early;
    early.layer.unmountFailures = 100;
    early.layer.cancelAtMs = 3000;
    QCOMPARE(runClassicDfu(early.layer, early.job, early.layer.options()).stage,
             ClassicStage::Eject);
    QVERIFY(early.layer.events.isEmpty());
}

void TestDfuAround::flowDoesNothingWhenPauseFails()
{
    Rig rig;
    rig.layer.pauseFails = {13};
    const ClassicDfuOutcome o = runClassicDfu(rig.layer, rig.job, rig.layer.options());
    QCOMPARE(o.stage, ClassicStage::Pause);
    QVERIFY(!o.ok);
    QCOMPARE(rig.device.opens(), 0);
    QCOMPARE(rig.layer.events.last(), QStringLiteral("resume 12"));
}

void TestDfuAround::accessErrors_data()
{
    QTest::addColumn<UsbAccessError>("code");
    QTest::addColumn<HostSystem>("system");
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("hintPart");

    QTest::newRow("denied linux") << UsbAccessError::AccessDenied << HostSystem::Linux
                                  << "No permission to use the iPod over USB." << "udev rule";
    QTest::newRow("denied windows") << UsbAccessError::AccessDenied << HostSystem::Windows
                                    << "No permission to use the iPod over USB." << "WinUSB";
    QTest::newRow("busy") << UsbAccessError::Busy << HostSystem::Linux
                          << "Another program is using the iPod." << "helperA";
    QTest::newRow("no device") << UsbAccessError::NoDevice << HostSystem::Linux
                               << "The iPod is gone. It was unplugged or it reset."
                               << "enter DFU mode again";
    QTest::newRow("unsupported") << UsbAccessError::NotSupported << HostSystem::Windows
                                 << "The iPod has no USB driver that TacetOS can use."
                                 << "Apple's driver";
    QTest::newRow("not found at configuration")
        << UsbAccessError::NotFoundAtConfig << HostSystem::Windows
        << "The driver bound to the iPod is not one TacetOS can use." << "Replace it with WinUSB";
    QTest::newRow("exclusive") << UsbAccessError::ExclusiveAccess << HostSystem::MacOs
                               << "Another program is using the iPod." << "helperA";
    QTest::newRow("other") << UsbAccessError::Other << HostSystem::Linux
                           << "The iPod could not be opened." << "Reset the iPod";
}

void TestDfuAround::accessErrors()
{
    QFETCH(UsbAccessError, code);
    QFETCH(HostSystem, system);
    QFETCH(QString, message);
    QFETCH(QString, hintPart);
    const UsbAccessMessage m = describeAccessError(code, system, {"helperA"});
    QCOMPARE(m.message, message);
    QVERIFY2(m.hint.contains(hintPart), qPrintable(m.hint));
    // Every message names a way out.
    QVERIFY(!m.hint.isEmpty());
}

void TestDfuAround::flowMapsAccessErrors_data()
{
    QTest::addColumn<bool>("atOpen");
    QTest::addColumn<UsbAccessError>("code");
    QTest::addColumn<QString>("message");

    QTest::newRow("open denied") << true << UsbAccessError::AccessDenied
                                 << "No permission to use the iPod over USB.";
    QTest::newRow("open busy") << true << UsbAccessError::Busy
                               << "Another program is using the iPod.";
    QTest::newRow("open no device")
        << true << UsbAccessError::NoDevice << "The iPod is gone. It was unplugged or it reset.";
    QTest::newRow("request no device")
        << false << UsbAccessError::NoDevice << "The iPod is gone. It was unplugged or it reset.";
    QTest::newRow("request unsupported") << false << UsbAccessError::NotSupported
                                         << "The iPod has no USB driver that TacetOS can use.";
}

void TestDfuAround::flowMapsAccessErrors()
{
    QFETCH(bool, atOpen);
    QFETCH(UsbAccessError, code);
    QFETCH(QString, message);
    Rig rig;
    FakeDfuScript script;
    if (atOpen) {
        script.openFails = true;
        script.openCode = code;
    } else {
        script.timeoutAtBlock = 0;
        script.requestCode = code;
    }
    FakeDfuDevice device(script);
    rig.layer.device = &device;
    const ClassicDfuOutcome o = runClassicDfu(rig.layer, rig.job, rig.layer.options());
    QVERIFY(!o.ok);
    QCOMPARE(o.stage, ClassicStage::Transfer);
    QCOMPARE(o.transfer.access, code);
    QCOMPARE(o.message, message);
    QVERIFY(!o.hint.isEmpty());
    // The programs are given back whatever the code.
    QCOMPARE(rig.layer.events.last(), QStringLiteral("resume 13"));
}

void TestDfuAround::afterTransferTable_data()
{
    QTest::addColumn<ClassicOperation>("operation");
    QTest::addColumn<BootloaderState>("before");
    QTest::addColumn<ReturnedMode>("mode");
    QTest::addColumn<BootloaderState>("now");
    QTest::addColumn<AfterAction>("action");
    QTest::addColumn<QString>("message");
    QTest::addColumn<bool>("writes");
    QTest::addColumn<bool>("deletes");

    using O = ClassicOperation;
    using B = BootloaderState;
    using M = ReturnedMode;
    using A = AfterAction;
    const QString done = QStringLiteral("Done");
    const QString same =
        QStringLiteral("The iPod restarted but the change was not made. It is as before.");
    const QString unknown = QStringLiteral("The iPod restarted. The change could not be checked.");

    QTest::newRow("install done") << O::Install << B::Absent << M::Normal << B::Present << A::Done
                                  << done << true << false;
    QTest::newRow("uninstall done")
        << O::Uninstall << B::Present << M::Normal << B::Absent << A::Done << done << false << true;
    QTest::newRow("install as before") << O::Install << B::Absent << M::Normal << B::Absent
                                       << A::OfferRetryOnce << same << false << false;
    QTest::newRow("uninstall as before") << O::Uninstall << B::Present << M::Normal << B::Present
                                         << A::OfferRetryOnce << same << false << false;
    QTest::newRow("install unknown") << O::Install << B::Absent << M::Normal << B::Unknown
                                     << A::ShowCheckByHand << unknown << false << false;
    QTest::newRow("uninstall unknown") << O::Uninstall << B::Present << M::Normal << B::Unknown
                                       << A::ShowCheckByHand << unknown << false << false;
    QTest::newRow("unknown before and after") << O::Install << B::Unknown << M::Normal << B::Absent
                                              << A::ShowCheckByHand << unknown << false << false;
    QTest::newRow("nothing in 60 s")
        << O::Install << B::Absent << M::Nothing << B::Unknown << A::ShowRestore
        << QStringLiteral("The iPod did not restart. Hold Menu and "
                          "Select for about 6 seconds to reset it. If "
                          "that does not help, enter DFU mode and "
                          "restore it with Apple's software.")
        << false << false;
    QTest::newRow("back in DFU") << O::Install << B::Absent << M::Dfu << B::Unknown
                                 << A::ButtonsHeld
                                 << QStringLiteral("The iPod went back to DFU mode. Release the "
                                                   "buttons as soon as the message appears.")
                                 << false << false;
    QTest::newRow("wtf") << O::Uninstall << B::Present << M::Wtf << B::Unknown << A::ResetWrongMode
                         << QStringLiteral("The iPod is in the wrong mode. Reset it.") << false
                         << false;
}

void TestDfuAround::afterTransferTable()
{
    QFETCH(ClassicOperation, operation);
    QFETCH(BootloaderState, before);
    QFETCH(ReturnedMode, mode);
    QFETCH(BootloaderState, now);
    QFETCH(AfterAction, action);
    QFETCH(QString, message);
    QFETCH(bool, writes);
    QFETCH(bool, deletes);
    const AfterTransfer r = classifyAfterTransfer(operation, before, mode, now);
    QCOMPARE(r.action, action);
    QCOMPARE(r.message, message);
    QCOMPARE(r.writeRecord, writes);
    QCOMPARE(r.deleteRecord, deletes);
    QCOMPARE(r.success, action == AfterAction::Done);
}

void TestDfuAround::watchAfterTransfer_data()
{
    QTest::addColumn<QList<FakeBusStep>>("bus");
    QTest::addColumn<ReturnedMode>("mode");

    QTest::newRow("normal") << QList<FakeBusStep>{at(0, {dfuAt(7)}), at(2000, {}),
                                                  at(9000, {normalAt(9)})}
                            << ReturnedMode::Normal;
    // 1223 still listed right after the last request is not a return.
    QTest::newRow("dfu listed at first")
        << QList<FakeBusStep>{at(0, {dfuAt(7)}), at(2000, {}), at(9000, {normalAt(9)})}
        << ReturnedMode::Normal;
    QTest::newRow("dfu again") << QList<FakeBusStep>{at(0, {dfuAt(7)}), at(2000, {}),
                                                     at(9000, {dfuAt(8)})}
                               << ReturnedMode::Dfu;
    QTest::newRow("wtf") << QList<FakeBusStep>{at(0, {}), at(5000, {wtfAt(8)})}
                         << ReturnedMode::Wtf;
    QTest::newRow("never") << QList<FakeBusStep>{at(0, {})} << ReturnedMode::Nothing;
    QTest::newRow("dfu never leaves")
        << QList<FakeBusStep>{at(0, {dfuAt(7)})} << ReturnedMode::Nothing;
}

void TestDfuAround::watchAfterTransfer()
{
    QFETCH(QList<FakeBusStep>, bus);
    QFETCH(ReturnedMode, mode);
    FakeSystemLayer layer;
    layer.bus = bus;
    QCOMPARE(::tacetos::installer::watchAfterTransfer(layer, layer.options()), mode);
    if (mode == ReturnedMode::Nothing)
        QCOMPARE(layer.now, 60000);
}

void TestDfuAround::retries()
{
    // Before the payload: no limit.
    ClassicRetryPolicy before;
    for (int i = 0; i < 10; ++i) {
        const RetryAdvice a = before.record(ClassicFailureKind::BeforePayload);
        QVERIFY(a.mayRetry);
        QVERIFY(a.mayInstall);
    }

    // A one-tone error: once more, then stop with the common causes.
    ClassicRetryPolicy one;
    const RetryAdvice first = one.record(ClassicFailureKind::OneTone);
    QVERIFY(first.mayRetry);
    QVERIFY(first.mayInstall);
    const RetryAdvice second = one.record(ClassicFailureKind::OneTone);
    QVERIFY(!second.mayRetry);
    QVERIFY(second.mayInstall);
    QVERIFY(second.message.contains(QLatin1String("Apple's firmware")));
    QVERIFY(second.message.contains(QLatin1String("restored in an unusual way")));
    QVERIFY(second.message.contains(QLatin1String("boot area")));
    // A success starts the count again.
    one.recordSuccess();
    QVERIFY(one.record(ClassicFailureKind::OneTone).mayRetry);

    // Two tones: nothing was written, the app asks for a report.
    ClassicRetryPolicy two;
    const RetryAdvice t = two.record(ClassicFailureKind::TwoTones);
    QVERIFY(!t.mayRetry);
    QVERIFY(t.askToReport);
    QVERIFY(t.mayInstall);

    // Three tones, or no restart: no more offer to install, only the restore.
    for (ClassicFailureKind kind :
         {ClassicFailureKind::ThreeTones, ClassicFailureKind::NoRestart}) {
        ClassicRetryPolicy bad;
        const RetryAdvice a = bad.record(kind);
        QVERIFY(!a.mayRetry);
        QVERIFY(!a.mayInstall);
        QVERIFY(a.showRestore);
        QVERIFY(!bad.mayInstall());
        // And it stays so, whatever comes next.
        QVERIFY(!bad.record(ClassicFailureKind::BeforePayload).mayInstall);
        bad.recordSuccess();
        QVERIFY(!bad.mayInstall());
    }
}

QTEST_GUILESS_MAIN(TestDfuAround)
#include "tst_installer_dfuaround.moc"
