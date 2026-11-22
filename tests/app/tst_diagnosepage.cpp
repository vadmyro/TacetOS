// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "diagnose.h"
#include "runner.h"
#include "wizard/diagnosepage.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

DiagnoseInput goodClassic()
{
    DiagnoseInput in;
    in.os = Os::Linux;
    in.osRelease = QStringLiteral("ID=fedora\n");
    in.udisks = {true, true, QStringLiteral("2.10.1")};
    in.helper = {true, true, true, true, true};
    in.udevRulePresent = true;
    in.backup = {QStringLiteral("/home/anna/Documents/TacetOS Backups"), true, false,
                 100LL * 1000 * 1000 * 1000};
    in.backupNeedBytes = 50LL * 1000 * 1000;
    in.family = Family::Dfu;
    in.kind = FlowKind::Install;
    in.path = InstallPath::Keep;
    IpodFacts &ipod = in.ipod;
    ipod.helperRead = true;
    ipod.modelSupported = true;
    ipod.methodAgrees = true;
    ipod.serial = QStringLiteral("8K1234ABCD");
    ipod.capacityGb = 120;
    ipod.volumeReady = true;
    ipod.filesystem = Filesystem::Fat32;
    ipod.volumeFree = 2LL * 1000 * 1000 * 1000;
    ipod.volumeSize = 120LL * 1000 * 1000 * 1000;
    ipod.appleVersion = QStringLiteral("2.0.1");
    in.firmwareNeedBytes = 40LL * 1000 * 1000;
    return in;
}

// Sources that the test changes between two runs of the step. The step reads them from the
// worker thread, the test writes them while the runner is stopped or waiting.
class FakeSources : public DiagnoseSources
{
public:
    DiagnoseInput in = goodClassic();

    Os os() const override { return in.os; }
    QString osRelease() const override { return in.osRelease; }
    UdisksInfo udisks() const override { return in.udisks; }
    HelperInfo helper() const override { return in.helper; }
    bool udevRulePresent() const override { return in.udevRulePresent; }
    FolderInfo backupFolder() const override { return in.backup; }
    qint64 backupNeedBytes(InstallPath) const override { return in.backupNeedBytes; }
    IpodFacts ipod() const override { return in.ipod; }
    PackageCheck package(const QString &) const override { return in.package; }
    qint64 firmwareNeedBytes(const PackageCheck &) const override { return in.firmwareNeedBytes; }
};

class DoneStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

using Change = std::function<void(DiagnoseInput &)>;

QLabel *label(QWidget *page, const QString &name)
{
    return page->findChild<QLabel *>(name);
}

QPushButton *button(QWidget *page, const QString &name)
{
    return page->findChild<QPushButton *>(name);
}

QString commandOf(QWidget *page, const QString &id)
{
    auto *field = page->findChild<QPlainTextEdit *>(QStringLiteral("command-") + id);
    return field ? field->toPlainText() : QString();
}

} // namespace

class TestDiagnosePage : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        FakeSources sources;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<DiagnosePage> page;
        std::unique_ptr<QSignalSpy> reported;
        std::unique_ptr<QSignalSpy> stopped;
        FlowShape shape;

        Fixture(const Change &change = Change())
        {
            if (change)
                change(sources.in);
            shape.family = Family::Dfu;
            shape.kind = FlowKind::Install;
            shape.path = InstallPath::Keep;
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (name == step::diagnose)
                        return std::make_unique<DiagnoseStep>(sources);
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<DiagnosePage>(runner.get(), QStringLiteral("8K1234ABCD"),
                                                  QStringLiteral("classic-6g"), Family::Dfu);
            page->setPause(50);
            page->show();
            reported = std::make_unique<QSignalSpy>(runner.get(), &WizardRunner::checksReported);
            stopped = std::make_unique<QSignalSpy>(runner.get(), &WizardRunner::flowStopped);
        }

        void start()
        {
            WizardRunner::Start s;
            s.serial = QStringLiteral("8K1234ABCD");
            s.model = QStringLiteral("classic-6g");
            s.shape = shape;
            s.packageManifest = QStringLiteral("/p/m.json");
            QCOMPARE(runner->start(s), QString());
        }

        // The step ended on a failed check: the list is shown and Check again is on.
        bool settled(int stops)
        {
            return stopped->count() >= stops && button(page.get(), "checkAgain")->isEnabled();
        }
    };

