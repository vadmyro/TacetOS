// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Merging the evidence (docs/specs/detection.md sections 8, 10 and 12.4): a
// list of evidence in, the detected iPod out. The models are the real ones of
// models/models.toml; nothing reads a system.

#include "constants.h"
#include "merge.h"
#include "messages.h"
#include "models.h"

#include <QTest>

using namespace tacetos::device;

Q_DECLARE_METATYPE(tacetos::device::IpodState)
Q_DECLARE_METATYPE(tacetos::device::ModelSource)

namespace {

QList<Model> g_models;

UsbDevice usbDevice(const QString &usbId, const QString &port = QStringLiteral("1-2"))
{
    const UsbMatch match = matchUsbId(g_models, usbId);
    UsbDevice device;
    device.usbId = usbId;
    device.mode = match.mode;
    device.candidates = match.candidates;
    device.portPath = port;
    return device;
}

DiskInfo someDisk()
{
    DiskInfo disk;
    disk.node = QStringLiteral("/dev/sdb");
    disk.sizeBytes = 120'000'000'000;
    disk.sectorSize = 512;
    return disk;
}

VolumeInfo someVolume(const QString &filesystem = QStringLiteral("vfat"))
{
    VolumeInfo volume;
    volume.mountPoint = QStringLiteral("/run/media/ipod");
    volume.filesystem = filesystem;
    volume.totalBytes = 118'000'000'000;
    volume.freeBytes = 50'000'000'000;
    return volume;
}

// An iPod that is plugged in, with a disk and a mounted FAT32 volume.
IpodEvidence plugged(const QString &usbId)
{
    IpodEvidence ev;
    ev.usb = usbDevice(usbId);
    ev.disk = someDisk();
    ev.volume = someVolume();
    ev.waitOver = true;
    return ev;
}

DiskReading readingOf(const QString &target)
{
    DiskReading reading;
    reading.status = DiskReadStatus::Ok;
    reading.target = target;
    return reading;
}

// Marker files that carry a valid info file of this model, as readMarkers
// would have filled them in.
MarkerReading infoOf(const QString &modelId, FirmwareFiles files = FirmwareFiles::Present)
{
    MarkerReading markers;
    markers.modelsFromInfo = {modelId};
    markers.firmware.files = files;
    return markers;
}

} // namespace

class TstDeviceMerge : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void stateTable_data();
    void stateTable();

    void usbCandidateDecidesTheModel();
    void minisWithDiskReading();
    void minisWithInfoFileOnly();
    void minisWithNothing();
    void minisUserChoice();
    void userChoiceOutsideCandidatesIsIgnored();
    void diskReadingOutsideCandidatesIsConflict();
    void diskReadingAgreesWithUsb();
    void infoFileOfAnotherModel();
    void infoFileOfTheSameModel();
    void infoFileDoesNotChangeTheModel();
    void firmwareIsCarriedOver();

    void dfuAfterNormalOnTheSamePort();
    void dfuAfterNormalTooLongAgo();
    void dfuAfterNormalOnAnotherPort();
    void dfuAfterDfuIsNotNormal();
    void dfuAlone();

    void pickedFolderWithInfoFile();
    void pickedFolderWithoutInfoFile();
    void pickedFolderWithUserModel();

    void volumeGoneIsNoVolume();
    void unsupportedFilesystemReadsNothing();
    void waitingEndsWithTheVolume();
    void twoIpodsAtOnce();

    void everyStateHasAMessage();
    void readyMessage();
    void permissionMessagePerSystem();
};

void TstDeviceMerge::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    g_models = list.models;
}

