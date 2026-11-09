// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The exact iPod inside a model (docs/specs/detection.md section 8.4). The
// models are the real ones of models/models.toml. The Apple model files in
// fixtures/sysinfo are synthetic: the one of the real Classic is empty, so the
// key and the layout come from the spec only (section 13).

#include "constants.h"
#include "markers.h"
#include "merge.h"
#include "messages.h"
#include "models.h"
#include "variant.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

QList<Model> g_models;

Model modelOf(const QString &id)
{
    for (const Model &m : g_models) {
        if (m.id == id)
            return m;
    }
    qFatal("no model %s", qPrintable(id));
}

// A disk as the OS reports it: a little more than the box says for a hard
// disk.
qint64 diskOf(double gigabytes)
{
    return static_cast<qint64>(gigabytes * 1e9);
}

// A music volume with `fixture` (a file of fixtures/sysinfo) as the Apple model
// file, or none when `fixture` is empty. Returns the reading of the markers.
MarkerReading readVolumeWith(QTemporaryDir &dir, const QString &fixture)
{
    if (!fixture.isEmpty()) {
        const QString device = dir.filePath(QStringLiteral("iPod_Control/Device"));
        QDir().mkpath(device);
        const QString from = QStringLiteral(TACETOS_DEVICE_FIXTURES "/sysinfo/") + fixture;
        QFile::copy(from, device + QStringLiteral("/SysInfo"));
    }
    return readMarkers(dir.path(), g_models);
}

IpodEvidence pluggedWith(const QString &usbId, qint64 diskBytes, const MarkerReading &markers)
{
    const UsbMatch match = matchUsbId(g_models, usbId);
    UsbDevice usb;
    usb.usbId = usbId;
    usb.mode = match.mode;
    usb.candidates = match.candidates;
    usb.portPath = QStringLiteral("1-2");

    IpodEvidence ev;
    ev.usb = usb;
    DiskInfo disk;
    disk.node = QStringLiteral("/dev/sdb");
    disk.sizeBytes = diskBytes;
    disk.sectorSize = 4096;
    ev.disk = disk;
    VolumeInfo volume;
    volume.mountPoint = QStringLiteral("/run/media/ipod");
    volume.filesystem = QStringLiteral("vfat");
    volume.totalBytes = diskBytes - 2'000'000'000;
    ev.volume = volume;
    ev.markers = markers;
    ev.waitOver = true;
    return ev;
}

} // namespace

class TstDeviceVariant : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void modelsHaveTheirVariants();
    void modelNumber_data();
    void modelNumber();
    void capacityWindow_data();
    void capacityWindow();

    void classicByCapacity_data();
    void classicByCapacity();
    void classic160IsOneNameTwoYears();
    void miniSecondGenerationSixGb();
    void miniFourGbIsNotGuessed();
    void swappedStorageGivesNoVariant();
    void sectorsAreNotUsed();

    void sysInfoBeatsCapacity();
    void sysInfoWithAnotherDiskKeepsTheVariant();
    void sysInfoOfAnotherModelIsIgnored();
    void emptyOrUnknownSysInfoFallsBack();
    void fixtureFilesThroughTheMarkers_data();
    void fixtureFilesThroughTheMarkers();

    void mergeGivesTheDisplayedName();
    void mergeKeepsTheVariantOutOfTheDecision();
    void mergeHintFromCapacity();
    void dfuKeepsTheVariant();
};

void TstDeviceVariant::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

void TstDeviceVariant::modelsHaveTheirVariants()
{
    const Model classic = modelOf(QStringLiteral("classic-6g"));
    QCOMPARE(classic.variants.size(), 4);
    for (const Model &m : g_models) {
        QVERIFY2(!m.variants.isEmpty(), qPrintable(m.id));
        for (const Variant &v : m.variants) {
            QVERIFY2(!v.name.isEmpty() && v.capacityGb > 0 && !v.appleModels.isEmpty(),
                     qPrintable(m.id + "/" + v.key));
        }
    }
    bool found = false;
    for (const Variant &v : classic.variants) {
        if (v.key == QLatin1String("120gb")) {
            found = true;
            QCOMPARE(v.name, QStringLiteral("iPod Classic 120 GB"));
            QCOMPARE(v.released, QStringLiteral("late 2008"));
            QCOMPARE(v.capacityGb, 120);
            QCOMPARE(v.appleModels, (QStringList{QStringLiteral("B562"), QStringLiteral("B565")}));
        }
    }
    QVERIFY(found);
}

void TstDeviceVariant::modelNumber_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("number");

    QTest::newRow("region letter") << "xB562" << "B562";
    QTest::newRow("lower case") << "xb562" << "B562";
    QTest::newRow("spaces") << "  xB562 " << "B562";
    QTest::newRow("mini, letter then digits") << "M9801" << "9801";
    QTest::newRow("no letter first") << "9801" << "9801";
    QTest::newRow("empty") << "" << "";
    QTest::newRow("only a letter") << "x" << "";
}

