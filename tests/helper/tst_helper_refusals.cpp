// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md sections 7 and 15: the disks that are not an iPod, and the
// disk that is not the one that was shown, for every command that writes. The
// helper loop runs in this process on recorded answers of udisks2 and a copy of
// the device tree; each "disk" is an image file. Each case checks the code, that
// there is a message, that no descriptor was opened for writing, and that the
// image is the same byte for byte (its hash).

#include "checks.h"
#include "fakepod.h"
#include "helperconstants.h"
#include "linuxfacts.h"
#include "protocol.h"
#include "server.h"
#include "symlinks.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <condition_variable>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <unistd.h>

using namespace tacetos::helper;
using tacetos::device::UdisksObjects;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodResult;
using tacetos::fakepod::FakePodSpec;

namespace {

const QString kBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kSerial = QStringLiteral("000A27001A2B3C4D");

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd())
        h.addData(f.read(1 << 20));
    return h.result();
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// A USB stick with one FAT32 partition and nothing of an iPod on it.
bool makeStick(const QString &path)
{
    QByteArray image(8 * 1024 * 1024, '\0');
    // The partition table: one entry of type 0x0C from sector 2048, and the
    // signature.
    const int entry = 0x1BE;
    image[entry + 4] = char(0x0C);
    image[entry + 8] = char(0x00);
    image[entry + 9] = char(0x08);
    const quint32 count = quint32(image.size() / 512 - 2048);
    for (int k = 0; k < 4; ++k)
        image[entry + 12 + k] = char((count >> (8 * k)) & 0xFF);
    image[510] = char(0x55);
    image[511] = char(0xAA);
    // The boot sector of the volume.
    const int boot = 2048 * 512;
    image.replace(boot + 82, 8, "FAT32   ");
    image[boot + 510] = char(0x55);
    image[boot + 511] = char(0xAA);
    return put(path, image);
}

// What a recorded system says about one disk: the objects of udisks2 and the
// part of /sys above the disk.
struct Recorded
{
    QString name;
    QString node = QStringLiteral("/dev/sdb");
    int minor = 16;
    bool removable = true;
    QString bus = QStringLiteral("usb");
    // The USB device above the disk; an empty vendor means there is none.
    QString vendor = QStringLiteral("05ac");
    QString product = QStringLiteral("1261");
    QString port = QStringLiteral("1-2");
    QString serial = kSerial;
};

UdisksObjects objectsOf(const Recorded &r, quint64 size)
{
    UdisksObjects objects;
    const QString drive = QStringLiteral("/org/freedesktop/UDisks2/drives/D");
    const QString block = QStringLiteral("/org/freedesktop/UDisks2/block_devices/x");
    objects[drive][kDrive] = {{"Removable", r.removable},
                              {"ConnectionBus", r.bus},
                              {"Serial", r.vendor.isEmpty() ? QString() : r.serial}};
    objects[block][kBlock] = {{"Device", QByteArray(r.node.toUtf8() + '\0')},
                              {"Size", size},
                              {"DeviceNumber", quint64((8 << 8) | r.minor)},
                              {"Drive", drive}};
    return objects;
}

// The copy of the device tree. The folders have colons in their names, so the
// tests that use it are skipped where links are not real (see symlinks.h).
bool makeTree(const QString &root, const Recorded &r)
{
    const QString name = r.node.mid(5);
    QString parent;
    if (r.vendor.isEmpty()) {
        parent = root + QStringLiteral("/devices/pci0000:00/0000:00:1f.2/ata1/host0");
    } else {
        parent = root + QStringLiteral("/devices/pci0000:00/usb1/") + r.port;
        if (!QDir().mkpath(parent))
            return false;
        const bool ok = put(parent + QStringLiteral("/idVendor"), (r.vendor + '\n').toUtf8()) &&
                        put(parent + QStringLiteral("/idProduct"), (r.product + '\n').toUtf8()) &&
                        put(parent + QStringLiteral("/busnum"), "1\n") &&
                        put(parent + QStringLiteral("/devnum"), "7\n") &&
                        put(parent + QStringLiteral("/serial"), (r.serial + '\n').toUtf8());
        if (!ok)
            return false;
        parent += QStringLiteral("/") + r.port + QStringLiteral(":1.0/host6");
    }
    const QString disk = parent + QStringLiteral("/target0:0:0/0:0:0:0/block/") + name;
    return QDir().mkpath(disk) && QDir().mkpath(root + QStringLiteral("/dev/block")) &&
           QFile::link(disk, root + QStringLiteral("/dev/block/8:%1").arg(r.minor));
}

