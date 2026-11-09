// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The D-Bus side of udisksbackend.h, against a fake disk service on a private
// peer to peer connection (no bus daemon). The real service and the real system
// bus are never used. Where no local socket can be opened (a sandbox) the tests
// are skipped, unless the CI variable is set: then they fail.

#include "udisks.h"
#include "udisksbackend.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusServer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace tacetos::device;

namespace {

using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;

const QString kBlockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb2");
const QString kWholePath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb");
const QString kDrivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/iPod");

// What the fake service remembers. Written by the thread of the service, read
// by the test once the call it made has been answered.
struct State
{
    QStringList calls;
    // The error name that Mount, Unmount and PowerOff answer with, when set.
    QString refuse;
    QString mountedAt;
};

class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    explicit FakeManager(State *state) : state_(state) {}

public slots:
    Objects GetManagedObjects()
    {
        Objects objects;
        Interfaces whole;
        whole[QStringLiteral("org.freedesktop.UDisks2.Block")] = {
            {"Device", QByteArray("/dev/sdb\0", 9)},
            {"DeviceNumber", qulonglong(makeDeviceNumber(8, 16))},
            {"Drive", QVariant::fromValue(QDBusObjectPath(kDrivePath))},
            {"IdType", QString()},
        };
        objects.insert(QDBusObjectPath(kWholePath), whole);

        Interfaces part;
        part[QStringLiteral("org.freedesktop.UDisks2.Block")] = {
            {"Device", QByteArray("/dev/sdb2\0", 10)},
            {"DeviceNumber", qulonglong(makeDeviceNumber(8, 18))},
            {"Drive", QVariant::fromValue(QDBusObjectPath(kDrivePath))},
            {"IdType", QStringLiteral("vfat")},
            {"IdLabel", QStringLiteral("IPOD")},
            {"Size", qulonglong(4000000)},
        };
        part[QStringLiteral("org.freedesktop.UDisks2.Partition")] = {{"Number", 2u}};
        QByteArrayList points;
        if (!state_->mountedAt.isEmpty())
            points << state_->mountedAt.toUtf8() + '\0';
        part[QStringLiteral("org.freedesktop.UDisks2.Filesystem")] = {
            {"MountPoints", QVariant::fromValue(points)}};
        objects.insert(QDBusObjectPath(kBlockPath), part);

        Interfaces drive;
        drive[QStringLiteral("org.freedesktop.UDisks2.Drive")] = {
            {"Vendor", QStringLiteral("Apple")},
            {"ConnectionBus", QStringLiteral("usb")},
            {"Ejectable", false},
            {"CanPowerOff", true},
        };
        objects.insert(QDBusObjectPath(kDrivePath), drive);
        return objects;
    }

private:
    State *state_;
};

class FakeFilesystem : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UDisks2.Filesystem")

public:
    explicit FakeFilesystem(State *state) : state_(state) {}

public slots:
    QString Mount(const QVariantMap &options)
    {
        state_->calls << QStringLiteral("mount ") + options.value("options").toString();
        if (!state_->refuse.isEmpty()) {
            sendErrorReply(state_->refuse, QStringLiteral("refused"));
            return {};
        }
        state_->mountedAt = QStringLiteral("/run/media/u/IPOD");
        return state_->mountedAt;
    }

    void Unmount(const QVariantMap &options)
    {
        state_->calls << QStringLiteral("unmount force=%1").arg(options.contains("force"));
        if (!state_->refuse.isEmpty()) {
            sendErrorReply(state_->refuse, QStringLiteral("refused"));
            return;
        }
        state_->mountedAt.clear();
    }

private:
    State *state_;
};

class FakeDrive : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UDisks2.Drive")

public:
    explicit FakeDrive(State *state) : state_(state) {}

public slots:
    void PowerOff(const QVariantMap &)
    {
        state_->calls << QStringLiteral("poweroff");
        if (!state_->refuse.isEmpty())
            sendErrorReply(state_->refuse, QStringLiteral("refused"));
    }

    void Eject(const QVariantMap &) { state_->calls << QStringLiteral("eject"); }

private:
    State *state_;
};

// Lives in its own thread: the test calls it with blocking calls, and the
// service must answer while the test waits.
class FakeService : public QObject
{
    Q_OBJECT

public slots:
    // Listens on the address. Returns the address that a client connects to, or
    // an empty string when the address cannot be used.
    QString start(const QString &address, State *state)
    {
        state_ = state;
        server_ = new QDBusServer(address);
        if (!server_->isConnected()) {
            qWarning() << "fake service:" << address << server_->lastError().name()
                       << server_->lastError().message();
            stop();
            return {};
        }
        // Over TCP there is no user to check: let any client in.
        server_->setAnonymousAuthenticationAllowed(true);
        connect(server_, &QDBusServer::newConnection, this, &FakeService::serve);
        return server_->address();
    }

