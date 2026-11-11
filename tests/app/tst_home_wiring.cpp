// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The home screen wired to the device code (docs/specs/home-screen.md section 11.3): a fake
// source, a fake clock, fake disk actions and a function that records the folders it is asked to
// open. Nothing reads the real /sys, nothing calls D-Bus, nothing writes a journal file.

#include "constants.h"
#include "home/homescreen.h"
#include "mainwindow.h"
#include "models.h"

#include <QLabel>
#include <QPushButton>
#include <QTest>

using namespace tacetos;
using namespace tacetos::device;

namespace {

const QString kPort = QStringLiteral("1-2");
const QString kClassic = QStringLiteral("05ac:1261");
const QString kMountPoint = QStringLiteral("/run/media/ipod");

class FakeClock : public Clock
{
public:
    qint64 nowMs() const override { return now; }
    qint64 now = 0;
};

class FakeWorld : public DeviceSource
{
public:
    explicit FakeWorld(const QList<Model> &models) : models_(models) {}

    std::optional<QList<UsbDevice>> readUsb() override { return usb; }
    PortReading readPort(const UsbDevice &device) override { return ports.value(device.portPath); }

    void plug(const QString &usbId, const QString &port)
    {
        const UsbMatch match = matchUsbId(models_, usbId);
        UsbDevice device;
        device.usbId = usbId;
        device.mode = match.mode;
        device.candidates = match.candidates;
        device.portPath = port;
        usb << device;
    }

    void unplug(const QString &port)
    {
        for (qsizetype i = usb.size() - 1; i >= 0; --i) {
            if (usb.at(i).portPath == port)
                usb.removeAt(i);
        }
        ports.remove(port);
    }

    void giveDisk(const QString &port)
    {
        DiskInfo disk;
        disk.node = QStringLiteral("/dev/sdb");
        disk.sizeBytes = 120'000'000'000;
        disk.sectorSize = 512;
        ports[port].disk = disk;
    }

    void giveVolume(const QString &port)
    {
        VolumeInfo volume;
        volume.mountPoint = kMountPoint;
        volume.filesystem = QStringLiteral("vfat");
        volume.totalBytes = 118'000'000'000;
        volume.freeBytes = 50'000'000'000;
        ports[port].volume = volume;
    }

    QList<UsbDevice> usb;
    QHash<QString, PortReading> ports;

private:
    QList<Model> models_;
};

class FakeActions : public DiskActions
{
public:
    DiskResult run(OperationKind kind, const QString &portPath) override
    {
        ran << QStringLiteral("%1 %2").arg(operationKindName(kind), portPath);
        return next;
    }

    DiskResult next;
    QStringList ran;
};

// Keeps the jobs until the test runs them, to see `busy`.
class ManualExecutor : public Executor
{
public:
    bool submit(Job job) override
    {
        jobs << std::move(job);
        return true;
    }

    void runNext()
    {
        Job job = jobs.takeFirst();
        job.started();
        job.finished(job.work());
    }

    QList<Job> jobs;
};

} // namespace

class TstHomeWiring : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void ipodAppearsAndGoes();
    void ejectEndsWithTheCardGone();
    void openOnReadyOpensTheMountPoint();
    void openOnNotMountedMountsThenOpens();
    void openReadOnlyUsesMountRo();
    void failedMountOpensNothing();
    void refreshReadsAgain();
    void startsWithTheEmbeddedModels();

private:
    // Everything a window needs, in the order it needs it.
    struct Setup
    {
        explicit Setup(const QList<Model> &models)
            : world(models), window(&world, &clock, &actions, &executor,
                                    [this](const QString &folder) { opened << folder; })
        {
        }

        FakeClock clock;
        FakeWorld world;
        FakeActions actions;
        ManualExecutor executor;
        QStringList opened;
        MainWindow window;

        void plug()
        {
            world.plug(kClassic, kPort);
            window.tracker()->usbEvent();
        }
        void unplug()
        {
            world.unplug(kPort);
            window.tracker()->usbEvent();
        }
        void giveDisk(bool volume)
        {
            world.giveDisk(kPort);
            if (volume)
                world.giveVolume(kPort);
            window.tracker()->diskEvent();
        }
        // A disk without a volume is only `not-mounted` once the wait for the disk is over.
        void waitForVolume()
        {
            clock.now += kDiskWaitMs;
            window.driver()->wake();
        }
        // The open button of an iPod that is not open exists on Linux only (the other systems
        // open it themselves), so elsewhere the request comes from the screen's signal.
        void requestMount()
        {
            if (currentHostSystem() == HostSystem::Linux)
                card()->openButton()->click();
            else
                emit window.homeScreen()->actionRequested(kPort, HomeAction::Mount);
        }
        QString kind() const
        {
            const auto entry = window.stateModel()->entry(kPort);
            return entry ? screenKindName(entry->state.kind) : QStringLiteral("none");
        }
        IpodCard *card() const
        {
            const QList<IpodCard *> cards = window.homeScreen()->cards();
            return cards.isEmpty() ? nullptr : cards.first();
        }
    };

    QList<Model> models_;
};

void TstHomeWiring::init()
{
    // The same list as the app, from its resource.
    const ModelList list = loadModels(QStringLiteral(":/data/models.toml"));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    models_ = list.models;
}