void TstDeviceVariant::modelNumber()
{
    QFETCH(QString, raw);
    QFETCH(QString, number);
    QCOMPARE(appleModelNumber(raw), number);
}

void TstDeviceVariant::capacityWindow_data()
{
    QTest::addColumn<int>("capacityGb");
    QTest::addColumn<double>("sizeGb");
    QTest::addColumn<bool>("matches");

    QTest::newRow("120 GB drive") << 120 << 120.03 << true;
    QTest::newRow("flash a little under") << 4 << 3.7 << true;
    QTest::newRow("low edge") << 100 << 90.0 << true;
    QTest::newRow("under") << 100 << 89.0 << false;
    QTest::newRow("high edge") << 100 << 105.0 << true;
    QTest::newRow("over") << 100 << 106.0 << false;
    QTest::newRow("no size") << 100 << 0.0 << false;
}

void TstDeviceVariant::capacityWindow()
{
    QFETCH(int, capacityGb);
    QFETCH(double, sizeGb);
    QFETCH(bool, matches);
    QCOMPARE(capacityMatches(capacityGb, diskOf(sizeGb)), matches);
}

void TstDeviceVariant::classicByCapacity_data()
{
    QTest::addColumn<double>("sizeGb");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("shown");

    QTest::newRow("80 GB, 2007") << 80.03 << "80gb" << "iPod Classic 80 GB";
    QTest::newRow("120 GB, late 2008") << 120.03 << "120gb" << "iPod Classic 120 GB";
}

void TstDeviceVariant::classicByCapacity()
{
    QFETCH(double, sizeGb);
    QFETCH(QString, key);
    QFETCH(QString, shown);
    const VariantMatch match =
        identifyVariant(modelOf(QStringLiteral("classic-6g")), QString(), diskOf(sizeGb));
    QCOMPARE(match.source, VariantSource::Capacity);
    QCOMPARE(match.key, key);
    QCOMPARE(match.name, shown);
    QVERIFY(!match.released.isEmpty());
    QVERIFY(!match.storageChanged);
}

void TstDeviceVariant::classic160IsOneNameTwoYears()
{
    const VariantMatch match =
        identifyVariant(modelOf(QStringLiteral("classic-6g")), QString(), diskOf(160.04));
    // Two variants fit: the name is shown without a year, and no variant.
    QCOMPARE(match.source, VariantSource::Capacity);
    QVERIFY(match.key.isEmpty());
    QVERIFY(match.released.isEmpty());
    QCOMPARE(match.name, QStringLiteral("iPod Classic 160 GB"));

    // The Apple model number tells the two apart.
    const VariantMatch byNumber = identifyVariant(modelOf(QStringLiteral("classic-6g")),
                                                  QStringLiteral("xC293"), diskOf(160.04));
    QCOMPARE(byNumber.source, VariantSource::SysInfo);
    QCOMPARE(byNumber.key, QStringLiteral("160gb-2009"));
    QCOMPARE(byNumber.released, QStringLiteral("late 2009"));
    const VariantMatch thick = identifyVariant(modelOf(QStringLiteral("classic-6g")),
                                               QStringLiteral("xB145"), diskOf(160.04));
    QCOMPARE(thick.key, QStringLiteral("160gb-2007"));
}

void TstDeviceVariant::miniSecondGenerationSixGb()
{
    const qint64 disk = diskOf(6.0);
    const VariantMatch second =
        identifyVariant(modelOf(QStringLiteral("mini-2g")), QString(), disk);
    QCOMPARE(second.source, VariantSource::Capacity);
    QCOMPARE(second.key, QStringLiteral("6gb"));

    // A 6 GB disk in a first generation is not an original one: no variant.
    const VariantMatch first = identifyVariant(modelOf(QStringLiteral("mini-1g")), QString(), disk);
    QCOMPARE(first.source, VariantSource::None);

    // And when the generation is not known yet, the disk size points to one
    // candidate, as a hint only.
    const QStringList candidates = matchUsbId(g_models, QStringLiteral("05ac:1205")).candidates;
    QCOMPARE(candidates, (QStringList{QStringLiteral("mini-1g"), QStringLiteral("mini-2g")}));
    QCOMPARE(modelHintFromCapacity(g_models, candidates, disk), QStringLiteral("mini-2g"));
}