    void stop()
    {
        for (const QString &name : std::as_const(connections_))
            QDBusConnection::disconnectFromPeer(name);
        connections_.clear();
        delete manager_;
        delete filesystem_;
        delete drive_;
        manager_ = nullptr;
        filesystem_ = nullptr;
        drive_ = nullptr;
        delete server_;
        server_ = nullptr;
    }

private slots:
    void serve(const QDBusConnection &connection)
    {
        QDBusConnection peer = connection;
        connections_ << peer.name();
        manager_ = new FakeManager(state_);
        filesystem_ = new FakeFilesystem(state_);
        drive_ = new FakeDrive(state_);
        const bool done =
            peer.registerObject(QStringLiteral("/org/freedesktop/UDisks2"), manager_,
                                QDBusConnection::ExportAllSlots) &&
            peer.registerObject(kBlockPath, filesystem_, QDBusConnection::ExportAllSlots) &&
            peer.registerObject(kDrivePath, drive_, QDBusConnection::ExportAllSlots);
        if (!done)
            qWarning() << "fake service: an object was not registered";
    }

private:
    State *state_ = nullptr;
    QDBusServer *server_ = nullptr;
    QStringList connections_;
    FakeManager *manager_ = nullptr;
    FakeFilesystem *filesystem_ = nullptr;
    FakeDrive *drive_ = nullptr;
};

// The tests cannot run where no local socket opens. A developer machine skips
// (nothing is checked, and the output says so); the CI must not.
#define SKIP_OR_FAIL(message)                                                                      \
    do {                                                                                           \
        if (qEnvironmentVariableIsSet("CI"))                                                       \
            QFAIL(message);                                                                        \
        else                                                                                       \
            QSKIP(message);                                                                        \
    } while (false)

LinuxDisk ipodDisk()
{
    LinuxDisk disk;
    disk.name = QStringLiteral("sdb");
    disk.node = QStringLiteral("/dev/sdb");
    disk.partitions = {{QStringLiteral("sdb2"), 2, 8, 18}};
    return disk;
}

} // namespace

class TestUdisksDBus : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void readsTheObjects();
    void mountsAndUnmounts();
    void passesReadOnly();
    void refusalComesBack();
    void ejectPowersOff();
    void noServiceOnTheBus();

private:
    DiskControl control()
    {
        return DiskControl(std::make_unique<UdisksDBusBackend>(
            QDBusConnection(QStringLiteral("client")), QString::fromLatin1(kUdisksService)));
    }

    QTemporaryDir folder_;
    QThread thread_;
    FakeService *service_ = nullptr;
    State state_;
};

void TestUdisksDBus::initTestCase()
{
    qDBusRegisterMetaType<Objects>();
    qDBusRegisterMetaType<Interfaces>();
    // What the real service sends for the mount points: an array of byte strings.
    qDBusRegisterMetaType<QByteArrayList>();

    QVERIFY(folder_.isValid());
    service_ = new FakeService;
    service_->moveToThread(&thread_);
    thread_.start();

    // A socket file first, then a local TCP port with anonymous clients.
    const QStringList candidates = {QStringLiteral("unix:tmpdir=") + folder_.path(),
                                    QStringLiteral("tcp:host=127.0.0.1"),
                                    QStringLiteral("tcp:host=localhost")};
    for (const QString &candidate : candidates) {
        QString address;
        QMetaObject::invokeMethod(service_, "start", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QString, address), Q_ARG(QString, candidate),
                                  Q_ARG(State *, &state_));
        if (address.isEmpty())
            continue;
        const QDBusConnection client =
            QDBusConnection::connectToPeer(address, QStringLiteral("client"));
        if (client.isConnected())
            return;
        qWarning() << "client:" << address << client.lastError().name()
                   << client.lastError().message();
        QDBusConnection::disconnectFromPeer(QStringLiteral("client"));
        QMetaObject::invokeMethod(service_, "stop", Qt::BlockingQueuedConnection);
    }
    SKIP_OR_FAIL("no local D-Bus socket can be opened here");
}