// udisks2, as far as the helper uses it: the answers are recorded, and a read
// opens the image file of the "disk". Every open is counted.
class ImageService : public DiskService
{
public:
    UdisksObjects data;
    QString imagePath;
    int readOpens = 0;
    int writeOpens = 0;
    QString version() override { return QStringLiteral("2.9.4"); }
    std::optional<UdisksObjects> objects(QString *) override { return data; }
    OpenResult open(const QString &, bool write) override
    {
        OpenResult r;
        if (write) {
            ++writeOpens;
            r.code = QStringLiteral("permission-denied");
            return r;
        }
        ++readOpens;
        r.fd = ::open(imagePath.toLocal8Bit().constData(), O_RDONLY);
        if (r.fd < 0)
            r.code = QStringLiteral("open-failed");
        return r;
    }
};

// The two ends of the helper, in memory. The input ends when the result is out,
// so that "the end of the input is a cancel" (section 3.7) does not fire.
struct Pipe
{
    std::mutex mutex;
    std::condition_variable wake;
    QByteArray input;
    int position = 0;
    bool done = false;
    QList<QJsonObject> events;
};

struct Answer
{
    QJsonObject result;
    QList<QJsonObject> events;
    QString code() const { return result.value(QStringLiteral("error")).toString(); }
    QString detail() const { return result.value(QStringLiteral("detail")).toString(); }
    bool ok() const { return result.value(QStringLiteral("ok")).toBool(); }
    QJsonObject data() const { return result.value(QStringLiteral("data")).toObject(); }
};

Answer runRequest(SystemLayer &system, const QString &lockFolder, const QString &cmd,
                  const QJsonObject &args)
{
    auto pipe = std::make_shared<Pipe>();
    pipe->input = QJsonDocument(QJsonObject{{"id", 1}, {"cmd", cmd}, {"args", args}})
                      .toJson(QJsonDocument::Compact) +
                  '\n';
    ServerIo io;
    io.readByte = [pipe] {
        std::unique_lock<std::mutex> lock(pipe->mutex);
        if (pipe->position < pipe->input.size())
            return int(uchar(pipe->input[pipe->position++]));
        pipe->wake.wait_for(lock, std::chrono::seconds(30), [&] { return pipe->done; });
        return -1;
    };
    io.write = [pipe](const QByteArray &bytes) {
        std::lock_guard<std::mutex> lock(pipe->mutex);
        const QJsonObject event = QJsonDocument::fromJson(bytes).object();
        pipe->events.append(event);
        if (event.value(QStringLiteral("event")).toString() == QLatin1String("result")) {
            pipe->done = true;
            pipe->wake.notify_all();
        }
        return true;
    };
    Server server(io, lockFolder, &system);
    server.run();
    Answer answer;
    std::lock_guard<std::mutex> lock(pipe->mutex);
    answer.events = pipe->events;
    for (const QJsonObject &e : pipe->events) {
        if (e.value(QStringLiteral("event")).toString() == QLatin1String("result"))
            answer.result = e;
    }
    return answer;
}

// The commands that write, with arguments that are right in every other way, so
// that the refusal under test is the first one.
QJsonObject argsFor(const QString &cmd, const QJsonObject &target, const QString &token,
                    const QString &dataDir, const QString &extraFile)
{
    QJsonObject a;
    a.insert(QStringLiteral("target"), target);
    a.insert(QStringLiteral("scanToken"), token);
    if (cmd == cmd::formatMusic || cmd == cmd::convertToFat32) {
        a.insert(QStringLiteral("ipodName"), QStringLiteral("Test iPod"));
        a.insert(QStringLiteral("typedName"), QStringLiteral("Test iPod"));
        a.insert(QStringLiteral("volumeLabel"), QStringLiteral("IPOD"));
        if (cmd == cmd::convertToFat32)
            a.insert(QStringLiteral("dataDir"), dataDir);
        return a;
    }
    a.insert(QStringLiteral("dataDir"), dataDir);
    if (cmd == cmd::restoreBackup) {
        a.insert(QStringLiteral("backupFile"), extraFile);
    } else if (cmd != cmd::backupFirmware) {
        a.insert(QStringLiteral("expectedState"), cmd == cmd::installBootloader
                                                      ? QStringLiteral("clean")
                                                      : QStringLiteral("installed"));
        if (cmd != cmd::uninstallBootloader) {
            a.insert(QStringLiteral("bootloaderFile"), extraFile);
            a.insert(QStringLiteral("bootloaderVersion"), QStringLiteral("0.1"));
        }
    }
    return a;
}

