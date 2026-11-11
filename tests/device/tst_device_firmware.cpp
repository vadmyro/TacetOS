// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The firmware line (docs/specs/detection.md section 7.6). The Apple model
// files in fixtures/sysinfo are synthetic: the one of the real Classic is
// empty, so the key and the layout come from the spec only (section 13).

#include "constants.h"
#include "firmware.h"
#include "markers.h"
#include "merge.h"
#include "messages.h"
#include "models.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

QList<Model> g_models;

void copyFixture(const QString &fixture, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    QVERIFY(QFile::copy(QStringLiteral(TACETOS_DEVICE_FIXTURES "/") + fixture, to));
}

void writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

// The Apple model file of a fixture on a music volume.
void putSysInfo(const QTemporaryDir &dir, const QString &fixture)
{
    copyFixture(QStringLiteral("sysinfo/") + fixture,
                dir.filePath(QStringLiteral("iPod_Control/Device/SysInfo")));
}

// The info file of the firmware folder, from the synthetic fixture, with
// `extra` lines added.
void putInfoFile(const QTemporaryDir &dir, const QString &extra = QString())
{
    QFile file(QStringLiteral(TACETOS_DEVICE_FIXTURES "/info-synthetic.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll()) + extra;
    writeText(
        dir.filePath(QString(kFirmwareFolder) + QLatin1Char('/') + QString(kFirmwareInfoFile)),
        text);
}

IpodEvidence classicWith(const MarkerReading &markers, const QString &manufacturer = QString(),
                         const QString &product = QString())
{
    const UsbMatch match = matchUsbId(g_models, QStringLiteral("05ac:1261"));
    UsbDevice usb;
    usb.usbId = QStringLiteral("05ac:1261");
    usb.mode = match.mode;
    usb.candidates = match.candidates;
    usb.portPath = QStringLiteral("1-2");
    usb.manufacturer = manufacturer;
    usb.product = product;

    IpodEvidence ev;
    ev.usb = usb;
    DiskInfo disk;
    disk.node = QStringLiteral("/dev/sdb");
    disk.sizeBytes = 120'030'000'000;
    disk.sectorSize = 4096;
    ev.disk = disk;
    VolumeInfo volume;
    volume.mountPoint = QStringLiteral("/run/media/ipod");
    volume.filesystem = QStringLiteral("vfat");
    volume.totalBytes = 118'000'000'000;
    ev.volume = volume;
    ev.markers = markers;
    ev.waitOver = true;
    return ev;
}

} // namespace

class TstDeviceFirmware : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void appleVersion_data();
    void appleVersion();
    void appleFirmwareFromSysInfo();
    void appleFirmwareWithoutVersion();
    void emptySysInfoIsUnknown();
    void noMarkersIsUnknown();
    void customFirmwareFromInfoFile();
    void tacetosFromDistributionLine();
    void distributionNameIsNotRepeated();
    void infoFileBeatsSysInfo();
    void infoFileOfAnotherModelIsNotUsed();
    void unknownTargetIsNotCustom();
    void dfuKeepsTheLine();
    void unsupportedFilesystemKnowsNothing();
    void runningFromStrings_data();
    void runningFromStrings();
    void appleStringsBeatTheFiles();
    void otherStringsKeepTheFiles();
    void otherStringsWithoutVolume();
    void noStringsUseTheFiles();
    void unknownLineIsEmpty();
    void messages();
    void classicNeverSaysBootloader();
    void logLineHasNoTrailingSpace();
};

void TstDeviceFirmware::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

void TstDeviceFirmware::appleVersion_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("version");

    QTest::newRow("brackets") << "0x01168000 (1.1.6)" << "1.1.6";
    QTest::newRow("two parts") << "0x02040000 (2.0)" << "2.0";
    QTest::newRow("no brackets") << "0x01168000" << "";
    QTest::newRow("empty brackets") << "0x01168000 ()" << "";
    QTest::newRow("words in brackets") << "0x01168000 (beta)" << "";
    QTest::newRow("empty") << "" << "";
}

void TstDeviceFirmware::appleVersion()
{
    QFETCH(QString, value);
    QFETCH(QString, version);
    QMap<QString, QString> fields;
    fields.insert(QString(kSysInfoFirmwareKey), value);
    QCOMPARE(appleFirmwareVersion(fields), version);
    QCOMPARE(appleFirmwareVersion(QMap<QString, QString>()), QString());
}