void TestUdisksDBus::cleanupTestCase()
{
    if (service_) {
        QMetaObject::invokeMethod(service_, "stop", Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait();
        delete service_;
    }
    QDBusConnection::disconnectFromPeer(QStringLiteral("client"));
}

void TestUdisksDBus::init()
{
    state_.calls.clear();
    state_.refuse.clear();
    state_.mountedAt.clear();
}

void TestUdisksDBus::readsTheObjects()
{
    UdisksDBusBackend backend(QDBusConnection(QStringLiteral("client")),
                              QString::fromLatin1(kUdisksService));
    UdisksReply error;
    const auto objects = backend.managedObjects(&error);
    QVERIFY2(objects.has_value(), qPrintable(error.errorName));

    state_.mountedAt = QStringLiteral("/run/media/u/My iPod");
    const UdisksSnapshot snapshot = parseUdisksObjects(*backend.managedObjects(&error));
    QCOMPARE(snapshot.blocks.size(), 2);
    QCOMPARE(snapshot.drives.size(), 1);
    for (const UdisksBlock &block : snapshot.blocks) {
        QCOMPARE(block.drivePath, kDrivePath);
        if (block.node == QLatin1String("/dev/sdb2")) {
            QCOMPARE(block.partitionNumber, 2);
            QCOMPARE(block.filesystem, QStringLiteral("vfat"));
            QCOMPARE(block.label, QStringLiteral("IPOD"));
            QCOMPARE(block.sizeBytes, quint64(4000000));
            QCOMPARE(block.deviceNumber, makeDeviceNumber(8, 18));
            QCOMPARE(block.mountPoints, QStringList{QStringLiteral("/run/media/u/My iPod")});
        }
    }
    QVERIFY(snapshot.drives.first().canPowerOff);
    QCOMPARE(snapshot.drives.first().connectionBus, QStringLiteral("usb"));
}

void TestUdisksDBus::mountsAndUnmounts()
{
    DiskControl disks = control();
    const DiskResult mounted = disks.mount(ipodDisk());
    QVERIFY2(mounted.ok(), qPrintable(mounted.detail));
    QCOMPARE(mounted.mountPoint, QStringLiteral("/run/media/u/IPOD"));
    QCOMPARE(state_.calls, QStringList{QStringLiteral("mount ")});

    const DiskResult again = disks.mount(ipodDisk());
    QVERIFY(again.alreadyMounted);
    QCOMPARE(again.mountPoint, QStringLiteral("/run/media/u/IPOD"));
    QCOMPARE(state_.calls.size(), 1);

    const DiskResult unmounted = disks.unmount(ipodDisk());
    QVERIFY(unmounted.ok());
    QCOMPARE(unmounted.unmounted, 1);
    // Never forced.
    QCOMPARE(state_.calls.last(), QStringLiteral("unmount force=0"));
    QVERIFY(state_.mountedAt.isEmpty());
}

void TestUdisksDBus::passesReadOnly()
{
    DiskControl disks = control();
    QVERIFY(disks.mount(ipodDisk(), true).ok());
    QCOMPARE(state_.calls, QStringList{QStringLiteral("mount ro")});
}

void TestUdisksDBus::refusalComesBack()
{
    DiskControl disks = control();
    state_.refuse = QStringLiteral("org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain");
    const DiskResult refused = disks.mount(ipodDisk());
    QCOMPARE(refused.error, DiskError::NotAuthorized);
    QCOMPARE(refused.detail, QStringLiteral("refused"));
}

void TestUdisksDBus::ejectPowersOff()
{
    DiskControl disks = control();
    state_.mountedAt = QStringLiteral("/run/media/u/IPOD");
    const DiskResult result = disks.eject(ipodDisk());
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QVERIFY(result.released);
    QCOMPARE(state_.calls,
             (QStringList{QStringLiteral("unmount force=0"), QStringLiteral("poweroff")}));

    // A busy volume stops the eject before the drive is touched.
    state_.calls.clear();
    state_.mountedAt = QStringLiteral("/run/media/u/IPOD");
    state_.refuse = QStringLiteral("org.freedesktop.UDisks2.Error.DeviceBusy");
    QCOMPARE(disks.eject(ipodDisk()).error, DiskError::Busy);
    QCOMPARE(state_.calls, QStringList{QStringLiteral("unmount force=0")});
}

void TestUdisksDBus::noServiceOnTheBus()
{
    // A connection that nothing answers on: the same error as a bus that lost
    // the service.
    UdisksDBusBackend backend(QDBusConnection(QStringLiteral("nobody")),
                              QString::fromLatin1(kUdisksService));
    UdisksReply error;
    QVERIFY(!backend.managedObjects(&error).has_value());
    QCOMPARE(diskErrorFromName(error.errorName), DiskError::ServiceMissing);
}

QTEST_GUILESS_MAIN(TestUdisksDBus)
#include "tst_device_udisksdbus.moc"
