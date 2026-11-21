// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md sections 7 and 15: the disks that are not an iPod, and the
// disk that is not the one that was shown, for every command that writes; then the
// writes themselves on a "real" disk when the gate is open (section 16). The
// helper loop runs in this process on recorded answers of udisks2 and a copy of
// the device tree; each "disk" is an image file. Each case checks the code, that
// there is a message, that no descriptor was opened for writing, and that the
// image is the same byte for byte (its hash).

#include "bootloaderfile.h"
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
using tacetos::fakepod::BootloaderFile;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodResult;
using tacetos::fakepod::FakePodSpec;
using tacetos::fakepod::TableKind;

namespace {

const QString kBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kSerial = QStringLiteral("000A27001A2B3C4D");

QByteArray fileHash(const QString &path)
{
    return tacetos::fakepod::imageHash(path);
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
    // Every call that opens or changes the disk, in order: "r", "rw", "unmount".
    QStringList calls;
    // Off: an open for writing is refused, as the user who closes the password
    // window. On: the image is given for writing, as udisks2 gives a real disk.
    bool allowWrite = false;
    // The answer to the unmount: empty when it works.
    QString unmountCode;
    QString version() override { return QStringLiteral("2.9.4"); }
    std::optional<UdisksObjects> objects(QString *) override { return data; }
    OpenResult open(const QString &, bool write) override
    {
        OpenResult r;
        calls.append(write ? QStringLiteral("rw") : QStringLiteral("r"));
        if (write) {
            ++writeOpens;
            if (!allowWrite) {
                r.code = QStringLiteral("permission-denied");
                r.detail = QStringLiteral("the user closed the password window");
                return r;
            }
            r.fd = ::open(imagePath.toLocal8Bit().constData(), O_RDWR);
        } else {
            ++readOpens;
            r.fd = ::open(imagePath.toLocal8Bit().constData(), O_RDONLY);
        }
        if (r.fd < 0)
            r.code = QStringLiteral("open-failed");
        return r;
    }
    ServiceResult unmountAll(const QString &) override
    {
        calls.append(QStringLiteral("unmount"));
        ServiceResult r;
        r.code = unmountCode;
        if (!r.code.isEmpty())
            r.detail = QStringLiteral("a program is using the volume");
        return r;
    }
    ServiceResult powerOff(const QString &) override
    {
        calls.append(QStringLiteral("power-off"));
        return {};
    }
    bool listed(const QString &) override { return false; }
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

    // A Windows or Mac iPod whose music partition is large enough for FAT32: made
    // larger as a sparse file, with its size written in the table (as in the
    // command tests of the helper).
    QString makeLargePod(const QString &name, TableKind table)
    {
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = path(name);
        spec.table = table;
        const FakePodResult r = FakePod::build(spec);
        if (!r.ok || r.sectorSize != 512)
            return {};
        constexpr qint64 kMusicTotal = 600 * 1024 * 2;
        QFile f(spec.path);
        if (!f.open(QIODevice::ReadWrite) ||
            !tacetos::fakepod::resizeSparse(f, (r.musicStart / 512 + kMusicTotal) * 512))
            return {};
        QByteArray count(4, '\0');
        for (int k = 0; k < 4; ++k) {
            const int shift = table == TableKind::Apm ? 24 - 8 * k : 8 * k;
            count[k] = char((quint32(kMusicTotal) >> shift) & 0xFF);
        }
        f.seek(table == TableKind::Apm ? 1536 + 12 : 0x1BE + 16 + 12);
        return f.write(count) == 4 ? spec.path : QString();
    }

    // A bootloader file for the target that read-state named.
    QString makeBootloader(const QString &target)
    {
        const QString file = path(QStringLiteral("boot%1.bin").arg(++m_count));
        return put(file, BootloaderFile::build(target)) ? file : QString();
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
    void writesWithTheGateOpen_data();
    void writesWithTheGateOpen();
    void restoreWithTheGateOpen();
    void unmountRefusedWritesNothing();
    void writeOpenRefusedWritesNothing();
    void gateFollowsTheBuild();
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
    // A restore takes its backup from the data folder only (section 6.6).
    const QString inData = data.filePath(QStringLiteral("extra.bin"));
    QVERIFY(put(inData, QByteArray(64, 'x')));
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
        const Answer a = runRequest(w->system, locks.path(), command,
                                    argsFor(command, targetFor(r, image), token, data.path(),
                                            command == cmd::restoreBackup ? inData : extra));
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
    // A restore takes its backup from the data folder only (section 6.6).
    const QString inData = data.filePath(QStringLiteral("extra.bin"));
    QVERIFY(put(inData, QByteArray(64, 'x')));
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
        const Answer a = runRequest(w->system, locks.path(), command,
                                    argsFor(command, targetFor(r, image), token, data.path(),
                                            command == cmd::restoreBackup ? inData : extra));
        QVERIFY2(!a.result.isEmpty(), qPrintable(command));
        QCOMPARE(a.code(), QString(err::devBuild));
        // A development build leaves the music partition mounted.
        QVERIFY2(!w->service->calls.contains(QStringLiteral("unmount")), qPrintable(command));
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

// docs/specs/helper.md section 16: the writes on a real disk, in a build whose gate is
// open. The loop runs in this process with `SystemLayer::realWrites` set by the test,
// which is the only way to open it besides the build option; the "real disk" is an
// image file given by the fake disk service for reading, then for writing. Each case
// checks the order of the calls (unmount for a format or a conversion, read only,
// then read and write after every check), the result, and the iPod read again.
void TestHelperRefusals::writesWithTheGateOpen_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<bool>("installed");
    QTest::addColumn<bool>("mac");
    QTest::addColumn<QStringList>("calls");
    QTest::addColumn<QString>("stateAfter");
    QTest::addColumn<QString>("tableAfter");
    const QStringList readThenWrite{"r", "rw"};
    QTest::newRow("install") << QString(cmd::installBootloader) << false << false << readThenWrite
                             << "installed" << "dos";
    QTest::newRow("update") << QString(cmd::updateBootloader) << true << false << readThenWrite
                            << "installed" << "dos";
    QTest::newRow("uninstall") << QString(cmd::uninstallBootloader) << true << false
                               << readThenWrite << "clean" << "dos";
#if defined(Q_OS_LINUX)
    // Large sparse files, which only Linux makes cheaply.
    const QStringList unmountFirst{"unmount", "r", "rw"};
    QTest::newRow("format") << QString(cmd::formatMusic) << false << false << unmountFirst
                            << QString() << "dos";
    QTest::newRow("convert") << QString(cmd::convertToFat32) << false << true << unmountFirst
                             << QString() << "dos";
#endif
}

void TestHelperRefusals::writesWithTheGateOpen()
{
    TACETOS_NEEDS_SYMLINKS();
    QFETCH(QString, command);
    QFETCH(bool, installed);
    QFETCH(bool, mac);
    QFETCH(QStringList, calls);
    QFETCH(QString, stateAfter);
    QFETCH(QString, tableAfter);
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const bool large = command == cmd::formatMusic || command == cmd::convertToFat32;
    const QString name = QStringLiteral("open-%1.img").arg(command);
    const QString image = large ? makeLargePod(name, mac ? TableKind::Apm : TableKind::Dos)
                                : makePod(name, installed);
    QVERIFY(!image.isEmpty());

    auto scanWorld = world(r, image, tree.path());
    const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                   QJsonObject{{"target", targetFor(r, image)}});
    QVERIFY2(scan.ok(), qPrintable(scan.detail()));
    const QString token = scan.data().value(QStringLiteral("scanToken")).toString();
    const QString boot = makeBootloader(scan.data().value(QStringLiteral("target")).toString());
    QVERIFY(!boot.isEmpty());
    const QByteArray before = fileHash(image);

    auto w = world(r, image, tree.path());
    w->system.realWrites = true;
    w->service->allowWrite = true;
    QJsonObject args = argsFor(command, targetFor(r, image), token, data.path(), boot);
    if (mac) {
        args.insert(QStringLiteral("ipodName"), QStringLiteral("Mac iPod"));
        args.insert(QStringLiteral("typedName"), QStringLiteral("Mac iPod"));
    }
    const Answer a = runRequest(w->system, locks.path(), command, args);
    QVERIFY2(a.ok(), qPrintable(a.code() + QLatin1Char(' ') + a.detail()));
    QCOMPARE(w->service->calls, calls);
    QVERIFY(fileHash(image) != before);
    // The first stage is the unmount for a format or a conversion, the open otherwise.
    QString firstStage;
    for (const QJsonObject &e : a.events) {
        if (e.value(QStringLiteral("event")).toString() == QLatin1String("progress")) {
            firstStage = e.value(QStringLiteral("stage")).toString();
            break;
        }
    }
    QCOMPARE(firstStage, calls.first() == QLatin1String("unmount") ? QStringLiteral("unmount")
                                                                   : QStringLiteral("open"));

    auto again = world(r, image, tree.path());
    const Answer after = runRequest(again->system, locks.path(), QString(cmd::readState),
                                    QJsonObject{{"target", targetFor(r, image)}});
    QVERIFY2(after.ok(), qPrintable(after.detail()));
    QCOMPARE(after.data().value(QStringLiteral("table")).toString(), tableAfter);
    if (!stateAfter.isEmpty())
        QCOMPARE(after.data().value(QStringLiteral("state")).toString(), stateAfter);
}

// A backup of the firmware partition made on the real disk is written back to it.
void TestHelperRefusals::restoreWithTheGateOpen()
{
    TACETOS_NEEDS_SYMLINKS();
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString image = makePod(QStringLiteral("open-restore.img"));
    QVERIFY(!image.isEmpty());
    const QByteArray original = fileHash(image);
    auto read = [&] {
        auto w = world(r, image, tree.path());
        return runRequest(w->system, locks.path(), QString(cmd::readState),
                          QJsonObject{{"target", targetFor(r, image)}});
    };

    Answer state = read();
    QVERIFY(state.ok());
    QString token = state.data().value(QStringLiteral("scanToken")).toString();
    auto w = world(r, image, tree.path());
    const Answer backup = runRequest(
        w->system, locks.path(), QString(cmd::backupFirmware),
        argsFor(QString(cmd::backupFirmware), targetFor(r, image), token, data.path(), QString()));
    QVERIFY2(backup.ok(), qPrintable(backup.detail()));
    const QString saved = backup.data().value(QStringLiteral("path")).toString();

    const QString boot = makeBootloader(state.data().value(QStringLiteral("target")).toString());
    w = world(r, image, tree.path());
    w->system.realWrites = true;
    w->service->allowWrite = true;
    const Answer install = runRequest(
        w->system, locks.path(), QString(cmd::installBootloader),
        argsFor(QString(cmd::installBootloader), targetFor(r, image), token, data.path(), boot));
    QVERIFY2(install.ok(), qPrintable(install.detail()));
    QVERIFY(fileHash(image) != original);

    state = read();
    token = state.data().value(QStringLiteral("scanToken")).toString();
    w = world(r, image, tree.path());
    w->system.realWrites = true;
    w->service->allowWrite = true;
    const Answer restore = runRequest(
        w->system, locks.path(), QString(cmd::restoreBackup),
        argsFor(QString(cmd::restoreBackup), targetFor(r, image), token, data.path(), saved));
    QVERIFY2(restore.ok(), qPrintable(restore.code() + QLatin1Char(' ') + restore.detail()));
    QCOMPARE(w->service->calls, (QStringList{"r", "rw"}));
    QCOMPARE(read().data().value(QStringLiteral("state")).toString(), QStringLiteral("clean"));
}

// The music volume is used by a program: the format stops at the unmount, before
// the disk is even opened, and nothing is written.
void TestHelperRefusals::unmountRefusedWritesNothing()
{
    TACETOS_NEEDS_SYMLINKS();
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString image = makePod(QStringLiteral("open-busy.img"));
    QVERIFY(!image.isEmpty());
    auto scanWorld = world(r, image, tree.path());
    const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                   QJsonObject{{"target", targetFor(r, image)}});
    QVERIFY(scan.ok());
    const QByteArray before = fileHash(image);
    auto w = world(r, image, tree.path());
    w->system.realWrites = true;
    w->service->allowWrite = true;
    w->service->unmountCode = QString(err::inUse);
    const Answer a = runRequest(w->system, locks.path(), QString(cmd::formatMusic),
                                argsFor(QString(cmd::formatMusic), targetFor(r, image),
                                        scan.data().value(QStringLiteral("scanToken")).toString(),
                                        data.path(), QString()));
    checkRefused(a, QString(err::inUse), image, before, w->service);
    QCOMPARE(w->service->calls, QStringList{QStringLiteral("unmount")});
}