private slots:
    void initTestCase() { qRegisterMetaType<QList<Check>>(); }

    void everythingPasses()
    {
        Fixture f;
        // Long enough that the pause cannot end while QTRY_VERIFY waits below, even on a slow
        // runner: the test checks that the signal has not been sent yet.
        f.page->setPause(1000);
        QSignalSpy canGo(f.page.get(), &DiagnosePage::canGoOnChanged);
        QSignalSpy ready(f.page.get(), &DiagnosePage::readyToGoOn);
        f.start();
        // Only the battery is left: a question, and the button.
        QTRY_VERIFY(button(f.page.get(), "batteryYes") &&
                    button(f.page.get(), "batteryYes")->isEnabled());
        QVERIFY(!f.page->canGoOn());
        QCOMPARE(label(f.page.get(), "sign-battery")->text(), QStringLiteral("?"));
        QCOMPARE(label(f.page.get(), "message-battery")->text(),
                 QStringLiteral("Is the iPod charged?"));
        QVERIFY(button(f.page.get(), "checkAgain")->isEnabled());
        // The computer is in its group, the iPod in the other.
        QVERIFY(f.page->findChild<QLabel *>("groupComputer"));
        QVERIFY(f.page->findChild<QLabel *>("groupIpod"));
        QVERIFY(f.page->findChild<QWidget *>("check-system"));
        QCOMPARE(f.page->findChild<QWidget *>("check-system")->property("result").toString(),
                 QStringLiteral("pass"));

        button(f.page.get(), "batteryYes")->click();
        QTRY_VERIFY(f.page->isReady());
        QVERIFY(f.page->canGoOn());
        QCOMPARE(canGo.count(), 1);
        QCOMPARE(canGo.first().first().toBool(), true);
        QCOMPARE(label(f.page.get(), "diagnoseNote")->text(),
                 QStringLiteral("Everything is ready"));
        QCOMPARE(label(f.page.get(), "sign-battery")->text(), QString(QChar(0x2713)));
        QVERIFY(!button(f.page.get(), "checkAgain")->isEnabled());
        // The page stays a moment, then the next step can show.
        QCOMPARE(ready.count(), 0);
        QTRY_COMPARE(ready.count(), 1);
    }

    void cases_data()
    {
        QTest::addColumn<Change>("change");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("result");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("blocks");

        QTest::newRow("no helper")
            << Change([](DiagnoseInput &i) { i.helper.found = false; }) << "helper" << "fail"
            << "helper program is missing" << QString() << true;
        QTest::newRow("development build")
            << Change([](DiagnoseInput &i) { i.helper.realWrites = false; }) << "writes" << "warn"
            << "development build" << QString() << false;
        QTest::newRow("no udisks2")
            << Change([](DiagnoseInput &i) { i.udisks.present = false; }) << "udisks" << "fail"
            << "udisks2 is missing" << "sudo dnf install udisks2" << true;
        QTest::newRow("udisks2 stopped")
            << Change([](DiagnoseInput &i) { i.udisks.running = false; }) << "udisks" << "fail"
            << "not running" << "sudo systemctl enable --now udisks2" << true;
        QTest::newRow("ubuntu") << Change([](DiagnoseInput &i) {
            i.osRelease = QStringLiteral("ID=ubuntu\nID_LIKE=debian\n");
            i.udisks.present = false;
        }) << "udisks" << "fail" << "udisks2 is missing"
                                << "sudo apt install udisks2" << true;
        QTest::newRow("backup folder on the iPod")
            << Change([](DiagnoseInput &i) { i.backup.onIpod = true; }) << "backup-folder"
            << "fail" << "on an iPod" << QString() << true;
        QTest::newRow("no room for the backup")
            << Change([](DiagnoseInput &i) { i.backup.freeBytes = 1000; }) << "backup-space"
            << "fail" << "" << QString() << true;
        QTest::newRow("model not supported")
            << Change([](DiagnoseInput &i) { i.ipod.modelSupported = false; }) << "model" << "fail"
            << "" << QString() << true;
        QTest::newRow("format") << Change([](DiagnoseInput &i) {
            i.ipod.filesystem = Filesystem::Other;
        }) << "format"
                                << "fail" << "" << QString() << true;
        QTest::newRow("battery asked") << Change([](DiagnoseInput &) {}) << "battery" << "ask"
                                       << "Is the iPod charged?" << QString() << true;
    }

    void cases()
    {
        QFETCH(Change, change);
        QFETCH(QString, id);
        QFETCH(QString, result);
        QFETCH(QString, text);
        QFETCH(QString, command);
        QFETCH(bool, blocks);

        Fixture f(change);
        f.start();
        QTRY_VERIFY(f.reported->count() >= 1);
        QWidget *row = nullptr;
        QTRY_VERIFY((row = f.page->findChild<QWidget *>(QStringLiteral("check-") + id)));
        QCOMPARE(row->property("result").toString(), result);
        QVERIFY(label(f.page.get(), QStringLiteral("message-") + id)->text().contains(text));
        QCOMPARE(commandOf(f.page.get(), id), command);
        // A blocking check keeps the user on the page; a warning does not stop anything.
        if (blocks) {
            QVERIFY(!f.page->canGoOn());
            QVERIFY(!f.page->isReady());
        }
        if (result == "ask")
            return;
        if (blocks)
            QTRY_VERIFY(f.settled(1));
    }

    void signsAndColours()
    {
        Fixture f([](DiagnoseInput &i) {
            i.helper.realWrites = false;
            i.udisks.running = false;
        });
        f.start();
        QTRY_VERIFY(f.settled(1));
        struct Expected
        {
            const char *id;
            QChar sign;
            const char *signRole;
            const char *messageRole;
        };
        const Expected cases[] = {
            {"system", QChar(0x2713), "", "secondary"},
            {"writes", QLatin1Char('!'), "secondary", ""},
            {"udisks", QChar(0x2715), "danger", "danger"},
        };
        for (const Expected &c : cases) {
            const QString id = QString::fromLatin1(c.id);
            QCOMPARE(label(f.page.get(), "sign-" + id)->text(), QString(c.sign));
            QCOMPARE(label(f.page.get(), "sign-" + id)->property("role").toString(),
                     QString::fromLatin1(c.signRole));
            QCOMPARE(label(f.page.get(), "message-" + id)->property("role").toString(),
                     QString::fromLatin1(c.messageRole));
        }
        // The tick takes the accent of the palette, never a colour of its own.
        QVERIFY(label(f.page.get(), "sign-system")->styleSheet().contains("palette(highlight)"));
        QVERIFY(label(f.page.get(), "name-udisks")->text() == QLatin1String("Disk service"));
    }

    void skippedSign()
    {
        // The iPod side is not checked while the helper is missing.
        Fixture f([](DiagnoseInput &i) { i.helper.found = false; });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QWidget *row = f.page->findChild<QWidget *>("check-model");
        QVERIFY(row);
        QCOMPARE(row->property("result").toString(), QStringLiteral("skipped"));
        QCOMPARE(label(f.page.get(), "sign-model")->text(), QStringLiteral("-"));
        QCOMPARE(label(f.page.get(), "sign-model")->property("role").toString(),
                 QStringLiteral("secondary"));
        QCOMPARE(label(f.page.get(), "message-model")->property("role").toString(),
                 QStringLiteral("secondary"));
    }

    void commandIsCopied()
    {
        Fixture f([](DiagnoseInput &i) { i.udisks.running = false; });
        f.start();
        QTRY_VERIFY(f.settled(1));
        auto *field = f.page->findChild<QPlainTextEdit *>("command-udisks");
        QVERIFY(field);
        // Selected, never changed.
        QVERIFY(field->isReadOnly());
        QApplication::clipboard()->setText(QStringLiteral("before"));
        button(f.page.get(), "copy-udisks")->click();
        QCOMPARE(QApplication::clipboard()->text(),
                 QStringLiteral("sudo systemctl enable --now udisks2"));
        QCOMPARE(button(f.page.get(), "copy-udisks")->text(), QStringLiteral("Copied"));
    }

    void checkAgainRunsTheStepAgain()
    {
        Fixture f([](DiagnoseInput &i) { i.udisks.running = false; });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QCOMPARE(f.reported->count(), 1);
        QVERIFY(!f.page->canGoOn());
        QSignalSpy canGo(f.page.get(), &DiagnosePage::canGoOnChanged);

        // Nothing is fixed: the same check fails again.
        button(f.page.get(), "checkAgain")->click();
        QTRY_COMPARE(f.reported->count(), 2);
        QTRY_VERIFY(f.settled(2));
        QCOMPARE(label(f.page.get(), "sign-udisks")->text(), QString(QChar(0x2715)));

        // The user starts the service: the next run goes on to the battery.
        f.sources.in.udisks.running = true;
        button(f.page.get(), "checkAgain")->click();
        QTRY_COMPARE(f.reported->count(), 3);
        QTRY_VERIFY(button(f.page.get(), "batteryYes") &&
                    button(f.page.get(), "batteryYes")->isEnabled());
        QCOMPARE(label(f.page.get(), "sign-udisks")->text(), QString(QChar(0x2713)));
        QVERIFY(!f.page->canGoOn());
        QCOMPARE(canGo.count(), 0);
    }

    void checkAgainWhileTheBatteryIsAsked()
    {
        Fixture f;
        f.start();
        QTRY_VERIFY(button(f.page.get(), "batteryYes") &&
                    button(f.page.get(), "batteryYes")->isEnabled());
        QCOMPARE(f.reported->count(), 1);
        // The question is asked again, after a run of the checks.
        button(f.page.get(), "checkAgain")->click();
        QTRY_COMPARE(f.stopped->count(), 1);
        QTRY_VERIFY(f.reported->count() >= 3);
        QTRY_VERIFY(button(f.page.get(), "batteryYes") &&
                    button(f.page.get(), "batteryYes")->isEnabled());
        QVERIFY(!f.page->canGoOn());
    }

    void udevRuleAction()
    {
        Fixture f([](DiagnoseInput &i) { i.udevRulePresent = false; });
        QThread *testThread = QThread::currentThread();
        std::atomic<int> calls{0};
        f.page->setUdevInstaller([&f, testThread, &calls] {
            // A thread of its own: the password window of pkexec stays open for long.
            if (QThread::currentThread() != testThread)
                ++calls;
            f.sources.in.udevRulePresent = true;
            return QString();
        });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QCOMPARE(f.page->findChild<QWidget *>("check-udev-rule")->property("result").toString(),
                 QStringLiteral("fail"));
        QPushButton *install = button(f.page.get(), "action-udev-rule");
        QVERIFY(install);
        QCOMPARE(install->text(), QStringLiteral("Install the rule"));
        // The two commands for those who prefer a terminal.
        const QString commands = commandOf(f.page.get(), "udev-rule");
        QVERIFY(commands.contains("sudo tee /etc/udev/rules.d/70-tacetos-dfu.rules"));
        QVERIFY(commands.contains("sudo udevadm control --reload-rules"));
        button(f.page.get(), "copy-udev-rule")->click();
        QCOMPARE(QApplication::clipboard()->text(), commands);

        install->click();
        // The checks run again by themselves, and go on to the battery.
        QTRY_VERIFY(f.reported->count() >= 2);
        QTRY_VERIFY(button(f.page.get(), "batteryYes") &&
                    button(f.page.get(), "batteryYes")->isEnabled());
        QCOMPARE(int(calls), 1);
        QCOMPARE(f.page->findChild<QWidget *>("check-udev-rule")->property("result").toString(),
                 QStringLiteral("pass"));
        QVERIFY(!button(f.page.get(), "action-udev-rule"));
    }

    void udevRuleRefused()
    {
        Fixture f([](DiagnoseInput &i) { i.udevRulePresent = false; });
        f.page->setUdevInstaller([] { return QStringLiteral("cancelled"); });
        f.start();
        QTRY_VERIFY(f.settled(1));
        button(f.page.get(), "action-udev-rule")->click();
        QTRY_VERIFY(label(f.page.get(), "diagnoseNote")->text().contains("Nothing was changed"));
        QCOMPARE(f.reported->count(), 1);
        QPushButton *install = button(f.page.get(), "action-udev-rule");
        QVERIFY(install);
        QTRY_COMPARE(install->text(), QStringLiteral("Install the rule"));
        QVERIFY(install->isEnabled());
    }

    // A volume left unmounted (after an eject): the check offers to mount it, in a thread of its
    // own, and the checks run again by themselves.
    void volumeMountAction()
    {
        Fixture f([](DiagnoseInput &i) {
            i.ipod.volumeReady = false;
            i.ipod.volumeMountable = true;
        });
        QThread *testThread = QThread::currentThread();
        std::atomic<int> calls{0};
        f.page->setVolumeMounter([&f, testThread, &calls] {
            if (QThread::currentThread() != testThread)
                ++calls;
            f.sources.in.ipod.volumeReady = true;
            f.sources.in.ipod.volumeMountable = false;
            return QString();
        });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QPushButton *mount = button(f.page.get(), "action-volume");
        QVERIFY(mount);
        QCOMPARE(mount->text(), QStringLiteral("Mount the volume"));
        mount->click();
        QTRY_VERIFY(f.reported->count() >= 2);
        QTRY_COMPARE(f.page->findChild<QWidget *>("check-volume")->property("result").toString(),
                     QStringLiteral("pass"));
        QCOMPARE(int(calls), 1);
        QVERIFY(!button(f.page.get(), "action-volume"));
    }

    void volumeMountFails()
    {
        Fixture f([](DiagnoseInput &i) {
            i.ipod.volumeReady = false;
            i.ipod.volumeMountable = true;
        });
        f.page->setVolumeMounter([] { return QStringLiteral("The permission was not given"); });
        f.start();
        QTRY_VERIFY(f.settled(1));
        button(f.page.get(), "action-volume")->click();
        QTRY_VERIFY(
            label(f.page.get(), "diagnoseNote")->text().contains("The permission was not given"));
        QCOMPARE(f.reported->count(), 1);
        QTRY_VERIFY(button(f.page.get(), "action-volume")->isEnabled());
    }

    // Without a mounter (or a volume that cannot be mounted), the message only.
    void noMounterNoButton()
    {
        Fixture f([](DiagnoseInput &i) {
            i.ipod.volumeReady = false;
            i.ipod.volumeMountable = true;
        });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QVERIFY(!button(f.page.get(), "action-volume"));
    }

    void noInstallerNoButtonPress()
    {
        Fixture f([](DiagnoseInput &i) { i.udevRulePresent = false; });
        f.start();
        QTRY_VERIFY(f.settled(1));
        QVERIFY(!button(f.page.get(), "action-udev-rule")->isEnabled());
    }
};

QTEST_MAIN(TestDiagnosePage)
#include "tst_diagnosepage.moc"