const QStringList kWriteCommands = {
    QString(cmd::backupFirmware),   QString(cmd::installBootloader),
    QString(cmd::updateBootloader), QString(cmd::uninstallBootloader),
    QString(cmd::restoreBackup),    QString(cmd::formatMusic),
    QString(cmd::convertToFat32)};

} // namespace

class TestHelperRefusals : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_count = 0;

    QString path(const QString &name) { return m_dir.filePath(name); }

    // An image of a Windows iPod, written by the fake iPod library.
    QString makePod(const QString &name, bool installed = false)
    {
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = path(name);
        spec.bootloaderPresent = installed;
        const FakePodResult r = FakePod::build(spec);
        return r.ok ? spec.path : QString();
    }

    QJsonObject targetFor(const Recorded &r, const QString &image) const
    {
        return {{"kind", "disk"},
                {"node", r.node},
                {"sizeBytes", double(QFileInfo(image).size())},
                {"serial", r.vendor.isEmpty() ? QString() : r.serial},
                {"port", r.vendor.isEmpty() ? QString() : r.port}};
    }

    // The system layer for one recorded disk whose "device" is `image`. The
    // folder of the device tree lives as long as `tree`.
    struct World
    {
        SystemLayer system;
        ImageService *service = nullptr;
    };
    std::unique_ptr<World> world(const Recorded &r, const QString &image, const QString &tree)
    {
        auto w = std::make_unique<World>();
        auto service = std::make_unique<ImageService>();
        service->data = objectsOf(r, quint64(QFileInfo(image).size()));
        service->imagePath = image;
        w->service = service.get();
        w->system.disks = std::move(service);
        w->system.linuxSystem = true;
        w->system.sysRoot = tree;
        w->system.mountinfo = path(QStringLiteral("no-mountinfo"));
        w->system.swaps = path(QStringLiteral("no-swaps"));
        return w;
    }

    // Everything every refusal must show.
    void checkRefused(const Answer &a, const QString &code, const QString &image,
                      const QByteArray &hashBefore, ImageService *service)
    {
        QVERIFY2(!a.result.isEmpty(), "no result event");
        QVERIFY(!a.ok());
        QCOMPARE(a.code(), code);
        QVERIFY2(!a.detail().isEmpty(), "a refusal has a message");
        QCOMPARE(service->writeOpens, 0);
        QCOMPARE(fileHash(image), hashBefore);
    }

private slots:
    void notAnIpod_data();
    void notAnIpod();
    void diskChangedBeforeTheWrite_data();
    void diskChangedBeforeTheWrite();
    void imageChangedAfterTheScan_data();
    void imageChangedAfterTheScan();
    void everyWriteCommandStopsAtTheGate();
    void backupOfTheRightIpodReadsOnly();
};

void TestHelperRefusals::notAnIpod_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("code");
    QTest::newRow("usb stick in fat32") << "stick" << "not-an-ipod";
    QTest::newRow("usb stick with the table and banner of an ipod") << "copy" << "not-an-ipod";
    QTest::newRow("internal sata disk with an ipod image") << "sata" << "not-removable-usb";
    QTest::newRow("ipod image behind a card reader") << "reader" << "not-an-ipod";
}

