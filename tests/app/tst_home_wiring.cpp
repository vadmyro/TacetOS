// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The home screen wired to the device code (docs/specs/home-screen.md section 11.3): a fake
// source, a fake clock, fake disk actions and a function that records the folders it is asked to
// open. Nothing reads the real /sys, nothing calls D-Bus, nothing writes a journal file.

#include "constants.h"
#include "home/homescreen.h"
#include "home/updatewatcher.h"
#include "mainwindow.h"
#include "models.h"

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
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

// Update section 9: the local channel, a folder made by the test. Never the real network.
update::ChannelAddress folderChannel(const QString &folder)
{
    update::ChannelAddress address;
    address.kind = update::ChannelAddress::Kind::Folder;
    address.folder = folder;
    return address;
}

bool writeIndex(const QString &folder, const QString &version)
{
    const QByteArray data =
        QStringLiteral("{\"format\": 1, \"version\": \"%1\", \"built\": \"2026-11-26T10:00:00Z\", "
                       "\"packages\": [{\"target\": \"ipod6g\", \"manifest\": "
                       "\"tacetos-ipod6g-%1.json\", \"size\": 812, \"sha256\": \"%2\"}]}")
            .arg(version, QString(64, QLatin1Char('a')))
            .toUtf8();
    QFile file(folder + QLatin1Char('/') + QString(kUpdateIndexFile));
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

int linesWith(const journal::Journal *journal, const QString &text)
{
    int count = 0;
    for (const journal::Entry &entry : journal->entries())
        if (entry.text.contains(text))
            ++count;
    return count;
}

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
    void mountsByItselfOnArrival();
    void refusedMountOnArrivalIsAWarning();
    void refreshReadsAgain();
    void noChannelReadWithoutATacetosIpod();
    void tacetosCardShowsTheUpdate();
    void refreshReadsTheChannelAgain();
    void aChannelThatFailsShowsNothing();
    void startsWithTheEmbeddedModels();

private:
    // Everything a window needs, in the order it needs it.
    struct Setup
    {
        // The mount on arrival is off unless asked for: the other tests are about the mount
        // that the user asks for.
        explicit Setup(const QList<Model> &models, bool mountOnArrival = false)
            : world(models), window(&world, &clock, &actions, &executor,
                                    [this](const QString &folder) { opened << folder; })
        {
            if (window.autoMounter())
                window.autoMounter()->setEnabled(mountOnArrival);
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
        // The files of TacetOS on the volume, with this version in the info file.
        void giveTacetos(const QString &version)
        {
            world.ports[kPort].markers.firmware.files = FirmwareFiles::Tacetos;
            world.ports[kPort].markers.firmware.version = version;
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

// Section 2.8 of the state spec: Linux only, no file manager after it.
void TstHomeWiring::mountsByItselfOnArrival()
{
    Setup s(models_, true);
    if (!s.window.autoMounter())
        QSKIP("the system mounts a FAT32 volume by itself");
    s.plug();
    s.giveDisk(false);
    s.waitForVolume();
    QCOMPARE(s.kind(), QStringLiteral("busy"));
    QCOMPARE(s.executor.jobs.size(), 1);

    s.actions.next.mountPoint = kMountPoint;
    s.executor.runNext();
    QCOMPARE(s.actions.ran, QStringList{QStringLiteral("mount 1-2")});
    QCOMPARE(s.kind(), QStringLiteral("done"));
    QVERIFY(s.opened.isEmpty());
}

void TstHomeWiring::refusedMountOnArrivalIsAWarning()
{
    Setup s(models_, true);
    if (!s.window.autoMounter())
        QSKIP("the system mounts a FAT32 volume by itself");
    s.plug();
    s.giveDisk(false);
    s.waitForVolume();
    s.actions.next.error = DiskError::NotAuthorized;
    s.actions.next.detail = QStringLiteral("not authorized");
    s.executor.runNext();

    // Not `failed`: the card is as it was, with its Open button.
    QCOMPARE(s.kind(), QStringLiteral("not-mounted"));
    QVERIFY(s.opened.isEmpty());
    int warnings = 0;
    for (const journal::Entry &entry : s.window.journal()->entries()) {
        if (entry.level == journal::Level::Warning &&
            entry.text.contains(QStringLiteral("not authorized")))
            ++warnings;
    }
    QCOMPARE(warnings, 1);
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

// Update section 8: without an iPod whose firmware line is `tacetos`, no read at all.
void TstHomeWiring::noChannelReadWithoutATacetosIpod()
{
    QTemporaryDir channel;
    QVERIFY(channel.isValid());
    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.3.0")));
    Setup s(models_);
    s.window.attachUpdateChannel(folderChannel(channel.path()));
    QVERIFY(s.window.updateWatcher());
    emit s.window.homeScreen()->refreshRequested();
    s.plug();
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));
    emit s.window.homeScreen()->refreshRequested();
    QTest::qWait(50);
    QCOMPARE(s.window.updateWatcher()->reads(), 0);
    QVERIFY(!s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(linesWith(s.window.journal(), QStringLiteral("update")), 0);
}

// Read once per launch, the first time the card is shown; the card then offers the update.
void TstHomeWiring::tacetosCardShowsTheUpdate()
{
    QTemporaryDir channel;
    QVERIFY(channel.isValid());
    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.3.0")));
    Setup s(models_);
    s.window.attachUpdateChannel(folderChannel(channel.path()));
    s.giveTacetos(QStringLiteral("0.2.0"));
    s.plug();
    s.giveDisk(true);
    QCOMPARE(s.kind(), QStringLiteral("ready"));
    QTRY_VERIFY(s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(s.card()->updateLabel()->text(), QStringLiteral("Update available: TacetOS 0.3.0"));
    QCOMPARE(s.card()->wizardButton()->text(), QStringLiteral("Update"));
    QCOMPARE(s.window.updateWatcher()->updateFor(kPort), QStringLiteral("0.3.0"));
    QCOMPARE(linesWith(s.window.journal(), QStringLiteral("Update TacetOS 0.3.0 for ipod6g")), 1);

    // The list changes again: no second read, no second line.
    s.window.tracker()->diskEvent();
    s.clock.now += 1000;
    s.window.driver()->wake();
    QTest::qWait(50);
    QCOMPARE(s.window.updateWatcher()->reads(), 1);
    QCOMPARE(linesWith(s.window.journal(), QStringLiteral("Update TacetOS 0.3.0 for ipod6g")), 1);

    // The button asks for the update window.
    QSignalSpy asked(&s.window, &MainWindow::wizardRequested);
    s.card()->wizardButton()->click();
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.at(0).at(1).value<HomeAction>(), HomeAction::UpdateWizard);

    // The iPod leaves: nothing is left for its port.
    s.unplug();
    QCOMPARE(s.window.updateWatcher()->updateFor(kPort), QString());
}

void TstHomeWiring::refreshReadsTheChannelAgain()
{
    QTemporaryDir channel;
    QVERIFY(channel.isValid());
    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.3.0")));
    Setup s(models_);
    s.window.attachUpdateChannel(folderChannel(channel.path()));
    s.giveTacetos(QStringLiteral("0.2.0"));
    s.plug();
    s.giveDisk(true);
    QTRY_VERIFY(s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(s.window.updateWatcher()->reads(), 1);

    // The channel now has nothing newer: Refresh reads it again and the update goes.
    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.2.0")));
    emit s.window.homeScreen()->refreshRequested();
    QTRY_VERIFY(!s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(s.window.updateWatcher()->reads(), 2);
    QCOMPARE(s.card()->wizardButton()->text(), QStringLiteral("Uninstall"));

    // And comes back on the next Refresh.
    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.4.0")));
    emit s.window.homeScreen()->refreshRequested();
    QTRY_VERIFY(s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(s.card()->updateLabel()->text(), QStringLiteral("Update available: TacetOS 0.4.0"));
    QCOMPARE(s.window.updateWatcher()->reads(), 3);
}

// Silence: no index, a bad one, an older version. Nothing on the card, one log line each, never
// an error.
void TstHomeWiring::aChannelThatFailsShowsNothing()
{
    QTemporaryDir channel;
    QVERIFY(channel.isValid());
    Setup s(models_);
    s.window.attachUpdateChannel(folderChannel(channel.path()));
    s.giveTacetos(QStringLiteral("0.2.0"));
    s.plug();
    s.giveDisk(true);
    QTRY_COMPARE(linesWith(s.window.journal(), QStringLiteral("No update")), 1);

    QFile bad(channel.filePath(QString(kUpdateIndexFile)));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not json");
    bad.close();
    emit s.window.homeScreen()->refreshRequested();
    QTRY_COMPARE(linesWith(s.window.journal(), QStringLiteral("No update")), 2);

    QVERIFY(writeIndex(channel.path(), QStringLiteral("0.1.0")));
    emit s.window.homeScreen()->refreshRequested();
    QTRY_COMPARE(linesWith(s.window.journal(), QStringLiteral("No update")), 3);

    QVERIFY(!s.card()->updateLabel()->isVisibleTo(s.card()));
    QCOMPARE(s.card()->wizardButton()->text(), QStringLiteral("Uninstall"));
    for (const journal::Entry &entry : s.window.journal()->entries())
        QVERIFY2(entry.level != journal::Level::Error, qPrintable(entry.text));
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