void TstDeviceVariant::miniFourGbIsNotGuessed()
{
    const qint64 disk = diskOf(4.0);
    const QStringList candidates{QStringLiteral("mini-1g"), QStringLiteral("mini-2g")};
    // Both generations were sold with 4 GB: no hint.
    QVERIFY(modelHintFromCapacity(g_models, candidates, disk).isEmpty());
    // Once the generation is known, the variant is.
    QCOMPARE(identifyVariant(modelOf(QStringLiteral("mini-1g")), QString(), disk).source,
             VariantSource::Capacity);
    QCOMPARE(identifyVariant(modelOf(QStringLiteral("mini-2g")), QString(), disk).key,
             QStringLiteral("4gb"));
}

void TstDeviceVariant::swappedStorageGivesNoVariant()
{
    const VariantMatch match =
        identifyVariant(modelOf(QStringLiteral("classic-6g")), QString(), diskOf(256.0));
    QCOMPARE(match.source, VariantSource::None);
    QVERIFY(match.name.isEmpty());
    // Nothing known at all gives nothing either.
    QCOMPARE(identifyVariant(modelOf(QStringLiteral("classic-6g")), QString(), 0).source,
             VariantSource::None);
}

void TstDeviceVariant::sectorsAreNotUsed()
{
    // A Classic of 120 GB has sectors of 4096 bytes: the size in bytes is
    // what counts, whatever the sector count says.
    IpodEvidence ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(120.03), MarkerReading());
    ev.disk->sectorSize = 512;
    QCOMPARE(mergeEvidence(ev, g_models).ipod.variant.key, QStringLiteral("120gb"));
    ev.disk->sectorSize = 4096;
    QCOMPARE(mergeEvidence(ev, g_models).ipod.variant.key, QStringLiteral("120gb"));
}

void TstDeviceVariant::sysInfoBeatsCapacity()
{
    // The number says 80 GB, the disk says 120 GB: the number wins, and the
    // storage is flagged as changed.
    const VariantMatch match = identifyVariant(modelOf(QStringLiteral("classic-6g")),
                                               QStringLiteral("xB029"), diskOf(120.03));
    QCOMPARE(match.source, VariantSource::SysInfo);
    QCOMPARE(match.key, QStringLiteral("80gb"));
    QVERIFY(match.storageChanged);
}

void TstDeviceVariant::sysInfoWithAnotherDiskKeepsTheVariant()
{
    const VariantMatch same = identifyVariant(modelOf(QStringLiteral("classic-6g")),
                                              QStringLiteral("xB562"), diskOf(120.03));
    QCOMPARE(same.key, QStringLiteral("120gb"));
    QVERIFY(!same.storageChanged);

    // No disk size: nothing to compare, no flag.
    const VariantMatch noDisk =
        identifyVariant(modelOf(QStringLiteral("classic-6g")), QStringLiteral("xB562"), 0);
    QCOMPARE(noDisk.key, QStringLiteral("120gb"));
    QVERIFY(!noDisk.storageChanged);
}

void TstDeviceVariant::sysInfoOfAnotherModelIsIgnored()
{
    // 9801 is a Mini variant: for a Classic it names nothing, and the disk
    // decides.
    const VariantMatch match = identifyVariant(modelOf(QStringLiteral("classic-6g")),
                                               QStringLiteral("M9801"), diskOf(120.03));
    QCOMPARE(match.source, VariantSource::Capacity);
    QCOMPARE(match.key, QStringLiteral("120gb"));
}

void TstDeviceVariant::emptyOrUnknownSysInfoFallsBack()
{
    for (const QString &raw : {QString(), QStringLiteral("xZZZZ"), QStringLiteral("x")}) {
        const VariantMatch match =
            identifyVariant(modelOf(QStringLiteral("classic-6g")), raw, diskOf(120.03));
        QCOMPARE(match.key, QStringLiteral("120gb"));
        QCOMPARE(match.source, VariantSource::Capacity);
    }
}

void TstDeviceVariant::fixtureFilesThroughTheMarkers_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<qint64>("disk");
    QTest::addColumn<QString>("key");
    QTest::addColumn<VariantSource>("source");

    QTest::newRow("120 GB file") << "classic-120gb.txt" << diskOf(120.03) << "120gb"
                                 << VariantSource::SysInfo;
    QTest::newRow("80 GB file, CRLF")
        << "classic-80gb-crlf.txt" << diskOf(80.03) << "80gb" << VariantSource::SysInfo;
    QTest::newRow("160 GB file of 2009")
        << "classic-160gb-2009.txt" << diskOf(160.04) << "160gb-2009" << VariantSource::SysInfo;
    QTest::newRow("empty file (the real Classic)")
        << "empty.txt" << diskOf(120.03) << "120gb" << VariantSource::Capacity;
    QTest::newRow("unknown number")
        << "unknown-number.txt" << diskOf(120.03) << "120gb" << VariantSource::Capacity;
    QTest::newRow("no file") << "" << diskOf(120.03) << "120gb" << VariantSource::Capacity;
}