void TstHomeWiring::ipodAppearsAndGoes()
{
    Setup s(models_);
    QCOMPARE(s.window.homeScreen()->cards().size(), 0);

    // The same simulated second: the clock does not move.
    s.clock.now = 5000;
    s.plug();
    QCOMPARE(s.window.homeScreen()->cards().size(), 1);
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));
    QCOMPARE(s.window.homeScreen()->cards().size(), 1);
    QCOMPARE(s.card()->portPath(), kPort);
    QVERIFY(!s.card()->findChild<QLabel *>(QStringLiteral("ipodState"))->text().isEmpty());

    s.unplug();
    QCOMPARE(s.window.homeScreen()->cards().size(), 0);
}

void TstHomeWiring::ejectEndsWithTheCardGone()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));

    s.clock.now = 1000;
    s.card()->ejectButton()->click();
    QCOMPARE(s.kind(), QStringLiteral("busy"));
    QVERIFY(!s.card()->ejectButton()->isEnabled());
    QCOMPARE(s.executor.jobs.size(), 1);

    // The drive powers off and the USB device goes, then the service answers.
    s.clock.now = 1500;
    s.unplug();
    s.clock.now = 2000;
    s.actions.next.released = true;
    s.actions.next.unmounted = 1;
    s.executor.runNext();
    QCOMPARE(s.actions.ran, QStringList{QStringLiteral("eject 1-2")});
    QCOMPARE(s.kind(), QStringLiteral("released"));
    QCOMPARE(s.window.homeScreen()->cards().size(), 1);

    s.clock.now = 2000 + kReleasedKeepMs;
    s.window.driver()->wake();
    QCOMPARE(s.kind(), QStringLiteral("none"));
    QCOMPARE(s.window.homeScreen()->cards().size(), 0);
}

void TstHomeWiring::openOnReadyOpensTheMountPoint()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));

    s.card()->openButton()->click();
    QCOMPARE(s.opened, QStringList{kMountPoint});
    // Not an operation: nothing is queued and the card does not change.
    QVERIFY(s.executor.jobs.isEmpty());
    QCOMPARE(s.kind(), QStringLiteral("ready"));
}

void TstHomeWiring::openOnNotMountedMountsThenOpens()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(false);
    s.waitForVolume();
    QCOMPARE(s.kind(), QStringLiteral("not-mounted"));

    s.requestMount();
    QCOMPARE(s.kind(), QStringLiteral("busy"));
    QVERIFY(s.opened.isEmpty());

    s.actions.next.mountPoint = QStringLiteral("/run/media/mounted");
    QVERIFY2(!s.executor.jobs.isEmpty(), "the request did not queue a job");
    s.executor.runNext();
    QCOMPARE(s.actions.ran, QStringList{QStringLiteral("mount 1-2")});
    QCOMPARE(s.kind(), QStringLiteral("done"));
    QCOMPARE(s.opened, QStringList{QStringLiteral("/run/media/mounted")});

    // The end of `done` does not open it again.
    s.clock.now = kDoneShownMs;
    s.window.driver()->wake();
    QCOMPARE(s.opened.size(), 1);
}

void TstHomeWiring::openReadOnlyUsesMountRo()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(false);
    s.waitForVolume();
    // The card only offers Open read only for a Mac formatted disk, which the fake source does
    // not make, so the request comes from the screen's signal.
    emit s.window.homeScreen()->actionRequested(kPort, HomeAction::MountReadOnly);
    QCOMPARE(s.kind(), QStringLiteral("busy"));
    s.actions.next.mountPoint = kMountPoint;
    QVERIFY2(!s.executor.jobs.isEmpty(), "the request did not queue a job");
    s.executor.runNext();
    QCOMPARE(s.actions.ran, QStringList{QStringLiteral("mount-ro 1-2")});
    QCOMPARE(s.opened, QStringList{kMountPoint});
}

void TstHomeWiring::failedMountOpensNothing()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(false);
    s.waitForVolume();
    QCOMPARE(s.kind(), QStringLiteral("not-mounted"));

    s.requestMount();
    s.actions.next.error = DiskError::Busy;
    s.actions.next.detail = QStringLiteral("device is busy");
    QVERIFY2(!s.executor.jobs.isEmpty(), "the request did not queue a job");
    s.executor.runNext();

    QCOMPARE(s.kind(), QStringLiteral("failed"));
    QVERIFY(s.opened.isEmpty());
    QVERIFY(s.card()->findChild<QWidget *>(QStringLiteral("banner"))->isVisibleTo(s.card()));
    // Failed stays, and it does not open anything later either.
    s.clock.now = 60'000;
    s.window.driver()->wake();
    QVERIFY(s.opened.isEmpty());
}

void TstHomeWiring::refreshReadsAgain()
{
    Setup s(models_);
    s.plug();
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));

    // The world changed without any event: only Refresh finds it.
    s.world.unplug(kPort);
    QCOMPARE(s.window.homeScreen()->cards().size(), 1);
    emit s.window.homeScreen()->refreshRequested();
    QCOMPARE(s.window.homeScreen()->cards().size(), 0);
}

void TstHomeWiring::startsWithTheEmbeddedModels()
{
    // The window reads the model list from its resource, so a ready iPod gets the name of its
    // model on the card.
    Setup s(models_);
    s.plug();
    s.giveDisk(true);
    const QString name = s.card()->findChild<QLabel *>(QStringLiteral("ipodName"))->text();
    QVERIFY(!name.isEmpty());
    // No journal file in a test, and no error about the model list.
    QVERIFY(!s.window.journal()->fileActive());
    for (const journal::Entry &entry : s.window.journal()->entries())
        QVERIFY2(entry.level != journal::Level::Error, qPrintable(entry.text));
    // What the detection says goes to the journal.
    QVERIFY(!s.window.journal()->entries().isEmpty());
}

QTEST_MAIN(TstHomeWiring)
#include "tst_home_wiring.moc"
