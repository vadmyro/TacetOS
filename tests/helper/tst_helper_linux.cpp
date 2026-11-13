// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The Linux layer of the helper (docs/specs/helper.md sections 7.2, 11 and 15.3),
// on recorded answers of udisks2 and a copy of the device tree. Nothing here
// reaches udisks2, pkexec or a disk.

#include "diskcmds.h"
#include "linuxfacts.h"
#include "scsisheet.h"
#include "symlinks.h"
#include "udevrule.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::helper;
using tacetos::device::UdisksObjects;

namespace {

const QString kBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");

// A recorded answer of `GetManagedObjects` for one disk with one drive.
struct Disk
{
    QString node = QStringLiteral("/dev/sdb");
    quint64 size = 160041885696ULL;
    bool removable = true;
    QString bus = QStringLiteral("usb");
    QString serial = QStringLiteral("000A27001A2B3C4D");
    bool partition = false;
    bool loop = false;
};

UdisksObjects objectsOf(const Disk &d)
{
    UdisksObjects objects;
    const QString drive = QStringLiteral("/org/freedesktop/UDisks2/drives/D");
    const QString block = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
    objects[drive][kDrive] = {
        {"Removable", d.removable}, {"ConnectionBus", d.bus}, {"Serial", d.serial}};
    objects[block][kBlock] = {{"Device", QByteArray(d.node.toUtf8() + '\0')},
                              {"Size", quint64(d.size)},
                              {"DeviceNumber", quint64((8 << 8) | 16)},
                              {"Drive", drive}};
    if (d.partition)
        objects[block][QStringLiteral("org.freedesktop.UDisks2.Partition")] = {};
    if (d.loop)
        objects[block][QStringLiteral("org.freedesktop.UDisks2.Loop")] = {};
    return objects;
}

UsbFacts usbOf(const QString &vendor, const QString &product)
{
    UsbFacts u;
    u.found = true;
    u.vendor = vendor;
    u.product = product;
    u.busNumber = 1;
    u.deviceNumber = 7;
    u.port = QStringLiteral("1-2");
    u.serial = QStringLiteral("000A27001A2B3C4D");
    u.partitionNames = {QStringLiteral("sdb1"), QStringLiteral("sdb2")};
    u.deviceNumbers = {(8 << 8) | 17, (8 << 8) | 18};
    return u;
}

DiskRequest requestOf(const Disk &d)
{
    return {d.node, qint64(d.size), QStringLiteral("000A27001A2B3C4D"), QStringLiteral("1-2")};
}

DiskInputs inputsOf(const Disk &d, const UsbFacts &usb)
{
    DiskInputs in;
    in.block = blockFactsFromObjects(objectsOf(d), d.node);
    in.usb = usb;
    in.swaps =
        QStringLiteral("Filename\tType\tSize\tUsed\tPriority\n/dev/sda3\tpartition\t100\t0\t-2\n");
    return in;
}

class FakeRunner : public ProcessRunner
{
public:
    int exitCode = 0;
    int calls = 0;
    QString program;
    QStringList arguments;
    // What a successful pkexec does: puts the rule in place.
    QString copyTo;
    int run(const QString &p, const QStringList &a, QString *) override
    {
        ++calls;
        program = p;
        arguments = a;
        if (exitCode == 0 && !copyTo.isEmpty())
            QFile::copy(a.last(), copyTo);
        return exitCode;
    }
};

} // namespace

class TestHelperLinux : public QObject
{
    Q_OBJECT
private slots:
    void ipodIsAccepted();
    void otherDisksAreRefused_data();
    void otherDisksAreRefused();
    void requestMustMatch();
    void systemMustNotBeOnTheDisk();
    void usbFactsFromTree();
    void realCheckWithFakeService();
    void versionAndIds();
    void sheetIsAssembled();
    void sheetRefusals();
    void udevRuleAlreadyThere();
    void udevRuleExitCodes();
    void udevRuleCommandLine();
};

