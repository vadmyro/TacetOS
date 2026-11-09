// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The watch on the mount table (docs/specs/detection.md section 9.1). The
// kernel only raises the exceptional condition on the real mountinfo, and a
// test cannot mount anything. So the watched file here is a fake one, and the
// change is reported by hand: the test checks that the file is opened, that
// the notifier has the right type, that a report sends
// disksChanged (and only that), and that a file which cannot be opened leaves
// the driver polling. No sleep.

#include "livedriver.h"
#include "mountwatcher.h"

#include <QSignalSpy>
#include <QTest>

using namespace tacetos::device;

namespace {

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return 0; }
};

class EmptyWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override { return QList<UsbDevice>(); }
    PortReading readPort(const UsbDevice &) override { return PortReading(); }
};

struct Rig
{
    Rig() : tracker(QList<Model>(), &clock, &world), driver(&tracker, &clock) {}

    FakeClock clock;
    EmptyWorld world;
    LiveTracker tracker;
    LiveDriver driver;
};

QString fakeMountinfo()
{
    return QStringLiteral(TACETOS_DEVICE_FIXTURES "/mountinfo-linux.txt");
}

QString missingFile()
{
    return QStringLiteral(TACETOS_DEVICE_FIXTURES "/no-such-mountinfo.txt");
}

} // namespace

class TstDeviceMountWatcher : public QObject
{
    Q_OBJECT

private slots:
    void opensTheFakeFile();
    void missingFileDoesNotOpen();
    void firingSendsOneDisksHint();
    void quietFileSendsNothing();
    void attachGivesTheDriverASource();
    void failedAttachLeavesPolling();
    void realMountinfoOpens();
};

void TstDeviceMountWatcher::opensTheFakeFile()
{
    MountWatcher watcher(fakeMountinfo());
    QCOMPARE(watcher.path(), fakeMountinfo());
    QVERIFY(!watcher.isOpen());
    QVERIFY(watcher.findChildren<QSocketNotifier *>().isEmpty());

    QVERIFY(watcher.open());
    QVERIFY(watcher.isOpen());

    // Section 9.1: the kernel reports a mount change as POLLPRI.
    const auto notifiers = watcher.findChildren<QSocketNotifier *>();
    QCOMPARE(notifiers.size(), 1);
    QCOMPARE(notifiers.first()->type(), QSocketNotifier::Exception);
    QVERIFY(notifiers.first()->isEnabled());

    // Opening twice is harmless.
    QVERIFY(watcher.open());
    QCOMPARE(watcher.findChildren<QSocketNotifier *>().size(), 1);
}

void TstDeviceMountWatcher::missingFileDoesNotOpen()
{
    MountWatcher watcher(missingFile());
    QVERIFY(!watcher.open());
    QVERIFY(!watcher.isOpen());
    QVERIFY(watcher.findChildren<QSocketNotifier *>().isEmpty());
}

void TstDeviceMountWatcher::firingSendsOneDisksHint()
{
    MountWatcher watcher(fakeMountinfo());
    QVERIFY(watcher.open());
    QSignalSpy disks(&watcher, &EventSource::disksChanged);
    QSignalSpy usb(&watcher, &EventSource::usbChanged);

    watcher.reportChange();
    QCOMPARE(disks.size(), 1);

    // Each mount or unmount is a hint of its own.
    watcher.reportChange();
    QCOMPARE(disks.size(), 2);
    QCOMPARE(usb.size(), 0);
}

// A regular file never has an exceptional condition: nothing is sent unless
// the kernel says so.
void TstDeviceMountWatcher::quietFileSendsNothing()
{
    MountWatcher watcher(fakeMountinfo());
    QVERIFY(watcher.open());
    QSignalSpy disks(&watcher, &EventSource::disksChanged);
    QCoreApplication::processEvents();
    QCOMPARE(disks.size(), 0);
}

void TstDeviceMountWatcher::attachGivesTheDriverASource()
{
    Rig r;
    QVERIFY(r.driver.polling());
    MountWatcher *watcher = MountWatcher::attach(&r.driver, fakeMountinfo());
    QVERIFY(watcher != nullptr);
    QVERIFY(watcher->isOpen());
    QCOMPARE(watcher->parent(), &r.driver);
    QVERIFY(!r.driver.polling());
}

// Section 9.1: no watch, no error: the driver polls.
void TstDeviceMountWatcher::failedAttachLeavesPolling()
{
    Rig r;
    QVERIFY(MountWatcher::attach(&r.driver, missingFile()) == nullptr);
    QVERIFY(r.driver.polling());
    QVERIFY(r.driver.findChildren<MountWatcher *>().isEmpty());
}

void TstDeviceMountWatcher::realMountinfoOpens()
{
    MountWatcher watcher;
    if (!watcher.open())
        QSKIP("The mountinfo of this process cannot be opened here.");
    QVERIFY(watcher.isOpen());
    QCOMPARE(watcher.path(), QStringLiteral("/proc/self/mountinfo"));
}

QTEST_GUILESS_MAIN(TstDeviceMountWatcher)
#include "tst_device_mountwatcher.moc"