void TstDeviceMerge::stateTable_data()
{
    QTest::addColumn<IpodEvidence>("evidence");
    QTest::addColumn<IpodState>("state");
    QTest::addColumn<QString>("model");

    {
        IpodEvidence ev;
        ev.usb = usbDevice(QStringLiteral("05ac:1261"));
        QTest::newRow("waiting: just plugged in") << ev << IpodState::Waiting << "classic-6g";
    }
    {
        IpodEvidence ev;
        ev.usb = usbDevice(QStringLiteral("05ac:1261"));
        ev.disk = someDisk();
        QTest::newRow("waiting: disk but no volume yet")
            << ev << IpodState::Waiting << "classic-6g";
    }
    QTest::newRow("ready: classic")
        << plugged(QStringLiteral("05ac:1261")) << IpodState::Ready << "classic-6g";
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading = readingOf(QStringLiteral("ipodvideo"));
        QTest::newRow("ready: video with disk reading") << ev << IpodState::Ready << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
        ev.disk.reset();
        ev.volume.reset();
        QTest::newRow("no-disk: wait over") << ev << IpodState::NoDisk << "classic-6g";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
        ev.volume.reset();
        QTest::newRow("no-volume: disk, nothing mounted")
            << ev << IpodState::NoVolume << "classic-6g";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.volume.reset();
        ev.diskReading = readingOf(QStringLiteral("ipodvideo"));
        ev.diskReading.macFormat = true;
        QTest::newRow("mac-format: disk reading, no volume (Windows)")
            << ev << IpodState::MacFormat << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.volume = someVolume(QStringLiteral("hfsplus"));
        QTest::newRow("mac-format: HFS+ volume mounted")
            << ev << IpodState::MacFormat << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
        ev.volume = someVolume(QStringLiteral("exfat"));
        QTest::newRow("unsupported-filesystem")
            << ev << IpodState::UnsupportedFilesystem << "classic-6g";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
        QTest::newRow("ambiguous: minis, nothing to tell them apart")
            << ev << IpodState::Ambiguous << "";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading = readingOf(QStringLiteral("ipodnano1g"));
        QTest::newRow("conflict: disk says another model")
            << ev << IpodState::Conflict << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading.status = DiskReadStatus::UnknownGeneration;
        QTest::newRow("unsupported-model: unknown generation")
            << ev << IpodState::UnsupportedModel << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading = readingOf(QStringLiteral("ipod2g-not-in-models"));
        QTest::newRow("unsupported-model: generation with no model")
            << ev << IpodState::UnsupportedModel << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading.status = DiskReadStatus::PermissionDenied;
        QTest::newRow("permission: read refused") << ev << IpodState::Permission << "ipod-video";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
        ev.diskReading.status = DiskReadStatus::PermissionDenied;
        QTest::newRow("permission before ambiguous (minis)") << ev << IpodState::Permission << "";
    }
    {
        IpodEvidence ev;
        ev.usb = usbDevice(QStringLiteral("05ac:1223"));
        QTest::newRow("dfu: alone") << ev << IpodState::Dfu << "";
    }
    {
        IpodEvidence ev;
        ev.usb = usbDevice(QStringLiteral("05ac:1205"));
        QTest::newRow("waiting before ambiguous (minis, still waiting)")
            << ev << IpodState::Waiting << "";
    }
    {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
        ev.diskReading = readingOf(QStringLiteral("ipodnano1g"));
        ev.diskReading.macFormat = true;
        QTest::newRow("conflict before mac-format") << ev << IpodState::Conflict << "ipod-video";
    }
}

void TstDeviceMerge::stateTable()
{
    QFETCH(IpodEvidence, evidence);
    QFETCH(IpodState, state);
    QFETCH(QString, model);

    const MergeResult result = mergeEvidence(evidence, g_models);
    QCOMPARE(result.ipod.state, state);
    QCOMPARE(result.ipod.model, model);
}

void TstDeviceMerge::usbCandidateDecidesTheModel()
{
    const DetectedIpod ipod = mergeEvidence(plugged(QStringLiteral("05ac:1261")), g_models).ipod;
    QCOMPARE(ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(ipod.modelSource, ModelSource::Usb);
    QCOMPARE(ipod.usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(ipod.mode, UsbMode::Normal);
    QCOMPARE(ipod.portPath, QStringLiteral("1-2"));
    QVERIFY(ipod.disk);
    QVERIFY(ipod.volume);
}

void TstDeviceMerge::minisWithDiskReading()
{
    struct Case
    {
        const char *target;
        const char *model;
    };
    for (const Case &c : {Case{"ipodmini1g", "mini-1g"}, Case{"ipodmini2g", "mini-2g"}}) {
        IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
        ev.diskReading = readingOf(QString::fromLatin1(c.target));
        const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
        QCOMPARE(ipod.state, IpodState::Ready);
        QCOMPARE(ipod.model, QString::fromLatin1(c.model));
        QCOMPARE(ipod.modelSource, ModelSource::Disk);
        QCOMPARE(ipod.candidates.size(), 2);
    }
}

void TstDeviceMerge::minisWithInfoFileOnly()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
    ev.markers = infoOf(QStringLiteral("mini-2g"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ready);
    QCOMPARE(ipod.model, QStringLiteral("mini-2g"));
    QCOMPARE(ipod.modelSource, ModelSource::InfoFile);
}

void TstDeviceMerge::minisWithNothing()
{
    // An info file of a model that is not a candidate does not decide.
    IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
    ev.markers = infoOf(QStringLiteral("ipod-video"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ambiguous);
    QVERIFY(ipod.model.isEmpty());
    QCOMPARE(ipod.modelSource, ModelSource::None);
    QVERIFY(!ipod.firmware.otherModel);
}

void TstDeviceMerge::minisUserChoice()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
    ev.userModel = QStringLiteral("mini-1g");
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ready);
    QCOMPARE(ipod.model, QStringLiteral("mini-1g"));
    QCOMPARE(ipod.modelSource, ModelSource::User);

    // The choice wins over the info file (section 8.1).
    ev.markers = infoOf(QStringLiteral("mini-2g"));
    const DetectedIpod other = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(other.model, QStringLiteral("mini-1g"));
    QCOMPARE(other.modelSource, ModelSource::User);
    QVERIFY(other.firmware.otherModel);
}

void TstDeviceMerge::userChoiceOutsideCandidatesIsIgnored()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.userModel = QStringLiteral("ipod-video");
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(ipod.modelSource, ModelSource::Usb);
}

