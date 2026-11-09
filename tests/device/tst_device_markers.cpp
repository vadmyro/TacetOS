// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Marker files on the music volume (docs/specs/detection.md sections 7 and
// 12.3). Volumes are built in a temporary folder, and the info files are
// synthetic: written for these tests, not taken from any build.

#include "constants.h"
#include "markers.h"
#include "models.h"
#include "symlinks.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

const QString kFolder{kFirmwareFolder};
const QString kInfoPath = kFolder + QLatin1Char('/') + QString(kFirmwareInfoFile);

void writeBytes(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(content);
}

QList<Model> testModels()
{
    Model classic;
    classic.id = "classic-6g";
    classic.bootloaderTarget = "ipod6g";
    Model video;
    video.id = "video-5g";
    video.bootloaderTarget = "ipodvideo";
    return {classic, video};
}

} // namespace

class TstDeviceMarkers : public QObject
{
    Q_OBJECT

private slots:
    void parsesSyntheticInfoFile();
    void parseRules_data();
    void parseRules();
    void distributionKeyIsOptional();

    void appleFolderInCapitals();
    void hiddenFolderAndCrLfInfoFile();
    void infoFileFromFixture();
    void infoFileWithSomeKeys();
    void infoFileWithUnknownTarget();
    void infoFileWithZeroByte();
    void infoFileOfOneMebibyte();
    void infoFileIsAFolder();
    void firmwareFolderWithoutInfoFile();
    void appleFolderOnly();
    void emptyVolume();
    void volumeVanishes();
    void linksAreNotFollowed();
    void nothingIsWritten();
    void modelsForTargetIgnoresCase();
    void constantsAreNotEmpty();
};

