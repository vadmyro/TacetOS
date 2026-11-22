// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "checks.h"
#include "home/homescreen.h"
#include "home/ipodcard.h"
#include "mainwindow.h"
#include "runner.h"
#include "style/appicon.h"
#include "wizard/wizardwindow.h"

#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos;
using namespace tacetos::device;

namespace {

// A world with no iPod, so the window never reads the real /sys or calls the disk service.
class EmptyWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override { return QList<UsbDevice>(); }
    PortReading readPort(const UsbDevice &) override { return PortReading(); }
};

class FixedClock : public Clock
{
public:
    qint64 nowMs() const override { return 0; }
};

class NoActions : public DiskActions
{
public:
    DiskResult run(OperationKind, const QString &) override { return DiskResult(); }
};

class NoExecutor : public Executor
{
public:
    bool submit(Job) override { return false; }
};

// One Classic with its disk and its volume, and the serial the wizard needs.
class OneClassic : public DeviceSource
{
public:
    static constexpr const char *kPort = "1-2";
    static constexpr const char *kSerial = "000A27001A2B3C4D";

    OneClassic()
    {
        const ModelList list = loadModels(QStringLiteral(":/data/models.toml"));
        const UsbMatch match = matchUsbId(list.models, QStringLiteral("05ac:1261"));
        usb.usbId = QStringLiteral("05ac:1261");
        usb.mode = match.mode;
        usb.candidates = match.candidates;
        usb.portPath = QString::fromLatin1(kPort);
        usb.serial = QString::fromLatin1(kSerial);
        DiskInfo disk;
        disk.node = QStringLiteral("/dev/sdb");
        disk.sizeBytes = 120'000'000'000;
        disk.sectorSize = 512;
        reading.disk = disk;
        VolumeInfo volume;
        volume.mountPoint = QStringLiteral("/run/media/ipod");
        volume.filesystem = QStringLiteral("vfat");
        volume.totalBytes = 118'000'000'000;
        volume.freeBytes = 50'000'000'000;
        reading.volume = volume;
    }

    std::optional<QList<UsbDevice>> readUsb() override { return QList<UsbDevice>{usb}; }
    PortReading readPort(const UsbDevice &) override { return reading; }

    UsbDevice usb;
    PortReading reading;
};

struct Parts
{
    EmptyWorld world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;

    MainWindow *makeWindow() { return new MainWindow(&world, &clock, &actions, &executor, {}); }
};

} // namespace

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void hasTitleAndMinimumSize();
    void opens();
    void hasAppIcon();
    void noWizardUntilAttached();
    void wizardOpensFromTheCard();
    void wizardRefusesToQuitWhileWriting();
    void devBuildOrNoHelperNeverWritesForReal();
    void journalSaysWhyAStepWaitsAgain();
    void journalSaysWhenAStepStartsAndEnds();
    void flowSerialOfAClassicUnderTacetos();
};

void TestMainWindow::hasTitleAndMinimumSize()
{
    Parts parts;
    std::unique_ptr<MainWindow> window(parts.makeWindow());
    QCOMPARE(window->windowTitle(), QStringLiteral("TacetOS"));
    QVERIFY(window->minimumWidth() >= 800);
    QVERIFY(window->minimumHeight() >= 560);
}

void TestMainWindow::opens()
{
    Parts parts;
    std::unique_ptr<MainWindow> window(parts.makeWindow());
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    QVERIFY(window->isVisible());
    QCOMPARE(window->centralWidget(), static_cast<QWidget *>(window->homeScreen()));
}

void TestMainWindow::hasAppIcon()
{
    tacetos::applyAppIcon();
    QVERIFY(!qApp->windowIcon().isNull());
}

void TestMainWindow::noWizardUntilAttached()
{
    Parts parts;
    std::unique_ptr<MainWindow> window(parts.makeWindow());
    QVERIFY(!window->wizard());
    QVERIFY(window->quitRefusal().isEmpty());
}