void TestHelperRefusals::notAnIpod()
{
    TACETOS_NEEDS_SYMLINKS();
    QFETCH(QString, kind);
    QFETCH(QString, code);
    Recorded r;
    QString image;
    if (kind == QLatin1String("stick")) {
        r.vendor = QStringLiteral("0781");
        r.product = QStringLiteral("5567");
        r.serial = QStringLiteral("4C530001");
        image = path(QStringLiteral("stick.img"));
        QVERIFY(makeStick(image));
    } else {
        image = makePod(QStringLiteral("pod-%1.img").arg(kind));
        QVERIFY(!image.isEmpty());
        if (kind == QLatin1String("copy")) {
            r.vendor = QStringLiteral("0781");
            r.product = QStringLiteral("5567");
        } else if (kind == QLatin1String("sata")) {
            r.node = QStringLiteral("/dev/sda");
            r.minor = 0;
            r.removable = false;
            r.bus = QStringLiteral("ata");
            r.vendor.clear();
        } else {
            // A card reader of a computer, on USB, with its own ids.
            r.vendor = QStringLiteral("0bda");
            r.product = QStringLiteral("0316");
        }
    }
    QTemporaryDir tree;
    QVERIFY(tree.isValid());
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString extra = path(QStringLiteral("extra.bin"));
    QVERIFY(put(extra, QByteArray(64, 'x')));
    const QByteArray before = fileHash(image);
    for (const QString &command : kWriteCommands) {
        const auto w = world(r, image, tree.path());
        const Answer a = runRequest(
            w->system, locks.path(), command,
            argsFor(command, targetFor(r, image), QStringLiteral("stale"), data.path(), extra));
        QVERIFY2(!a.result.isEmpty(), qPrintable(command));
        QCOMPARE(a.code(), code);
        QVERIFY2(!a.detail().isEmpty(), qPrintable(command));
        // Refused before anything is opened, even for reading.
        QCOMPARE(w->service->readOpens, 0);
        QCOMPARE(w->service->writeOpens, 0);
        QCOMPARE(fileHash(image), before);
        // No `accepted` event: the refusal comes before it (section 7.1).
        for (const QJsonObject &e : a.events)
            QVERIFY(e.value(QStringLiteral("event")).toString() != QLatin1String("accepted"));
    }
    // Nothing was made in the data folder either.
    QVERIFY(QDir(data.path()).isEmpty());
}

void TestHelperRefusals::diskChangedBeforeTheWrite_data()
{
    QTest::addColumn<QString>("what");
    QTest::newRow("another serial") << "serial";
    QTest::newRow("another port") << "port";
    QTest::newRow("another size") << "size";
    QTest::newRow("gone") << "gone";
}

// The scan saw one disk, the system now shows another on the same name (check 1
// of bootloader-legacy.md section 11, in the part the system answers).
void TestHelperRefusals::diskChangedBeforeTheWrite()
{
    TACETOS_NEEDS_SYMLINKS();
    QFETCH(QString, what);
    const QString image = makePod(QStringLiteral("changed.img"));
    QVERIFY(!image.isEmpty());
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString extra = path(QStringLiteral("extra.bin"));
    QVERIFY(put(extra, QByteArray(64, 'x')));
    QJsonObject target = targetFor(r, image);
    QString code = QStringLiteral("disk-changed");
    if (what == QLatin1String("serial"))
        target.insert(QStringLiteral("serial"), QStringLiteral("OTHER0001"));
    else if (what == QLatin1String("port"))
        target.insert(QStringLiteral("port"), QStringLiteral("1-4"));
    else if (what == QLatin1String("size"))
        target.insert(QStringLiteral("sizeBytes"), double(QFileInfo(image).size() + 512));
    else if (what == QLatin1String("gone")) {
        target.insert(QStringLiteral("node"), QStringLiteral("/dev/sdc"));
        code = QStringLiteral("disk-gone");
    }
    const QByteArray before = fileHash(image);
    for (const QString &command : kWriteCommands) {
        const auto w = world(r, image, tree.path());
        const Answer a =
            runRequest(w->system, locks.path(), command,
                       argsFor(command, target, QStringLiteral("stale"), data.path(), extra));
        checkRefused(a, code, image, before, w->service);
        QCOMPARE(w->service->readOpens, 0);
    }
}

void TestHelperRefusals::imageChangedAfterTheScan_data()
{
    QTest::addColumn<qint64>("offset");
    QTest::newRow("sector 0") << qint64(100);
    // The first sector of the firmware partition (the fake library puts it at
    // sector 64).
    QTest::newRow("first sector of the firmware partition") << qint64(64 * 512 + 300);
}

