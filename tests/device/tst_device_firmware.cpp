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

IpodEvidence classicWith(const MarkerReading &markers)
{
    const UsbMatch match = matchUsbId(g_models, QStringLiteral("05ac:1261"));
    UsbDevice usb;
    usb.usbId = QStringLiteral("05ac:1261");
    usb.mode = match.mode;
    usb.candidates = match.candidates;
    usb.portPath = QStringLiteral("1-2");

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
    QCOMPARE(firmwareMessage(result.ipod), QStringLiteral("Firmware unknown"));
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
    QCOMPARE(identifyFirmware(files, QMap<QString, QString>()).version, QStringLiteral("0.3"));
    files.version = QStringLiteral("tacetos 0.3");
    QCOMPARE(identifyFirmware(files, QMap<QString, QString>()).version, QStringLiteral("0.3"));
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

void TstDeviceFirmware::messages()
{
    DetectedIpod ipod;
    ipod.firmwareLine = {FirmwareKind::Tacetos, QString()};
    QCOMPARE(firmwareMessage(ipod), QStringLiteral("TacetOS"));
    ipod.firmwareLine = {FirmwareKind::Custom, QString()};
    QCOMPARE(firmwareMessage(ipod), QStringLiteral("Custom firmware"));
    ipod.firmwareLine = {FirmwareKind::Unknown, QStringLiteral("9.9")};
    QCOMPARE(firmwareMessage(ipod), QStringLiteral("Firmware unknown"));
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
    const FirmwareLine unknown = identifyFirmware(FirmwareInfo(), QMap<QString, QString>(), &log);
    QCOMPARE(unknown.kind, FirmwareKind::Unknown);
    QCOMPARE(log, QStringList{QStringLiteral("firmware: unknown")});

    // With a version, one space then the version.
    FirmwareInfo files;
    files.files = FirmwareFiles::Present;
    files.infoVersion = QStringLiteral("1.2");
    log.clear();
    identifyFirmware(files, QMap<QString, QString>(), &log);
    QCOMPARE(log, QStringList{QStringLiteral("firmware: custom 1.2")});
}

QTEST_GUILESS_MAIN(TstDeviceFirmware)
#include "tst_device_firmware.moc"