// The user closes the password window when the disk is opened for writing: the
// answer says so and the iPod is as it was.
void TestHelperRefusals::writeOpenRefusedWritesNothing()
{
    TACETOS_NEEDS_SYMLINKS();
    const Recorded r;
    QTemporaryDir tree;
    QVERIFY(makeTree(tree.path(), r));
    QTemporaryDir locks;
    QTemporaryDir data;
    const QString image = makePod(QStringLiteral("open-denied.img"));
    QVERIFY(!image.isEmpty());
    auto scanWorld = world(r, image, tree.path());
    const Answer scan = runRequest(scanWorld->system, locks.path(), QString(cmd::readState),
                                   QJsonObject{{"target", targetFor(r, image)}});
    QVERIFY(scan.ok());
    const QString boot = makeBootloader(scan.data().value(QStringLiteral("target")).toString());
    const QByteArray before = fileHash(image);
    auto w = world(r, image, tree.path());
    w->system.realWrites = true;
    const Answer a = runRequest(w->system, locks.path(), QString(cmd::installBootloader),
                                argsFor(QString(cmd::installBootloader), targetFor(r, image),
                                        scan.data().value(QStringLiteral("scanToken")).toString(),
                                        data.path(), boot));
    QVERIFY(!a.ok());
    QCOMPARE(a.code(), QString(err::permissionDenied));
    QVERIFY(!a.detail().isEmpty());
    QCOMPARE(w->service->calls, (QStringList{"r", "rw"}));
    QCOMPARE(fileHash(image), before);
}

// Section 9: outside these tests, the gate is the build option and nothing else.
void TestHelperRefusals::gateFollowsTheBuild()
{
    QCOMPARE(SystemLayer().realWrites, tacetos::installer::realWritesAllowed());
    QCOMPARE(makeRealSystem()->realWrites, tacetos::installer::realWritesAllowed());
}

QTEST_GUILESS_MAIN(TestHelperRefusals)
#include "tst_helper_refusals.moc"