// The system shows the same disk, but what it holds is not what the scan read:
// another iPod was plugged in on the same port (check 1, in the part the sectors
// answer). It is caught before any write for each command.
void TestHelperRefusals::imageChangedAfterTheScan()
{
    TACETOS_NEEDS_SYMLINKS();
    QFETCH(qint64, offset);
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString extra = path(QStringLiteral("extra.bin"));
    QVERIFY(put(extra, QByteArray(64, 'x')));
    for (const QString &command : kWriteCommands) {
        const QString image = makePod(QStringLiteral("scan%1.img").arg(++m_count));
        QVERIFY(!image.isEmpty());
        // The scan, as the app asks for it.
        auto scanWorld = world(r, image, tree.path());
        const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                       QJsonObject{{"target", targetFor(r, image)}});
        QVERIFY2(scan.ok(), qPrintable(scan.detail()));
        const QString token = scan.data().value(QStringLiteral("scanToken")).toString();
        QVERIFY(!token.isEmpty());
        // The disk changes.
        {
            QFile f(image);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.seek(offset));
            QVERIFY(f.write("\x5A\xA5", 2) == 2);
        }
        const QByteArray before = fileHash(image);
        const auto w = world(r, image, tree.path());
        const Answer a =
            runRequest(w->system, locks.path(), command,
                       argsFor(command, targetFor(r, image), token, data.path(), extra));
        checkRefused(a, QStringLiteral("disk-changed"), image, before, w->service);
        // The disk was opened for reading, to be compared, and for nothing else.
        QCOMPARE(w->service->readOpens, 1);
    }
}

// A development build is told to write to a real disk (the helper sees the image as
// one, as it sees every disk of the computer): every command that writes stops at
// the gate with `dev-build`, the message says so, nothing is opened for writing and
// the image is the same byte for byte. Update and uninstall get an iPod that has
// the bootloader, so that the state shown to the user is the state on the disk and
// the gate is the first refusal. (Skipped in the build of the beta, which has no
// gate.)
void TestHelperRefusals::everyWriteCommandStopsAtTheGate()
{
    TACETOS_NEEDS_SYMLINKS();
    if (tacetos::installer::realWritesAllowed())
        QSKIP("this build writes to real disks");
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString extra = path(QStringLiteral("extra.bin"));
    QVERIFY(put(extra, QByteArray(64, 'x')));
    int gated = 0;
    for (const QString &command : kWriteCommands) {
        if (command == cmd::backupFirmware)
            continue;
        const bool installed =
            command == cmd::updateBootloader || command == cmd::uninstallBootloader;
        const QString image = makePod(QStringLiteral("gate-%1.img").arg(command), installed);
        QVERIFY2(!image.isEmpty(), qPrintable(command));
        auto scanWorld = world(r, image, tree.path());
        const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                       QJsonObject{{"target", targetFor(r, image)}});
        QVERIFY2(scan.ok(), qPrintable(scan.detail()));
        const QString token = scan.data().value(QStringLiteral("scanToken")).toString();
        const QByteArray before = fileHash(image);
        const auto w = world(r, image, tree.path());
        const Answer a =
            runRequest(w->system, locks.path(), command,
                       argsFor(command, targetFor(r, image), token, data.path(), extra));
        QVERIFY2(!a.result.isEmpty(), qPrintable(command));
        QCOMPARE(a.code(), QString(err::devBuild));
        QVERIFY2(a.detail().contains(QStringLiteral("does not write to real iPods")),
                 qPrintable(command + ": " + a.detail()));
        QCOMPARE(w->service->writeOpens, 0);
        QCOMPARE(fileHash(image), before);
        ++gated;
    }
    // The six commands that write to the iPod; the backup writes in the data folder only.
    QCOMPARE(gated, 6);
}

// The backup writes in the data folder only: on the right iPod it is made, with
// the disk opened for reading and the image unchanged.
void TestHelperRefusals::backupOfTheRightIpodReadsOnly()
{
    TACETOS_NEEDS_SYMLINKS();
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString image = makePod(QStringLiteral("backup.img"));
    QVERIFY(!image.isEmpty());
    auto scanWorld = world(r, image, tree.path());
    const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                   QJsonObject{{"target", targetFor(r, image)}});
    QVERIFY2(scan.ok(), qPrintable(scan.detail()));
    const QString token = scan.data().value(QStringLiteral("scanToken")).toString();
    const QByteArray before = fileHash(image);
    const auto w = world(r, image, tree.path());
    const Answer a = runRequest(
        w->system, locks.path(), QString(cmd::backupFirmware),
        argsFor(QString(cmd::backupFirmware), targetFor(r, image), token, data.path(), QString()));
    QVERIFY2(a.ok(), qPrintable(a.code() + a.detail()));
    QVERIFY(QFileInfo::exists(a.data().value(QStringLiteral("path")).toString()));
    QCOMPARE(w->service->writeOpens, 0);
    QCOMPARE(fileHash(image), before);
}

QTEST_GUILESS_MAIN(TestHelperRefusals)
#include "tst_helper_refusals.moc"
