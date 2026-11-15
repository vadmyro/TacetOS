// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md sections 6.9 and 6.10: `classic-dfu` on a "real" iPod
// Classic, and `resume-paused`. The layer of the system is the fake of the
// installer tests (the USB bus over time, the eject, a clock that moves only when
// the flow sleeps), the iPod in DFU mode is the fake device of the same tests, and
// the parts of the image are synthetic bytes. Never libusb, udisks2 or a real iPod.

#include "classiccmds.h"
#include "constants.h"
#include "dfuimage.h"
#include "fakesystemlayer.h"
#include "helperconstants.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::helper;
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

bool put(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// The four generated parts (bootloader-classic.md section 8), made of patterns.
bool writeParts(const QString &dir)
{
    QByteArray installer = pattern(kPayload, 0x11);
    // The last 0x50 bytes are the bootloader header, empty in the program.
    installer.replace(kPayload - 0x50, 0x50, QByteArray(0x50, '\0'));
    const QDir d(dir);
    return put(d.filePath(QString(kDfuPartSignature)), pattern(16, 0x31)) &&
           put(d.filePath(QString(kDfuPartCertificate)), pattern(698, 0x55)) &&
           put(d.filePath(QString(kDfuPartInstaller)), installer) &&
           put(d.filePath(QString(kDfuPartUninstaller)), pattern(kUninstallPayload, 0x77));
}

// A bootloader file of section 9: the checksum (big-endian), the model tag, the body.
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

UsbDeviceInfo deviceAt(quint16 product, int address)
{
    UsbDeviceInfo d;
    d.id = {0x05AC, product};
    d.bus = 1;
    d.address = address;
    return d;
}

FakeBusStep at(int ms, const QList<UsbDeviceInfo> &devices)
{
    FakeBusStep s;
    s.fromMs = ms;
    s.devices = devices;
    return s;
}

// The command owns the layer it is given; this one hands over the test's fake.
class Borrowed : public ClassicSystemLayer
{
public:
    explicit Borrowed(FakeSystemLayer &fake) : m_fake(fake) {}
    HostSystem hostSystem() const override { return m_fake.hostSystem(); }
    QList<UsbDeviceInfo> listUsbDevices() override { return m_fake.listUsbDevices(); }
    std::unique_ptr<UsbDfuLink> makeLink(const UsbDeviceInfo &d) override
    {
        return m_fake.makeLink(d);
    }
    bool unmountIpod(QString *error) override { return m_fake.unmountIpod(error); }
    bool ipodListed() override { return m_fake.ipodListed(); }
    QList<SystemProcess> findAppleProcesses() override { return m_fake.findAppleProcesses(); }
    bool pauseProcess(const SystemProcess &p, QString *e) override
    {
        return m_fake.pauseProcess(p, e);
    }
    bool resumeProcess(const SystemProcess &p, QString *e) override
    {
        return m_fake.resumeProcess(p, e);
    }

private:
    FakeSystemLayer &m_fake;
};

class Hooks : public CommandHooks
{
public:
    QStringList stages;
    QList<qint64> seconds;
    QStringList logs;
    bool cancel = false;
    bool firstWrite = false;
    void log(const QString &, const QString &text) override { logs.append(text); }
    void stage(const QString &name) override
    {
        if (stages.isEmpty() || stages.last() != name)
            stages.append(name);
    }
    void progress(qint64 done, qint64) override { seconds.append(done); }
    bool cancelRequested() override { return cancel; }
    bool beforeFirstWrite() override
    {
        firstWrite = true;
        return true;
    }
};

} // namespace