void TstDeviceMarkers::parsesSyntheticInfoFile()
{
    QFile file(QStringLiteral(TACETOS_DEVICE_FIXTURES "/info-synthetic.txt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const InfoFile info = parseInfoFile(QString::fromUtf8(file.readAll()), QString());
    QCOMPARE(info.target, QString("ipod6g"));
    QCOMPARE(info.targetId, QString("71"));
    QCOMPARE(info.version, QString("4.0"));
    QCOMPARE(info.binary, QString("tacetos.ipod"));
    QVERIFY(info.distribution.isEmpty());
}

void TstDeviceMarkers::parseRules_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("target");
    QTest::addColumn<QString>("version");

    QTest::newRow("last one wins") << "Target: a\nTarget: b\n" << "b" << "";
    QTest::newRow("crlf") << "Target: ipod6g\r\nVersion: 4.0\r\n" << "ipod6g" << "4.0";
    QTest::newRow("no white space after the colon") << "Target:ipod6g\n" << "" << "";
    QTest::newRow("lower case key") << "target: ipod6g\n" << "" << "";
    QTest::newRow("key with digits") << "Target2: ipod6g\n" << "" << "";
    QTest::newRow("no colon") << "Target ipod6g\n" << "" << "";
    QTest::newRow("empty") << "" << "" << "";
    QTest::newRow("value keeps inner colons") << "Version: 4.0: test\n" << "" << "4.0: test";
    QTest::newRow("extra white space") << "Target:   ipod6g   \n" << "ipod6g" << "";
}

void TstDeviceMarkers::parseRules()
{
    QFETCH(QString, text);
    QFETCH(QString, target);
    QFETCH(QString, version);
    const InfoFile info = parseInfoFile(text, QString());
    QCOMPARE(info.target, target);
    QCOMPARE(info.version, version);
}

void TstDeviceMarkers::distributionKeyIsOptional()
{
    const QString text = "Target: ipod6g\nDistro: TacetOS 0.3\n";
    QCOMPARE(parseInfoFile(text, QString()).distribution, QString());
    QCOMPARE(parseInfoFile(text, "Distro").distribution, QString("TacetOS 0.3"));
    QCOMPARE(parseInfoFile(text, "Other").distribution, QString());
}

void TstDeviceMarkers::appleFolderInCapitals()
{
    QTemporaryDir dir;
    writeBytes(dir.filePath("IPOD_CONTROL/DEVICE/SYSINFO"),
               "ModelNumStr: xA446\r\npszSerialNumber: ABC123\r\n");
    writeBytes(dir.filePath("IPOD_CONTROL/ITUNES/ITUNESDB"), "x");
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QVERIFY(r.appleFolder);
    QVERIFY(r.appleModelFile);
    QVERIFY(r.appleDatabase);
    QCOMPARE(r.appleModelFields.value("ModelNumStr"), QString("xA446"));
    QCOMPARE(r.appleModelFields.value("pszSerialNumber"), QString("ABC123"));
    QVERIFY(!r.volumeGone);
    QCOMPARE(r.firmware.files, FirmwareFiles::Absent);
}

void TstDeviceMarkers::hiddenFolderAndCrLfInfoFile()
{
    QTemporaryDir dir;
    // The folder name starts with a dot: it is a hidden entry on Linux.
    writeBytes(dir.filePath(kInfoPath.toUpper()),
               "Version: 4.0\r\nTarget: ipod6g\r\nTarget id: 71\r\n");
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QCOMPARE(r.firmware.files, FirmwareFiles::Present);
    QCOMPARE(r.info.target, QString("ipod6g"));
    QCOMPARE(r.modelsFromInfo, QStringList{"classic-6g"});
    QVERIFY(!r.appleFolder);
}

void TstDeviceMarkers::infoFileFromFixture()
{
    QTemporaryDir dir;
    QVERIFY(QFile::copy(QStringLiteral(TACETOS_DEVICE_FIXTURES "/info-synthetic.txt"), [&] {
        QDir().mkpath(dir.filePath(kFolder));
        return dir.filePath(kInfoPath);
    }()));
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QCOMPARE(r.firmware.files, FirmwareFiles::Present);
    QVERIFY(r.firmware.version.isEmpty());
    QCOMPARE(r.info.version, QString("4.0"));
    QCOMPARE(r.info.binary, QString("tacetos.ipod"));
    QCOMPARE(r.modelsFromInfo, QStringList{"classic-6g"});
}

void TstDeviceMarkers::infoFileWithSomeKeys()
{
    QTemporaryDir dir;
    writeBytes(dir.filePath(kInfoPath), "Target: ipodvideo\n");
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QCOMPARE(r.firmware.files, FirmwareFiles::Present);
    QCOMPARE(r.modelsFromInfo, QStringList{"video-5g"});
    QVERIFY(r.info.version.isEmpty());

    // Without a Target key the file is not valid.
    writeBytes(dir.filePath(kInfoPath), "Version: 4.0\n");
    QCOMPARE(readMarkers(dir.path(), testModels()).firmware.files, FirmwareFiles::Unknown);
}

void TstDeviceMarkers::infoFileWithUnknownTarget()
{
    QTemporaryDir dir;
    writeBytes(dir.filePath(kInfoPath), "Target: nosuchtarget\nVersion: 4.0\n");
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QCOMPARE(r.firmware.files, FirmwareFiles::Unknown);
    QVERIFY(r.modelsFromInfo.isEmpty());
    QVERIFY(!r.volumeGone);
}

void TstDeviceMarkers::infoFileWithZeroByte()
{
    QTemporaryDir dir;
    writeBytes(dir.filePath(kInfoPath), QByteArray("Target: ipod6g\n\0Version: 4.0\n", 29));
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::NotText);
    QCOMPARE(readMarkers(dir.path(), testModels()).firmware.files, FirmwareFiles::Unknown);
}

void TstDeviceMarkers::infoFileOfOneMebibyte()
{
    QTemporaryDir dir;
    QByteArray big("Target: ipod6g\n");
    big.append(QByteArray(1024 * 1024, 'x'));
    writeBytes(dir.filePath(kInfoPath), big);
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::TooLarge);
    QCOMPARE(readMarkers(dir.path(), testModels()).firmware.files, FirmwareFiles::Unknown);

    // Exactly at the limit is still read; one byte more is not.
    writeBytes(dir.filePath(kInfoPath), QByteArray(10, 'a'));
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath, 10).status, ReadStatus::Ok);
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath, 9).status, ReadStatus::TooLarge);
}

void TstDeviceMarkers::infoFileIsAFolder()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath(kInfoPath)));
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::Unreadable);
    QCOMPARE(readMarkers(dir.path(), testModels()).firmware.files, FirmwareFiles::Unknown);
}

void TstDeviceMarkers::firmwareFolderWithoutInfoFile()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath(kFolder)));
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::Missing);
    QCOMPARE(readMarkers(dir.path(), testModels()).firmware.files, FirmwareFiles::Unknown);
}