void TestHelperLinux::ipodIsAccepted()
{
    const Disk d;
    QVERIFY(judgeDisk(requestOf(d), inputsOf(d, usbOf("05ac", "1261"))).ok);
}

void TestHelperLinux::otherDisksAreRefused_data()
{
    QTest::addColumn<bool>("removable");
    QTest::addColumn<QString>("bus");
    QTest::addColumn<QString>("vendor");
    QTest::addColumn<QString>("code");
    QTest::newRow("usb stick") << true << "usb" << "0781" << "not-an-ipod";
    QTest::newRow("usb hard disk reporting itself fixed")
        << false << "usb" << "05ac" << "not-removable-usb";
    QTest::newRow("internal disk") << false << "ata" << "" << "not-removable-usb";
    QTest::newRow("card in a built-in reader") << true << "sdio" << "" << "not-removable-usb";
    QTest::newRow("card reader on usb") << true << "usb" << "0bda" << "not-an-ipod";
}

void TestHelperLinux::otherDisksAreRefused()
{
    QFETCH(bool, removable);
    QFETCH(QString, bus);
    QFETCH(QString, vendor);
    QFETCH(QString, code);
    Disk d;
    d.removable = removable;
    d.bus = bus;
    UsbFacts usb = vendor.isEmpty() ? UsbFacts() : usbOf(vendor, "1261");
    if (vendor.isEmpty()) {
        d.serial.clear();
    }
    DiskRequest r = requestOf(d);
    if (vendor.isEmpty()) {
        r.serial.clear();
        r.port.clear();
    }
    const DiskVerdict v = judgeDisk(r, inputsOf(d, usb));
    QVERIFY(!v.ok);
    QCOMPARE(v.code, code);
}

void TestHelperLinux::requestMustMatch()
{
    Disk d;
    const UsbFacts usb = usbOf("05ac", "1261");
    DiskRequest r = requestOf(d);
    r.sizeBytes += 512;
    QCOMPARE(judgeDisk(r, inputsOf(d, usb)).code, QStringLiteral("disk-changed"));
    r = requestOf(d);
    r.port = QStringLiteral("1-3");
    QCOMPARE(judgeDisk(r, inputsOf(d, usb)).code, QStringLiteral("disk-changed"));
    r = requestOf(d);
    r.serial = QStringLiteral("OTHER");
    QCOMPARE(judgeDisk(r, inputsOf(d, usb)).code, QStringLiteral("disk-changed"));
    r = requestOf(d);
    r.node = QStringLiteral("/dev/sdc");
    DiskInputs gone = inputsOf(d, usb);
    gone.block = blockFactsFromObjects(objectsOf(d), r.node);
    QCOMPARE(judgeDisk(r, gone).code, QStringLiteral("disk-gone"));
    // A partition, a loop device and a volume are no whole disk.
    r = requestOf(d);
    r.node = QStringLiteral("/dev/sdb2");
    QCOMPARE(judgeDisk(r, inputsOf(d, usb)).code, QStringLiteral("bad-target"));
    r.node = QStringLiteral("/dev/dm-0");
    QCOMPARE(judgeDisk(r, inputsOf(d, usb)).code, QStringLiteral("bad-target"));
    d.partition = true;
    QCOMPARE(judgeDisk(requestOf(d), inputsOf(d, usb)).code, QStringLiteral("bad-target"));
    d.partition = false;
    d.loop = true;
    QCOMPARE(judgeDisk(requestOf(d), inputsOf(d, usb)).code, QStringLiteral("bad-target"));
}