void TstDeviceFirmware::appleFirmwareFromSysInfo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    const MarkerReading markers = readMarkers(dir.path(), g_models);
    const MergeResult result = mergeEvidence(classicWith(markers), g_models);

    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Apple);
    QCOMPARE(result.ipod.firmwareLine.version, QStringLiteral("1.1.6"));
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Apple firmware 1.1.6"));
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QLatin1String("firmware: apple 1.1.6")));
}

void TstDeviceFirmware::appleFirmwareWithoutVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-no-version.txt"));
    const MergeResult result =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Apple);
    QVERIFY(result.ipod.firmwareLine.version.isEmpty());
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Apple firmware"));
}

void TstDeviceFirmware::emptySysInfoIsUnknown()
{
    // The Apple model file of the real Classic, which runs another firmware.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("empty.txt"));
    const MergeResult result =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
    QVERIFY(firmwareMessage(result.ipod).isEmpty());
}

void TstDeviceFirmware::noMarkersIsUnknown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const MergeResult result =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
}

void TstDeviceFirmware::customFirmwareFromInfoFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putInfoFile(dir);
    const MarkerReading markers = readMarkers(dir.path(), g_models, QStringLiteral("Distro"));
    QCOMPARE(markers.firmware.files, FirmwareFiles::Present);
    QCOMPARE(markers.firmware.infoVersion, QStringLiteral("4.0"));

    const MergeResult result = mergeEvidence(classicWith(markers), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Custom);
    QCOMPARE(result.ipod.firmwareLine.version, QStringLiteral("4.0"));
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Custom firmware 4.0"));
}

void TstDeviceFirmware::tacetosFromDistributionLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putInfoFile(dir, QStringLiteral("Distro: 0.3\n"));
    const MarkerReading markers = readMarkers(dir.path(), g_models, QStringLiteral("Distro"));
    const MergeResult result = mergeEvidence(classicWith(markers), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Tacetos);
    QCOMPARE(result.ipod.firmwareLine.version, QStringLiteral("0.3"));
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("TacetOS 0.3"));
}

void TstDeviceFirmware::distributionNameIsNotRepeated()
{
    FirmwareInfo files;
    files.files = FirmwareFiles::Tacetos;
    files.version = QStringLiteral("TacetOS 0.3");
    QCOMPARE(identifyFirmware(files, QMap<QString, QString>(), RunningFirmware::Unknown).version,
             QStringLiteral("0.3"));
    files.version = QStringLiteral("tacetos 0.3");
    QCOMPARE(identifyFirmware(files, QMap<QString, QString>(), RunningFirmware::Unknown).version,
             QStringLiteral("0.3"));
}

void TstDeviceFirmware::infoFileBeatsSysInfo()
{
    // Files of our firmware on an iPod that Apple's software had set up.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    putInfoFile(dir, QStringLiteral("Distro: 0.3\n"));
    const MarkerReading markers = readMarkers(dir.path(), g_models, QStringLiteral("Distro"));
    const MergeResult result = mergeEvidence(classicWith(markers), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Tacetos);
}

void TstDeviceFirmware::infoFileOfAnotherModelIsNotUsed()
{
    // Files for the 5th generation Video on a Classic.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(QString(kFirmwareFolder));
    QFile file(QStringLiteral(TACETOS_DEVICE_FIXTURES "/info-synthetic.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString text = QString::fromUtf8(file.readAll());
    text.replace(QStringLiteral("Target: ipod6g"), QStringLiteral("Target: ipodvideo"));
    writeText(folder + QLatin1Char('/') + QString(kFirmwareInfoFile), text);

    const MarkerReading markers = readMarkers(dir.path(), g_models);
    const MergeResult result = mergeEvidence(classicWith(markers), g_models);
    QVERIFY(result.ipod.firmware.otherModel);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QLatin1String("for another model")));

    // With the Apple model file the line falls back on it.
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    const MergeResult apple =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(apple.ipod.firmwareLine.kind, FirmwareKind::Apple);
}

void TstDeviceFirmware::unknownTargetIsNotCustom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeText(
        dir.filePath(QString(kFirmwareFolder) + QLatin1Char('/') + QString(kFirmwareInfoFile)),
        QStringLiteral("Version: 4.0\nTarget: nothing-known\n"));
    const MarkerReading markers = readMarkers(dir.path(), g_models);
    QCOMPARE(markers.firmware.files, FirmwareFiles::Unknown);
    const MergeResult result = mergeEvidence(classicWith(markers), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
}