void TstDeviceMerge::diskReadingOutsideCandidatesIsConflict()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1205"));
    ev.diskReading = readingOf(QStringLiteral("ipodvideo"));
    const MergeResult result = mergeEvidence(ev, g_models);
    QCOMPARE(result.ipod.state, IpodState::Conflict);
    // Nothing to guess from: the model stays empty.
    QVERIFY(result.ipod.model.isEmpty());
    // The log keeps both values.
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QStringLiteral("mini-1g")));
    QVERIFY(result.log.join(QLatin1Char('\n')).contains(QStringLiteral("ipod-video")));
}

void TstDeviceMerge::diskReadingAgreesWithUsb()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1209"));
    ev.diskReading = readingOf(QStringLiteral("ipodvideo"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ready);
    // Rule 1 comes first: the USB id is the source.
    QCOMPARE(ipod.modelSource, ModelSource::Usb);
}

void TstDeviceMerge::infoFileOfAnotherModel()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.markers = infoOf(QStringLiteral("ipod-video"), FirmwareFiles::Present);
    const MergeResult result = mergeEvidence(ev, g_models);
    QVERIFY(result.ipod.firmware.otherModel);
    QCOMPARE(result.ipod.firmware.files, FirmwareFiles::Present);
    QVERIFY(!result.log.isEmpty());
}

void TstDeviceMerge::infoFileOfTheSameModel()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.markers = infoOf(QStringLiteral("classic-6g"));
    QVERIFY(!mergeEvidence(ev, g_models).ipod.firmware.otherModel);
}

void TstDeviceMerge::infoFileDoesNotChangeTheModel()
{
    // Rule 4: the decision of rules 1 to 3 stays, and the state stays ready.
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.markers = infoOf(QStringLiteral("nano-2g"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(ipod.modelSource, ModelSource::Usb);
    QCOMPARE(ipod.state, IpodState::Ready);
}

void TstDeviceMerge::firmwareIsCarriedOver()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.markers = infoOf(QStringLiteral("classic-6g"), FirmwareFiles::Tacetos);
    ev.markers.firmware.version = QStringLiteral("0.3");
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.firmware.files, FirmwareFiles::Tacetos);
    QCOMPARE(ipod.firmware.version, QStringLiteral("0.3"));
}

void TstDeviceMerge::dfuAfterNormalOnTheSamePort()
{
    PreviousIpod previous;
    previous.ipod = mergeEvidence(plugged(QStringLiteral("05ac:1261")), g_models).ipod;
    previous.ipod.firmware.files = FirmwareFiles::Tacetos;
    previous.ipod.firmware.version = QStringLiteral("0.3");
    previous.ageMs = 5000;

    IpodEvidence ev;
    ev.usb = usbDevice(QStringLiteral("05ac:1223"));
    ev.previous = previous;
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;

    QCOMPARE(ipod.state, IpodState::Dfu);
    QCOMPARE(ipod.mode, UsbMode::Dfu);
    QCOMPARE(ipod.usbId, QStringLiteral("05ac:1223"));
    QCOMPARE(ipod.model, QStringLiteral("classic-6g"));
    QCOMPARE(ipod.modelSource, ModelSource::Usb);
    QVERIFY(ipod.volume);
    QCOMPARE(ipod.firmware.files, FirmwareFiles::Tacetos);
    QCOMPARE(ipod.firmware.version, QStringLiteral("0.3"));
    // No disk in DFU mode.
    QVERIFY(!ipod.disk);
}