class TestHelperClassic : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_parts;
    QTemporaryDir m_data;
    std::unique_ptr<FakeSystemLayer> m_layer;
    std::unique_ptr<FakeDfuDevice> m_device;
    QString m_bootFile;

    std::unique_ptr<SystemLayer> system(bool gateOpen = true)
    {
        auto s = std::make_unique<SystemLayer>();
        s->realWrites = gateOpen;
        s->dfuPartsDir = m_parts.path();
        FakeSystemLayer *fake = m_layer.get();
        s->classicLayer = [fake](SystemLayer &, const QString &) {
            return std::make_unique<Borrowed>(*fake);
        };
        s->sleep = m_layer->options().sleep;
        return s;
    }

    static RealDisk classic(const QString &product = QStringLiteral("1261"))
    {
        RealDisk d;
        d.ok = true;
        d.blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
        d.sizeBytes = qint64(120) * 1000 * 1000 * 1000;
        d.usbBus = 1;
        d.usbDevice = 5;
        d.usbProduct = product;
        return d;
    }

    QJsonObject installArgs() const
    {
        return {{"operation", "install"},
                {"target", QJsonObject()},
                {"bootloaderFile", m_bootFile},
                {"stateBefore", "absent"},
                {"dataDir", m_data.path()}};
    }

    QJsonObject uninstallArgs() const
    {
        return {{"operation", "uninstall"},
                {"target", QJsonObject()},
                {"stateBefore", "present"},
                {"dataDir", m_data.path()}};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_parts.isValid());
        QVERIFY(m_data.isValid());
        QVERIFY(writeParts(m_parts.path()));
        m_bootFile = m_data.filePath(QStringLiteral("bootloader-classic.bin"));
        QVERIFY(put(m_bootFile, bootFile(pattern(4096, 0x42))));
    }

    void init()
    {
        m_layer = std::make_unique<FakeSystemLayer>();
        m_device = std::make_unique<FakeDfuDevice>();
        m_layer->device = m_device.get();
        // The iPod is in DFU mode once the eject is done.
        m_layer->bus = {at(0, {deviceAt(0x1223, 7)})};
    }

    // The image the helper builds is the one the iPod receives, after the eject and
    // the wait, with the countdown and the stages of section 10.1.
    void installSendsTheImage()
    {
        auto s = system();
        Hooks hooks;
        const CommandOutcome o = runClassicDfuOnDisk(installArgs(), *s, classic(), hooks);
        QVERIFY2(o.ok, qPrintable(o.error + QLatin1Char(' ') + o.detail));
        QFile file(m_bootFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const DfuImage expected =
            buildInstallerImage(loadDfuParts(m_parts.path()).parts, file.readAll());
        QVERIFY(expected.ok);
        QVERIFY(m_device->received().startsWith(expected.image));
        QVERIFY(m_layer->unmountCalls >= 1);
        QCOMPARE(hooks.stages,
                 (QStringList{"check", "eject", "spin-down", "wait-device", "send", "resume"}));
        QVERIFY(hooks.firstWrite);
        QVERIFY2(!hooks.seconds.isEmpty(), "the waits give one progress per second");
    }

    void uninstallSendsTheUninstaller()
    {
        auto s = system();
        Hooks hooks;
        const CommandOutcome o = runClassicDfuOnDisk(uninstallArgs(), *s, classic(), hooks);
        QVERIFY2(o.ok, qPrintable(o.error + QLatin1Char(' ') + o.detail));
        const DfuImage expected = buildUninstallerImage(loadDfuParts(m_parts.path()).parts);
        QVERIFY(m_device->received().startsWith(expected.image));
    }

    // Section 9: a development build builds and checks the image, then stops before
    // the eject. It never asks for DFU mode.
    void devBuildStopsBeforeTheEject()
    {
        auto s = system(false);
        Hooks hooks;
        const CommandOutcome o = runClassicDfuOnDisk(installArgs(), *s, classic(), hooks);
        QCOMPARE(o.error, QString(err::devBuild));
        QCOMPARE(m_layer->unmountCalls, 0);
        QVERIFY(m_device->received().isEmpty());
    }

    void notAClassic()
    {
        auto s = system();
        Hooks hooks;
        const CommandOutcome o =
            runClassicDfuOnDisk(installArgs(), *s, classic(QStringLiteral("1260")), hooks);
        QCOMPARE(o.error, QString(err::notClassic));
        QCOMPARE(m_layer->unmountCalls, 0);
    }

    void wrongArguments_data()
    {
        QTest::addColumn<QString>("remove");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::newRow("install without a file") << "bootloaderFile" << "" << "";
        QTest::newRow("uninstall with a file") << "" << "operation" << "uninstall";
        QTest::newRow("another operation") << "" << "operation" << "update";
        QTest::newRow("another state") << "" << "stateBefore" << "maybe";
    }

    void wrongArguments()
    {
        QFETCH(QString, remove);
        QFETCH(QString, key);
        QFETCH(QString, value);
        QJsonObject args = installArgs();
        if (!remove.isEmpty())
            args.remove(remove);
        if (!key.isEmpty())
            args.insert(key, value);
        auto s = system();
        Hooks hooks;
        QCOMPARE(runClassicDfuOnDisk(args, *s, classic(), hooks).error, QString(err::badRequest));
        QCOMPARE(m_layer->unmountCalls, 0);
    }

    // Without the generated parts, or without USB access, the Classic install is
    // not available in this build.
    void notAvailable()
    {
        auto s = system();
        s->dfuPartsDir.clear();
        Hooks hooks;
        QCOMPARE(runClassicDfuOnDisk(installArgs(), *s, classic(), hooks).error,
                 QString(err::unavailable));
        auto t = system();
        t->classicLayer = {};
        QCOMPARE(runClassicDfuOnDisk(installArgs(), *t, classic(), hooks).error,
                 QString(err::unavailable));
        QCOMPARE(m_layer->unmountCalls, 0);
    }

    // Two iPods in DFU mode at once, or a device that is not `05ac:1223`, or none:
    // nothing is sent (bootloader-classic.md section 6).
    void refusedDevices_data()
    {
        QTest::addColumn<QList<FakeBusStep>>("bus");
        QTest::newRow("two in DFU mode")
            << QList<FakeBusStep>{at(0, {deviceAt(0x1223, 7), deviceAt(0x1223, 8)})};
        QTest::newRow("normal mode only") << QList<FakeBusStep>{at(0, {deviceAt(0x1261, 7)})};
        QTest::newRow("unplugged") << QList<FakeBusStep>{at(0, {})};
    }

    void refusedDevices()
    {
        QFETCH(QList<FakeBusStep>, bus);
        m_layer->bus = bus;
        auto s = system();
        Hooks hooks;
        const CommandOutcome o = runClassicDfuOnDisk(installArgs(), *s, classic(), hooks);
        QCOMPARE(o.error, QString(err::dfuFailed));
        QVERIFY(!o.detail.isEmpty());
        QVERIFY(m_device->received().isEmpty());
        QVERIFY(!hooks.firstWrite);
    }

    // A cancel during the waits: nothing is sent.
    void cancelBeforeTheTransfer()
    {
        auto s = system();
        Hooks hooks;
        hooks.cancel = true;
        const CommandOutcome o = runClassicDfuOnDisk(installArgs(), *s, classic(), hooks);
        QCOMPARE(o.error, QString(err::cancelled));
        QVERIFY(m_device->received().isEmpty());
    }

    // Section 6.10: with no state file, nothing to resume, at once.
    void resumeWithNothingPaused()
    {
        auto s = system();
        const CommandOutcome o = runResumePaused({{"dataDir", m_data.path()}}, *s);
        QVERIFY(o.ok);
        QVERIFY(o.data.value(QStringLiteral("names")).toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHelperClassic)
#include "tst_helper_classic.moc"