void TestMainWindow::wizardOpensFromTheCard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.backupRoot = dir.filePath(QStringLiteral("backups"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    config.appVersion = QStringLiteral("0.2.0");
    config.models = loadModels(QStringLiteral(":/data/models.toml")).models;
    window.attachWizard(config);
    QVERIFY(window.wizard());

    window.tracker()->usbEvent();
    window.tracker()->diskEvent();
    const QString port = QString::fromLatin1(OneClassic::kPort);
    const QString serial = QString::fromLatin1(OneClassic::kSerial);
    QCOMPARE(window.homeScreen()->cards().size(), 1);
    QVERIFY(!window.wizard()->manager().windowFor(serial));

    // A port that no iPod has opens nothing.
    QVERIFY(!window.wizard()->open(QStringLiteral("9-9"), HomeAction::InstallWizard));

    window.homeScreen()->cards().first()->wizardButton()->click();
    WizardWindow *wizard = window.wizard()->manager().windowFor(serial);
    QVERIFY(wizard);
    QCOMPARE(wizard->target().serial, serial);
    QCOMPARE(wizard->target().family, wizard::Family::Dfu);
    QVERIFY(wizard->runner());
    QCOMPARE(wizard->pageName(), QStringLiteral("install"));
    QVERIFY(wizard->isVisible());

    // The same iPod does not get a second window.
    WizardWindow *again = window.wizard()->open(port, HomeAction::InstallWizard);
    QCOMPARE(again, wizard);
    QVERIFY(window.quitRefusal().isEmpty());

    QCOMPARE(window.homeScreen()->cards().first()->wizardButton()->text(),
             QStringLiteral("Show the wizard"));

    // The window goes: the iPod can have another, and the card leaves "Show the wizard".
    wizard->close();
    QTRY_VERIFY(!window.wizard()->manager().windowFor(serial));
    QTRY_COMPARE(window.homeScreen()->cards().first()->wizardButton()->text(),
                 QStringLiteral("Install TacetOS"));
    WizardWindow *second = window.wizard()->open(port, HomeAction::UninstallWizard);
    QVERIFY(second);
    QCOMPARE(second->pageName(), QStringLiteral("uninstall"));
}

void TestMainWindow::wizardRefusesToQuitWhileWriting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.backupRoot = dir.filePath(QStringLiteral("backups"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    config.models = loadModels(QStringLiteral(":/data/models.toml")).models;
    window.attachWizard(config);
    window.tracker()->usbEvent();
    window.tracker()->diskEvent();
    QVERIFY(
        window.wizard()->open(QString::fromLatin1(OneClassic::kPort), HomeAction::InstallWizard));
    // Nothing started: a window that waits for the first click does not stop the app.
    QVERIFY(window.quitRefusal().isEmpty());
}

void TestMainWindow::devBuildOrNoHelperNeverWritesForReal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    window.attachWizard(config);
    // Without the helper the writes are off, whatever the build says.
    QVERIFY(!window.wizard()->helperFound());
    QVERIFY(!window.wizard()->realWrites());

    // With a file in its place the writes follow the build: off in a development build.
    QFile fake(dir.filePath(QStringLiteral("tacet-helper")));
    QVERIFY(fake.open(QIODevice::WriteOnly));
    fake.close();
    WizardLauncher::Config withHelper = config;
    withHelper.helperProgram = fake.fileName();
    WizardLauncher launcher(withHelper, *window.tracker(), nullptr);
    QVERIFY(launcher.helperFound());
    QCOMPARE(launcher.realWrites(), tacetos::installer::realWritesAllowed());
}

// A step that waits again writes the reason and the helper's detail in the journal of the app,
// which the screen alone showed before, and the USB detail of a failed DFU try. The first wait,
// without a reason, writes nothing.
void TestMainWindow::journalSaysWhyAStepWaitsAgain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.backupRoot = dir.filePath(QStringLiteral("backups"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    config.models = loadModels(QStringLiteral(":/data/models.toml")).models;
    window.attachWizard(config);
    window.tracker()->usbEvent();
    window.tracker()->diskEvent();
    const QString port = QString::fromLatin1(OneClassic::kPort);
    WizardWindow *wizard = window.wizard()->open(port, HomeAction::InstallWizard);
    QVERIFY(wizard);
    QVERIFY(window.journal());

    const auto waitsAgain = [&] {
        QStringList lines;
        for (const journal::Entry &entry : window.journal()->entries()) {
            if (entry.text.contains(QLatin1String("waits again")))
                lines << entry.text;
        }
        return lines;
    };
    const QString step = QStringLiteral("classic-bootloader");
    emit wizard->runner()->stepWaiting(step, wizard::WaitKind::ReadyForDfu,
                                       {{"reason", ""}, {"hint", ""}});
    QVERIFY(waitsAgain().isEmpty());

    emit wizard->runner()->stepWaiting(
        step, wizard::WaitKind::ReadyForDfu,
        {{"reason", "dfu-failed"}, {"hint", "The iPod did not appear in DFU mode."}});
    emit wizard->runner()->stepWaiting(
        step, wizard::WaitKind::ReadyForDfu,
        {{"reason", "dfu-failed"},
         {"hint", "The iPod could not be opened."},
         {"usb", "Claiming the USB interface failed: Other error (LIBUSB_ERROR_OTHER)"}});
    emit wizard->runner()->stepWaiting(step, wizard::WaitKind::ReadyForDfu,
                                       {{"reason", "disk-mode-instead"}, {"hint", ""}});
    QCOMPARE(
        waitsAgain(),
        (QStringList{QStringLiteral("step classic-bootloader waits again: dfu-failed (The "
                                    "iPod did not appear in DFU mode.)"),
                     QStringLiteral("step classic-bootloader waits again: dfu-failed (The "
                                    "iPod could not be opened.) [USB: Claiming the USB interface "
                                    "failed: Other error (LIBUSB_ERROR_OTHER)]"),
                     QStringLiteral("step classic-bootloader waits again: disk-mode-instead")}));
    for (const journal::Entry &entry : window.journal()->entries()) {
        if (entry.text.contains(QLatin1String("waits again"))) {
            QCOMPARE(entry.port, port);
            QCOMPARE(entry.level, journal::Level::Warning);
        }
    }
}

// Each step writes a line when it starts and when it is done, so that a report shows a success
// too, and not only the failures.
void TestMainWindow::journalSaysWhenAStepStartsAndEnds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.backupRoot = dir.filePath(QStringLiteral("backups"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    config.models = loadModels(QStringLiteral(":/data/models.toml")).models;
    window.attachWizard(config);
    window.tracker()->usbEvent();
    window.tracker()->diskEvent();
    const QString port = QString::fromLatin1(OneClassic::kPort);
    WizardWindow *wizard = window.wizard()->open(port, HomeAction::InstallWizard);
    QVERIFY(wizard);

    emit wizard->runner()->stepStarted(5, QStringLiteral("verify"));
    emit wizard->runner()->stepDone(5, QStringLiteral("verify"));
    QStringList lines;
    for (const journal::Entry &entry : window.journal()->entries()) {
        if (entry.text.startsWith(QLatin1String("step verify"))) {
            lines << entry.text;
            QCOMPARE(entry.port, port);
            QCOMPARE(entry.level, journal::Level::Info);
        }
    }
    QCOMPARE(lines, (QStringList{QStringLiteral("step verify started"),
                                 QStringLiteral("step verify done")}));
}

// D45: a Classic under TacetOS gives another serial. An install record that saw it gives back
// Apple's serial, which names the iPod everywhere in the app; any other serial stays as it is.
void TestMainWindow::flowSerialOfAClassicUnderTacetos()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("backups/APPLE1/2026-11-22_135136"));
    QVERIFY(QDir().mkpath(folder));
    QFile record(QDir(folder).filePath(QStringLiteral("install-record.json")));
    QVERIFY(record.open(QIODevice::WriteOnly));
    record.write(R"({"format": 1, "operation": "install", "date": "2026-11-22T13:07:05Z",
                     "serial": "APPLE1", "serialUnderTacetos": "TACET1"})");
    record.close();

    OneClassic world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;
    MainWindow window(&world, &clock, &actions, &executor, {});
    WizardLauncher::Config config;
    config.dataDir = dir.filePath(QStringLiteral("data"));
    config.backupRoot = dir.filePath(QStringLiteral("backups"));
    config.helperProgram = dir.filePath(QStringLiteral("no-such-helper"));
    WizardLauncher launcher(config, *window.tracker(), nullptr);

    device::DetectedIpod ipod;
    ipod.serial = QStringLiteral("TACET1");
    QCOMPARE(launcher.flowSerial(ipod), QStringLiteral("APPLE1"));
    ipod.serial = QStringLiteral("APPLE1");
    QCOMPARE(launcher.flowSerial(ipod), QStringLiteral("APPLE1"));
    ipod.serial = QStringLiteral("OTHER");
    QCOMPARE(launcher.flowSerial(ipod), QStringLiteral("OTHER"));
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