void TstDeviceFirmware::dfuKeepsTheLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    const MergeResult normal =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);

    const UsbMatch match = matchUsbId(g_models, QStringLiteral("05ac:1223"));
    UsbDevice usb;
    usb.usbId = QStringLiteral("05ac:1223");
    usb.mode = match.mode;
    usb.candidates = match.candidates;
    usb.portPath = QStringLiteral("1-2");
    IpodEvidence ev;
    ev.usb = usb;
    PreviousIpod previous;
    previous.ipod = normal.ipod;
    previous.ageMs = 5000;
    ev.previous = previous;

    const MergeResult dfu = mergeEvidence(ev, g_models);
    QCOMPARE(dfu.ipod.state, IpodState::Dfu);
    QCOMPARE(dfu.ipod.firmwareLine.kind, FirmwareKind::Apple);
    QCOMPARE(dfu.ipod.firmwareLine.version, QStringLiteral("1.1.6"));

    // Nothing remembered: nothing claimed.
    ev.previous.reset();
    QCOMPARE(mergeEvidence(ev, g_models).ipod.firmwareLine.kind, FirmwareKind::Unknown);
}

void TstDeviceFirmware::unsupportedFilesystemKnowsNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    IpodEvidence ev = classicWith(readMarkers(dir.path(), g_models));
    ev.volume->filesystem = QStringLiteral("ext4");
    const MergeResult result = mergeEvidence(ev, g_models);
    QCOMPARE(result.ipod.state, IpodState::UnsupportedFilesystem);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
}

void TstDeviceFirmware::runningFromStrings_data()
{
    QTest::addColumn<QString>("manufacturer");
    QTest::addColumn<QString>("product");
    QTest::addColumn<int>("running");
    const auto apple = static_cast<int>(RunningFirmware::Apple);
    const auto other = static_cast<int>(RunningFirmware::Other);
    const auto unknown = static_cast<int>(RunningFirmware::Unknown);
    QTest::newRow("apple") << "Apple Inc." << "iPod" << apple;
    QTest::newRow("no strings") << "" << "" << unknown;
    QTest::newRow("other both") << "Some Maker" << "Some Player" << other;
    QTest::newRow("manufacturer only") << "Apple Inc." << "" << other;
    QTest::newRow("product only") << "" << "iPod" << other;
    QTest::newRow("apple manufacturer, other product") << "Apple Inc." << "iPod Classic" << other;
}

void TstDeviceFirmware::runningFromStrings()
{
    QFETCH(QString, manufacturer);
    QFETCH(QString, product);
    QFETCH(int, running);
    QCOMPARE(static_cast<int>(runningFirmware(manufacturer, product)), running);

    // The merge reads them in normal mode and carries them to the iPod.
    const MergeResult result =
        mergeEvidence(classicWith(MarkerReading(), manufacturer, product), g_models);
    QCOMPARE(static_cast<int>(result.ipod.running), running);
}

void TstDeviceFirmware::appleStringsBeatTheFiles()
{
    // Measured: Apple's firmware running with the files of another one on the
    // volume. The line is Apple's, with the version of the Apple model file.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    putInfoFile(dir);
    const MarkerReading markers = readMarkers(dir.path(), g_models);
    QCOMPARE(markers.firmware.files, FirmwareFiles::Present);
    const MergeResult result = mergeEvidence(
        classicWith(markers, QStringLiteral("Apple Inc."), QStringLiteral("iPod")), g_models);
    QCOMPARE(result.ipod.running, RunningFirmware::Apple);
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Apple firmware 1.1.6"));

    // With the files of our own firmware too.
    putInfoFile(dir, QStringLiteral("Distro: 0.3\n"));
    const MergeResult ours =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models, QStringLiteral("Distro")),
                                  QStringLiteral("Apple Inc."), QStringLiteral("iPod")),
                      g_models);
    QCOMPARE(firmwareMessage(ours.ipod), QStringLiteral("Apple firmware 1.1.6"));
}

void TstDeviceFirmware::otherStringsKeepTheFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putInfoFile(dir);
    const MarkerReading markers = readMarkers(dir.path(), g_models);
    const MergeResult custom = mergeEvidence(
        classicWith(markers, QStringLiteral("Some Maker"), QStringLiteral("Some Player")),
        g_models);
    QCOMPARE(firmwareMessage(custom.ipod), QStringLiteral("Custom firmware 4.0"));

    putInfoFile(dir, QStringLiteral("Distro: 0.3\n"));
    const MergeResult ours =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models, QStringLiteral("Distro")),
                                  QStringLiteral("Some Maker"), QStringLiteral("Some Player")),
                      g_models);
    QCOMPARE(firmwareMessage(ours.ipod), QStringLiteral("TacetOS 0.3"));
}