void TstDeviceVariant::fixtureFilesThroughTheMarkers()
{
    QFETCH(QString, fixture);
    QFETCH(qint64, disk);
    QFETCH(QString, key);
    QFETCH(VariantSource, source);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const MarkerReading markers = readVolumeWith(dir, fixture);
    const IpodEvidence ev = pluggedWith(QStringLiteral("05ac:1261"), disk, markers);
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(ipod.variant.key, key);
    QCOMPARE(ipod.variant.source, source);
    QCOMPARE(ipod.state, IpodState::Ready);
}

void TstDeviceVariant::mergeGivesTheDisplayedName()
{
    IpodEvidence ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(120.03), MarkerReading());
    DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(stateMessage(ipod, g_models),
             QStringLiteral("iPod Classic 120 GB (late 2008), Windows format"));

    // The 160 GB: the name only.
    ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(160.04), MarkerReading());
    ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(stateMessage(ipod, g_models), QStringLiteral("iPod Classic 160 GB, Windows format"));

    // No variant fits: the model and the real size.
    ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(256.0), MarkerReading());
    ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(stateMessage(ipod, g_models),
             QStringLiteral("iPod Classic (6th generation), 256 GB, Windows format"));

    // The number says 80 GB on a 256 GB storage: the variant, and the real size.
    MarkerReading markers;
    markers.appleModelFields.insert(QString(kSysInfoModelKey), QStringLiteral("xB029"));
    ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(256.0), markers);
    const MergeResult result = mergeEvidence(ev, g_models);
    QVERIFY(result.ipod.variant.storageChanged);
    QCOMPARE(stateMessage(result.ipod, g_models),
             QStringLiteral("iPod Classic 80 GB (late 2007), 256 GB, Windows format"));
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QLatin1String("storage changed")));
}

void TstDeviceVariant::mergeKeepsTheVariantOutOfTheDecision()
{
    // A number of a Mini on a Classic does not change the model or the state.
    MarkerReading markers;
    markers.appleModelFields.insert(QString(kSysInfoModelKey), QStringLiteral("M9801"));
    const IpodEvidence ev = pluggedWith(QStringLiteral("05ac:1261"), diskOf(120.03), markers);
    const MergeResult result = mergeEvidence(ev, g_models);
    QCOMPARE(result.ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(result.ipod.state, IpodState::Ready);
    QCOMPARE(result.ipod.variant.key, QStringLiteral("120gb"));
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QLatin1String("is of mini-2g")));
}

void TstDeviceVariant::mergeHintFromCapacity()
{
    // Mini, 6 GB: the question stays, with the hint, and no variant yet.
    IpodEvidence ev = pluggedWith(QStringLiteral("05ac:1205"), diskOf(6.0), MarkerReading());
    DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ambiguous);
    QVERIFY(ipod.model.isEmpty());
    QCOMPARE(ipod.hintModel, QStringLiteral("mini-2g"));
    QCOMPARE(ipod.variant.source, VariantSource::None);
    QVERIFY(stateMessage(ipod, g_models).contains(QLatin1String("A disk of 6 GB is usually the")));

    // Mini, 4 GB: nothing is guessed.
    ev = pluggedWith(QStringLiteral("05ac:1205"), diskOf(4.0), MarkerReading());
    ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ambiguous);
    QVERIFY(ipod.hintModel.isEmpty());
    QCOMPARE(stateMessage(ipod, g_models),
             QStringLiteral("Several iPod models share this connection. Which one is yours?"));

    // The user picks the generation: the variant follows.
    ev.userModel = QStringLiteral("mini-1g");
    ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.model, QStringLiteral("mini-1g"));
    QCOMPARE(ipod.variant.key, QStringLiteral("4gb"));
}

void TstDeviceVariant::dfuKeepsTheVariant()
{
    const IpodEvidence normal =
        pluggedWith(QStringLiteral("05ac:1261"), diskOf(120.03), MarkerReading());
    PreviousIpod previous;
    previous.ipod = mergeEvidence(normal, g_models).ipod;
    previous.ageMs = 1000;
    QCOMPARE(previous.ipod.variant.key, QStringLiteral("120gb"));

    IpodEvidence dfu;
    const UsbMatch match = matchUsbId(g_models, QStringLiteral("05ac:1223"));
    UsbDevice usb;
    usb.usbId = QStringLiteral("05ac:1223");
    usb.mode = match.mode;
    usb.candidates = match.candidates;
    usb.portPath = QStringLiteral("1-2");
    dfu.usb = usb;
    dfu.previous = previous;
    const DetectedIpod ipod = mergeEvidence(dfu, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Dfu);
    QCOMPARE(ipod.variant.key, QStringLiteral("120gb"));
}

QTEST_GUILESS_MAIN(TstDeviceVariant)
#include "tst_device_variant.moc"