void TstDeviceMerge::dfuAfterNormalTooLongAgo()
{
    PreviousIpod previous;
    previous.ipod = mergeEvidence(plugged(QStringLiteral("05ac:1261")), g_models).ipod;

    IpodEvidence ev;
    ev.usb = usbDevice(QStringLiteral("05ac:1223"));

    previous.ageMs = kModeMemoryMs;
    ev.previous = previous;
    QCOMPARE(mergeEvidence(ev, g_models).ipod.model, QStringLiteral("classic-6g"));

    previous.ageMs = kModeMemoryMs + 1;
    ev.previous = previous;
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Dfu);
    QVERIFY(ipod.model.isEmpty());
    QVERIFY(!ipod.volume);
}

void TstDeviceMerge::dfuAfterNormalOnAnotherPort()
{
    PreviousIpod previous;
    previous.ipod = mergeEvidence(plugged(QStringLiteral("05ac:1261")), g_models).ipod;
    previous.ageMs = 1000;

    IpodEvidence ev;
    ev.usb = usbDevice(QStringLiteral("05ac:1223"), QStringLiteral("1-4"));
    ev.previous = previous;
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Dfu);
    QVERIFY(ipod.model.isEmpty());
}

void TstDeviceMerge::dfuAfterDfuIsNotNormal()
{
    PreviousIpod previous;
    IpodEvidence first;
    first.usb = usbDevice(QStringLiteral("05ac:1223"));
    previous.ipod = mergeEvidence(first, g_models).ipod;
    previous.ageMs = 1000;
    previous.ipod.model = QStringLiteral("classic-6g");

    IpodEvidence ev;
    ev.usb = usbDevice(QStringLiteral("05ac:1223"));
    ev.previous = previous;
    QVERIFY(mergeEvidence(ev, g_models).ipod.model.isEmpty());
}

void TstDeviceMerge::dfuAlone()
{
    IpodEvidence ev;
    ev.usb = usbDevice(QStringLiteral("05ac:1223"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Dfu);
    QCOMPARE(ipod.candidates, QStringList{QStringLiteral("classic-6g")});
    QVERIFY(ipod.model.isEmpty());
    QCOMPARE(ipod.modelSource, ModelSource::None);
    QVERIFY(!ipod.disk);
    QVERIFY(!ipod.volume);
}

void TstDeviceMerge::pickedFolderWithInfoFile()
{
    IpodEvidence ev;
    ev.volume = someVolume();
    ev.markers = infoOf(QStringLiteral("ipod-video"));
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QVERIFY(ipod.usbId.isEmpty());
    QCOMPARE(ipod.mode, UsbMode::None);
    QCOMPARE(ipod.state, IpodState::Ready);
    QCOMPARE(ipod.model, QStringLiteral("ipod-video"));
    QCOMPARE(ipod.modelSource, ModelSource::InfoFile);
}

void TstDeviceMerge::pickedFolderWithoutInfoFile()
{
    IpodEvidence ev;
    ev.volume = someVolume();
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::Ambiguous);
    QVERIFY(ipod.model.isEmpty());
}

void TstDeviceMerge::pickedFolderWithUserModel()
{
    IpodEvidence ev;
    ev.volume = someVolume();
    ev.volume->filesystem.clear();
    ev.userModel = QStringLiteral("nano-1g");
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    // A folder gives no filesystem name: that is not "unsupported".
    QCOMPARE(ipod.state, IpodState::Ready);
    QCOMPARE(ipod.model, QStringLiteral("nano-1g"));
    QCOMPARE(ipod.modelSource, ModelSource::User);
}

void TstDeviceMerge::volumeGoneIsNoVolume()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.markers = infoOf(QStringLiteral("classic-6g"), FirmwareFiles::Tacetos);
    ev.markers.volumeGone = true;
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::NoVolume);
    QVERIFY(!ipod.volume);
    QCOMPARE(ipod.firmware.files, FirmwareFiles::Absent);
}

void TstDeviceMerge::unsupportedFilesystemReadsNothing()
{
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.volume = someVolume(QStringLiteral("exfat"));
    ev.markers = infoOf(QStringLiteral("ipod-video"), FirmwareFiles::Present);
    const DetectedIpod ipod = mergeEvidence(ev, g_models).ipod;
    QCOMPARE(ipod.state, IpodState::UnsupportedFilesystem);
    QCOMPARE(ipod.firmware.files, FirmwareFiles::Absent);
    QVERIFY(!ipod.firmware.otherModel);
    // The volume is kept in the result.
    QVERIFY(ipod.volume);
}