void TstDeviceMarkers::appleFolderOnly()
{
    QTemporaryDir dir;
    QVERIFY(QDir().mkpath(dir.filePath("iPod_Control")));
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QVERIFY(r.appleFolder);
    QVERIFY(!r.appleModelFile);
    QVERIFY(!r.appleDatabase);
    QCOMPARE(r.firmware.files, FirmwareFiles::Absent);
    QVERIFY(!r.volumeGone);
}

void TstDeviceMarkers::emptyVolume()
{
    QTemporaryDir dir;
    const MarkerReading r = readMarkers(dir.path(), testModels());
    QVERIFY(!r.appleFolder);
    QVERIFY(!r.appleModelFile);
    QVERIFY(!r.appleDatabase);
    QCOMPARE(r.firmware.files, FirmwareFiles::Absent);
    QVERIFY(!r.volumeGone);
}

void TstDeviceMarkers::volumeVanishes()
{
    QString gone;
    {
        QTemporaryDir dir;
        gone = dir.path();
        writeBytes(dir.filePath(kInfoPath), "Target: ipod6g\n");
    }
    // The folder is gone: not an error to show, the end of that iPod.
    QVERIFY(!QFileInfo::exists(gone));
    QVERIFY(readMarkers(gone, testModels()).volumeGone);
    QCOMPARE(readMarkerFile(gone, kInfoPath).status, ReadStatus::VolumeGone);

    // A file that is not there on a volume that is: only missing.
    QTemporaryDir dir;
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::Missing);
}

void TstDeviceMarkers::linksAreNotFollowed()
{
    TACETOS_NEEDS_SYMLINKS();
    QTemporaryDir outside;
    writeBytes(outside.filePath("secret.txt"), "Target: ipod6g\n");

    QTemporaryDir dir;
    // A firmware folder that is a link to a folder outside the volume.
    QVERIFY(QFile::link(outside.path(), dir.filePath(kFolder)));
    writeBytes(outside.filePath(QString(kFirmwareInfoFile)), "Target: ipod6g\n");
    QVERIFY(findMarkerPath(dir.path(), kInfoPath).isEmpty());
    QCOMPARE(readMarkerFile(dir.path(), kInfoPath).status, ReadStatus::Missing);

    // An info file that is a link to a file outside the volume.
    QTemporaryDir dir2;
    QVERIFY(QDir().mkpath(dir2.filePath(kFolder)));
    QVERIFY(QFile::link(outside.filePath("secret.txt"), dir2.filePath(kInfoPath)));
    QVERIFY(findMarkerPath(dir2.path(), kInfoPath).isEmpty());
    QCOMPARE(readMarkers(dir2.path(), testModels()).firmware.files, FirmwareFiles::Unknown);
}

void TstDeviceMarkers::nothingIsWritten()
{
    QTemporaryDir dir;
    writeBytes(dir.filePath(kInfoPath), "Target: ipod6g\n");
    writeBytes(dir.filePath("iPod_Control/Device/SysInfo"), "ModelNumStr: x\n");

    const auto snapshot = [&] {
        QStringList names;
        QDirIterator it(dir.path(),
                        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            names << it.filePath() + QLatin1Char('|') + QString::number(it.fileInfo().size()) +
                         QLatin1Char('|') +
                         it.fileInfo().lastModified().toString(Qt::ISODateWithMs);
        }
        names.sort();
        return names;
    };
    const QStringList before = snapshot();
    readMarkers(dir.path(), testModels());
    QCOMPARE(snapshot(), before);
}

void TstDeviceMarkers::modelsForTargetIgnoresCase()
{
    QCOMPARE(modelsForTarget(testModels(), "IPOD6G"), QStringList{"classic-6g"});
    QVERIFY(modelsForTarget(testModels(), "").isEmpty());
    QVERIFY(modelsForTarget(testModels(), "unknown").isEmpty());
}

void TstDeviceMarkers::constantsAreNotEmpty()
{
    QVERIFY(!QString(kFirmwareFolder).isEmpty());
    QVERIFY(!QString(kFirmwareInfoFile).isEmpty());
    QCOMPARE(kMarkerMaxBytes, qint64(64 * 1024));
}

QTEST_GUILESS_MAIN(TstDeviceMarkers)
#include "tst_device_markers.moc"