void TestHelperLinux::systemMustNotBeOnTheDisk()
{
    const Disk d;
    DiskInputs in = inputsOf(d, usbOf("05ac", "1261"));
    in.mounts = tacetos::device::parseMountinfo(
        QStringLiteral("36 29 8:18 / /home rw - ext4 /dev/sdb2 rw\n"));
    QCOMPARE(judgeDisk(requestOf(d), in).code, QStringLiteral("system-disk"));
    in.mounts = tacetos::device::parseMountinfo(
        QStringLiteral("36 29 8:18 / /run/media/u/IPOD rw - vfat /dev/sdb2 rw\n"));
    QVERIFY(judgeDisk(requestOf(d), in).ok);
    in.dataDir = QStringLiteral("/run/media/u/IPOD/backups");
    QCOMPARE(judgeDisk(requestOf(d), in).code, QStringLiteral("system-disk"));
    in.dataDir.clear();
    in.swaps += QStringLiteral("/dev/sdb2\tpartition\t100\t0\t-3\n");
    QCOMPARE(judgeDisk(requestOf(d), in).code, QStringLiteral("system-disk"));
}

// A copy of the part of /sys above an iPod: the names have colons, so the test is
// skipped where links are not real (see symlinks.h).
static void makeTree(const QString &root)
{
    const QString usb = root + QStringLiteral("/devices/pci0000:00/usb1/1-2");
    const QString disk = usb + QStringLiteral("/1-2:1.0/host6/target6:0:0/6:0:0:0/block/sdb");
    QVERIFY(QDir().mkpath(disk + QStringLiteral("/sdb2")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/dev/block")));
    auto put = [](const QString &path, const QString &text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    put(usb + QStringLiteral("/idVendor"), QStringLiteral("05ac\n"));
    put(usb + QStringLiteral("/idProduct"), QStringLiteral("1261\n"));
    put(usb + QStringLiteral("/busnum"), QStringLiteral("1\n"));
    put(usb + QStringLiteral("/devnum"), QStringLiteral("7\n"));
    put(usb + QStringLiteral("/serial"), QStringLiteral("000A27001A2B3C4D\n"));
    put(disk + QStringLiteral("/sdb2/partition"), QStringLiteral("2\n"));
    put(disk + QStringLiteral("/sdb2/dev"), QStringLiteral("8:18\n"));
    QVERIFY(QFile::link(disk, root + QStringLiteral("/dev/block/8:16")));
}

void TestHelperLinux::usbFactsFromTree()
{
    TACETOS_NEEDS_SYMLINKS();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeTree(dir.path());
    const UsbFacts u = readUsbFacts(dir.path(), 8, 16);
    QVERIFY(u.found);
    QCOMPARE(u.vendor, QStringLiteral("05ac"));
    QCOMPARE(u.product, QStringLiteral("1261"));
    QCOMPARE(u.port, QStringLiteral("1-2"));
    QCOMPARE(u.busNumber, 1);
    QCOMPARE(u.deviceNumber, 7);
    QCOMPARE(u.serial, QStringLiteral("000A27001A2B3C4D"));
    QCOMPARE(u.partitionNames, QStringList{QStringLiteral("sdb2")});
    QVERIFY(!readUsbFacts(dir.path(), 8, 32).found);
}

class FakeService : public DiskService
{
public:
    UdisksObjects data;
    QString ver = QStringLiteral("2.9.4");
    int opens = 0;
    bool lastWrite = false;
    QString version() override { return ver; }
    std::optional<UdisksObjects> objects(QString *) override { return data; }
    OpenResult open(const QString &, bool write) override
    {
        ++opens;
        lastWrite = write;
        OpenResult r;
        r.code = QStringLiteral("permission-denied");
        return r;
    }
};

void TestHelperLinux::realCheckWithFakeService()
{
    TACETOS_NEEDS_SYMLINKS();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeTree(dir.path());
    SystemLayer system;
    auto fake = std::make_unique<FakeService>();
    fake->data = objectsOf(Disk());
    FakeService *service = fake.get();
    system.disks = std::move(fake);
    system.linuxSystem = true;
    system.sysRoot = dir.path();
    system.mountinfo = dir.filePath(QStringLiteral("none"));
    system.swaps = dir.filePath(QStringLiteral("none"));
    QJsonObject target{{"kind", "disk"},
                       {"node", "/dev/sdb"},
                       {"sizeBytes", 160041885696.0},
                       {"serial", "000A27001A2B3C4D"},
                       {"port", "1-2"}};
    const RealDisk disk = checkRealDisk(target, QString(), system);
    QVERIFY2(disk.ok, qPrintable(disk.detail));
    QCOMPARE(disk.usbBus, 1);
    QCOMPARE(disk.usbDevice, 7);
    // Reading opens for reading only, and a refusal of the user is passed on.
    const CommandOutcome o = readStateOnDisk(system, disk, [](auto &&, auto &&) {}, [](auto &&) {});
    QCOMPARE(service->opens, 1);
    QVERIFY(!service->lastWrite);
    QCOMPARE(o.error, QStringLiteral("permission-denied"));
    // An old udisks2 is refused.
    service->ver = QStringLiteral("2.7.2");
    QCOMPARE(checkRealDisk(target, QString(), system).code, QStringLiteral("unsupported-system"));
}

void TestHelperLinux::versionAndIds()
{
    QVERIFY(udisksVersionSupportsOpenDevice(QStringLiteral("2.7.3")));
    QVERIFY(udisksVersionSupportsOpenDevice(QStringLiteral("2.10.1")));
    QVERIFY(!udisksVersionSupportsOpenDevice(QStringLiteral("2.7.2")));
    QVERIFY(!udisksVersionSupportsOpenDevice(QStringLiteral("1.9.9")));
    QVERIFY(!udisksVersionSupportsOpenDevice(QStringLiteral("")));
    // In step with models/models.toml: every `normal` and `disk_mode` id is accepted,
    // the DFU ids are not.
    QFile toml(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY(toml.open(QIODevice::ReadOnly));
    int seen = 0;
    for (const QString &line : QString::fromUtf8(toml.readAll()).split(QLatin1Char('\n'))) {
        const bool disk = line.startsWith(QLatin1String("normal = ")) ||
                          line.startsWith(QLatin1String("disk_mode = "));
        const bool dfu = line.startsWith(QLatin1String("dfu = "));
        const int at = line.indexOf(QLatin1String("05ac:"));
        if (!(disk || dfu) || at < 0)
            continue;
        ++seen;
        QCOMPARE(isIpodUsbId(QStringLiteral("05ac"), line.mid(at + 5, 4)), disk);
    }
    QVERIFY(seen > 10);
    QVERIFY(!isIpodUsbId(QStringLiteral("0781"), QStringLiteral("1261")));
}

static QByteArray page(quint8 code, const QByteArray &data)
{
    QByteArray a;
    a.append(char(0));
    a.append(char(code));
    a.append(char(data.size() >> 8));
    a.append(char(data.size() & 0xff));
    return a + data;
}

void TestHelperLinux::sheetIsAssembled()
{
    const QByteArray xml = "<?xml version=\"1.0\"?><plist><dict/></plist>";
    const QMap<quint8, QByteArray> pages = {{0xC0, page(0xC0, QByteArray("\xC1\xC2", 2))},
                                            {0xC1, page(0xC1, xml.left(10))},
                                            {0xC2, page(0xC2, xml.mid(10) + QByteArray(3, '\0'))}};
    QList<quint8> asked;
    const SheetResult r = readDeviceSheet([&](quint8 p, QByteArray &out, QString *) {
        asked.append(p);
        out = pages.value(p);
        return pages.contains(p);
    });
    QVERIFY(r.ok);
    QCOMPARE(r.text.toUtf8(), xml);
    QCOMPARE(asked, (QList<quint8>{0xC0, 0xC1, 0xC2}));
}

void TestHelperLinux::sheetRefusals()
{
    // No answer to page 0xC0.
    QCOMPARE(readDeviceSheet([](quint8, QByteArray &, QString *e) {
                 *e = QStringLiteral("x");
                 return false;
             }).code,
             QStringLiteral("sheet-unsupported"));
    // Not an XML document.
    QCOMPARE(readDeviceSheet([](quint8 p, QByteArray &out, QString *) {
                 out = p == 0xC0 ? page(0xC0, QByteArray("\xC1", 1)) : page(p, "hello");
                 return true;
             }).code,
             QStringLiteral("sheet-unreadable"));
    // Too long: the list asks for the same page again and again.
    const QByteArray big(250, 'a');
    QCOMPARE(readDeviceSheet([&](quint8 p, QByteArray &out, QString *) {
                 out = p == 0xC0 ? page(0xC0, QByteArray(300, '\xC1')) : page(p, big);
                 return true;
             }).code,
             QStringLiteral("sheet-too-big"));
    // The answer of another page is no answer.
    QCOMPARE(readDeviceSheet([](quint8, QByteArray &out, QString *) {
                 out = page(0x80, "x");
                 return true;
             }).code,
             QStringLiteral("sheet-unsupported"));
}

void TestHelperLinux::udevRuleAlreadyThere()
{
    QTemporaryDir dir;
    FakeRunner runner;
    UdevEnv env;
    env.rulesFile = dir.filePath(QStringLiteral("70.rules"));
    env.pkexecPath = QStringLiteral("/usr/bin/pkexec");
    env.runner = &runner;
    env.tempBase = dir.path();
    QFile f(env.rulesFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(udevRuleText().toUtf8());
    f.close();
    const UdevOutcome o = installUdevRule(env);
    QVERIFY(o.ok);
    QVERIFY(o.alreadyInstalled);
    QCOMPARE(runner.calls, 0);
    // The rule built into the helper is the file of the sources, and only the DFU id.
    QFile source(QStringLiteral(TACETOS_UDEV_RULE_FILE));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), udevRuleText().toUtf8());
}

void TestHelperLinux::udevRuleExitCodes()
{
    QTemporaryDir dir;
    FakeRunner runner;
    UdevEnv env;
    env.rulesFile = dir.filePath(QStringLiteral("70.rules"));
    env.pkexecPath = QStringLiteral("/usr/bin/pkexec");
    env.runner = &runner;
    env.tempBase = dir.path();
    runner.exitCode = 126;
    QCOMPARE(installUdevRule(env).code, QStringLiteral("cancelled"));
    runner.exitCode = 127;
    QCOMPARE(installUdevRule(env).code, QStringLiteral("permission-denied"));
    runner.exitCode = 1;
    QCOMPARE(installUdevRule(env).code, QStringLiteral("udev-failed"));
    // Exit 0 but nothing in place: the read back catches it.
    runner.exitCode = 0;
    QCOMPARE(installUdevRule(env).code, QStringLiteral("udev-failed"));
    runner.copyTo = env.rulesFile;
    const UdevOutcome ok = installUdevRule(env);
    QVERIFY(ok.ok);
    QVERIFY(!ok.alreadyInstalled);
    QCOMPARE(runner.program, QStringLiteral("/usr/bin/pkexec"));
    // No pkexec: the app shows the commands instead.
    QFile::remove(env.rulesFile);
    env.pkexecPath.clear();
    QCOMPARE(installUdevRule(env).code, QStringLiteral("unsupported-system"));
}

void TestHelperLinux::udevRuleCommandLine()
{
    const QStringList args = pkexecArguments(QStringLiteral("/tmp/x/rule"));
    QCOMPARE(args.size(), 5);
    QCOMPARE(args.at(0), QStringLiteral("/bin/sh"));
    QCOMPARE(args.at(1), QStringLiteral("-c"));
    QCOMPARE(args.last(), QStringLiteral("/tmp/x/rule"));
    // The script is fixed: the path comes only as "$1".
    QVERIFY(!args.at(2).contains(QStringLiteral("/tmp/x")));
    QVERIFY(args.at(2).contains(QStringLiteral(
        "install -m 0644 -o root -g root \"$1\" /etc/udev/rules.d/70-tacetos-dfu.rules")));
    QVERIFY(args.at(2).contains(QStringLiteral("udevadm control --reload-rules")));
}

QTEST_GUILESS_MAIN(TestHelperLinux)
#include "tst_helper_linux.moc"