void TstDeviceMerge::waitingEndsWithTheVolume()
{
    // The volume is there before the wait is over: ready, not waiting.
    IpodEvidence ev = plugged(QStringLiteral("05ac:1261"));
    ev.waitOver = false;
    QCOMPARE(mergeEvidence(ev, g_models).ipod.state, IpodState::Ready);

    // No disk yet, wait over: no-disk. A disk that comes later replaces it.
    ev.disk.reset();
    ev.volume.reset();
    QCOMPARE(mergeEvidence(ev, g_models).ipod.state, IpodState::Waiting);
    ev.waitOver = true;
    QCOMPARE(mergeEvidence(ev, g_models).ipod.state, IpodState::NoDisk);
    ev.disk = someDisk();
    QCOMPARE(mergeEvidence(ev, g_models).ipod.state, IpodState::NoVolume);
    ev.volume = someVolume();
    QCOMPARE(mergeEvidence(ev, g_models).ipod.state, IpodState::Ready);
}

void TstDeviceMerge::twoIpodsAtOnce()
{
    // Same model, two ports: each keeps its own disk, volume and state.
    IpodEvidence a = plugged(QStringLiteral("05ac:1261"));
    a.usb->portPath = QStringLiteral("1-2");
    a.disk->node = QStringLiteral("/dev/sdb");
    IpodEvidence b = plugged(QStringLiteral("05ac:1261"));
    b.usb->portPath = QStringLiteral("1-3");
    b.disk->node = QStringLiteral("/dev/sdc");
    b.volume.reset();

    const DetectedIpod first = mergeEvidence(a, g_models).ipod;
    const DetectedIpod second = mergeEvidence(b, g_models).ipod;
    QCOMPARE(first.state, IpodState::Ready);
    QCOMPARE(second.state, IpodState::NoVolume);
    QCOMPARE(first.disk->node, QStringLiteral("/dev/sdb"));
    QCOMPARE(second.disk->node, QStringLiteral("/dev/sdc"));
    QCOMPARE(first.model, second.model);
    QVERIFY(first.portPath != second.portPath);
}

void TstDeviceMerge::everyStateHasAMessage()
{
    const QList<IpodState> states = {
        IpodState::Conflict,   IpodState::UnsupportedModel,
        IpodState::Permission, IpodState::Ambiguous,
        IpodState::Dfu,        IpodState::Waiting,
        IpodState::NoDisk,     IpodState::MacFormat,
        IpodState::NoVolume,   IpodState::UnsupportedFilesystem,
        IpodState::Ready,
    };
    QStringList seen;
    for (IpodState state : states) {
        DetectedIpod ipod;
        ipod.state = state;
        ipod.model = QStringLiteral("classic-6g");
        const QString text = stateMessage(ipod, g_models, HostSystem::Linux);
        QVERIFY2(!text.isEmpty(), qPrintable(QString::number(static_cast<int>(state))));
        QVERIFY(!seen.contains(text));
        seen << text;
    }
}

void TstDeviceMerge::readyMessage()
{
    DetectedIpod ipod = mergeEvidence(plugged(QStringLiteral("05ac:1261")), g_models).ipod;
    QCOMPARE(stateMessage(ipod, g_models),
             QStringLiteral("iPod Classic 120 GB (late 2008), Windows format"));

    // Without a disk size the volume size is used, and an unknown name of
    // filesystem adds nothing. The variant is dropped to test the plain form.
    ipod.variant = VariantMatch();
    ipod.disk.reset();
    ipod.volume->filesystem = QStringLiteral("hfsplus");
    QCOMPARE(stateMessage(ipod, g_models),
             QStringLiteral("iPod Classic (6th generation), 118 GB, Mac format"));
    ipod.volume->filesystem.clear();
    QCOMPARE(stateMessage(ipod, g_models), QStringLiteral("iPod Classic (6th generation), 118 GB"));
}

void TstDeviceMerge::permissionMessagePerSystem()
{
    DetectedIpod ipod;
    ipod.state = IpodState::Permission;
    const QString linux = stateMessage(ipod, g_models, HostSystem::Linux);
    const QString windows = stateMessage(ipod, g_models, HostSystem::Windows);
    const QString mac = stateMessage(ipod, g_models, HostSystem::MacOs);
    for (const QString &text : {linux, windows, mac})
        QVERIFY(text.startsWith(QStringLiteral("TacetOS may not read the disk of the iPod.")));
    QVERIFY(linux != windows);
    QVERIFY(windows != mac);
    QVERIFY(linux != mac);
}

QTEST_GUILESS_MAIN(TstDeviceMerge)
#include "tst_device_merge.moc"