void TstDeviceFirmware::otherStringsWithoutVolume()
{
    // Strings that are not Apple's and nothing to read: "Custom firmware".
    IpodEvidence ev =
        classicWith(MarkerReading(), QStringLiteral("Some Maker"), QStringLiteral("Some Player"));
    ev.volume.reset();
    const MergeResult result = mergeEvidence(ev, g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Custom);
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Custom firmware"));

    // The same on a volume with no marker files at all.
    const MergeResult onVolume = mergeEvidence(
        classicWith(MarkerReading(), QStringLiteral("Some Maker"), QStringLiteral("Some Player")),
        g_models);
    QCOMPARE(firmwareMessage(onVolume.ipod), QStringLiteral("Custom firmware"));
}

void TstDeviceFirmware::noStringsUseTheFiles()
{
    // The old rule: the files first, then the Apple model file.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    putSysInfo(dir, QStringLiteral("classic-apple-firmware.txt"));
    putInfoFile(dir);
    const MergeResult files =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(files.ipod.running, RunningFirmware::Unknown);
    QCOMPARE(firmwareMessage(files.ipod), QStringLiteral("Custom firmware 4.0"));

    QTemporaryDir apple;
    QVERIFY(apple.isValid());
    putSysInfo(apple, QStringLiteral("classic-apple-firmware.txt"));
    const MergeResult model =
        mergeEvidence(classicWith(readMarkers(apple.path(), g_models)), g_models);
    QCOMPARE(firmwareMessage(model.ipod), QStringLiteral("Apple firmware 1.1.6"));
}

void TstDeviceFirmware::unknownLineIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const MergeResult result =
        mergeEvidence(classicWith(readMarkers(dir.path(), g_models)), g_models);
    QCOMPARE(result.ipod.firmwareLine.kind, FirmwareKind::Unknown);
    QVERIFY(firmwareMessage(result.ipod).isEmpty());
}

void TstDeviceFirmware::messages()
{
    DetectedIpod ipod;
    ipod.firmwareLine = {FirmwareKind::Tacetos, QString()};
    QCOMPARE(firmwareMessage(ipod), QStringLiteral("TacetOS"));
    ipod.firmwareLine = {FirmwareKind::Custom, QString()};
    QCOMPARE(firmwareMessage(ipod), QStringLiteral("Custom firmware"));
    ipod.firmwareLine = {FirmwareKind::Unknown, QStringLiteral("9.9")};
    QVERIFY(firmwareMessage(ipod).isEmpty());
}

void TstDeviceFirmware::classicNeverSaysBootloader()
{
    // Section 7.4: files are not a bootloader. No situation of a Classic gives
    // a line that talks about one.
    const QStringList fixtures = {QStringLiteral("classic-apple-firmware.txt"),
                                  QStringLiteral("classic-no-version.txt"),
                                  QStringLiteral("empty.txt")};
    for (const QString &fixture : fixtures) {
        for (const bool withInfo : {false, true}) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            putSysInfo(dir, fixture);
            if (withInfo)
                putInfoFile(dir, QStringLiteral("Distro: 0.3\n"));
            const MarkerReading markers =
                readMarkers(dir.path(), g_models, QStringLiteral("Distro"));
            const MergeResult result = mergeEvidence(classicWith(markers), g_models);
            QCOMPARE(result.ipod.model, QStringLiteral("classic-6g"));
            QVERIFY(!firmwareMessage(result.ipod)
                         .contains(QLatin1String("bootloader"), Qt::CaseInsensitive));
            QVERIFY(!stateMessage(result.ipod, g_models)
                         .contains(QLatin1String("bootloader"), Qt::CaseInsensitive));
        }
    }
}

void TstDeviceFirmware::logLineHasNoTrailingSpace()
{
    // No version: nothing after the kind, not even a space.
    QStringList log;
    const FirmwareLine unknown =
        identifyFirmware(FirmwareInfo(), QMap<QString, QString>(), RunningFirmware::Unknown, &log);
    QCOMPARE(unknown.kind, FirmwareKind::Unknown);
    QCOMPARE(log, QStringList{QStringLiteral("firmware: unknown")});

    // With a version, one space then the version.
    FirmwareInfo files;
    files.files = FirmwareFiles::Present;
    files.infoVersion = QStringLiteral("1.2");
    log.clear();
    identifyFirmware(files, QMap<QString, QString>(), RunningFirmware::Unknown, &log);
    QCOMPARE(log, QStringList{QStringLiteral("firmware: custom 1.2")});
}

QTEST_GUILESS_MAIN(TstDeviceFirmware)
#include "tst_device_firmware.moc"
