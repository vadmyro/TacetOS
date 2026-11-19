// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "runner.h"
#include "wizard/donepage.h"
#include "wizard/installpage.h"
#include "wizarderrors.h"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

// One recorded event of a step: a progress line, a first write, or a wait of the user.
struct Event
{
    enum class Kind { Progress, FirstWrite, Wait };
    Kind kind = Kind::Progress;
    QString stage;
    qint64 done = 0;
    qint64 total = 0;
    WaitKind wait = WaitKind::Offer;
    QJsonObject info;
};

Event progress(const QString &stage, qint64 done = 0, qint64 total = 0)
{
    Event e;
    e.stage = stage;
    e.done = done;
    e.total = total;
    return e;
}

Event firstWrite()
{
    Event e;
    e.kind = Event::Kind::FirstWrite;
    return e;
}

Event wait(WaitKind kind, const QJsonObject &info)
{
    Event e;
    e.kind = Event::Kind::Wait;
    e.wait = kind;
    e.info = info;
    return e;
}

QJsonObject offer(const QString &which)
{
    QJsonObject info;
    info.insert(QStringLiteral("offer"), which);
    info.insert(QStringLiteral("backup"), QStringLiteral("/b/2026-11-02"));
    return info;
}

// Plays the events in order, one each time the test lets it, then ends with `code` (empty:
// done).
struct Script
{
    QList<Event> events;
    QString code;
    QString detail;
    QJsonObject facts;
    std::atomic<int> allowed{0};
    std::atomic<int> played{0};
    std::atomic<int> yes{0};
    std::atomic<int> no{0};
};

class RecordedStep : public Step
{
public:
    explicit RecordedStep(Script &script) : script_(script) {}

    StepOutcome run(StepContext &context) override
    {
        for (const Event &e : script_.events) {
            while (script_.allowed <= script_.played) {
                if (context.stopRequested())
                    return StepOutcome::cancelled();
                QThread::msleep(2);
            }
            if (e.kind == Event::Kind::Wait) {
                script_.played++;
                const UserAnswer answer = context.waitForUser(e.wait, e.info);
                if (answer.cancelled)
                    return StepOutcome::cancelled();
                (answer.yes ? script_.yes : script_.no)++;
            } else {
                if (e.kind == Event::Kind::FirstWrite)
                    context.firstWrite();
                else
                    context.progress(e.stage, e.done, e.total);
                script_.played++;
            }
        }
        // The page is read before the step ends: it ends when the test calls finish().
        while (script_.allowed < 1000) {
            if (context.stopRequested())
                return StepOutcome::cancelled();
            QThread::msleep(2);
        }
        for (auto it = script_.facts.begin(); it != script_.facts.end(); ++it)
            context.setFact(it.key(), it.value());
        if (!script_.code.isEmpty())
            return StepOutcome::failed(script_.code, script_.detail);
        return StepOutcome::done();
    }

private:
    Script &script_;
};

class DoneStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

QLabel *label(QWidget *page, const QString &name)
{
    return page->findChild<QLabel *>(name);
}

} // namespace

class TestInstallPage : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        Script script;
        qint64 clock = 0;
        QString stepName;
        int seen = 0;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<InstallPage> page;

        explicit Fixture(const QString &step, const QList<Event> &events,
                         const QString &model = QStringLiteral("ipod-video"))
            : stepName(step)
        {
            script.events = events;
            const bool classic = step == QLatin1String("classic-bootloader");
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (name == stepName)
                        return std::make_unique<RecordedStep>(script);
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<InstallPage>(runner.get());
            page->setClock([this] { return clock; });
            page->setRestartWindowMs(60 * 1000);
            page->show();
            const auto count = [this] { ++seen; };
            QObject::connect(runner.get(), &WizardRunner::stepProgress, page.get(), count);
            QObject::connect(runner.get(), &WizardRunner::stepFirstWrite, page.get(), count);
            QObject::connect(runner.get(), &WizardRunner::stepWaiting, page.get(), count);
            FlowShape shape;
            shape.kind = FlowKind::Install;
            shape.path = classic ? InstallPath::Clean : InstallPath::Keep;
            shape.family = classic ? Family::Dfu : Family::Partition;
            WizardRunner::Start s;
            s.serial = QStringLiteral("ABC123");
            s.model = model;
            s.shape = shape;
            s.packageManifest = QStringLiteral("/p/m.json");
            QCOMPARE(runner->start(s), QString());
        }

        ~Fixture()
        {
            script.allowed = 1000;
            runner->cancel();
            runner->waitFinished(5000);
        }

        // The step plays the next event, and the page has read it: the signals of the runner
        // reach the page first, then `seen`, in the thread of the test.
        void next()
        {
            const int target = seen + 1;
            script.allowed = script.played + 1;
            QTRY_VERIFY_WITH_TIMEOUT(seen >= target, 5000);
        }

        // The flow runs to its end, and the page has read every signal of it. Waiting for one
        // state of the page is not enough: a step before this one may have left it done.
        void finish()
        {
            script.allowed = 1000;
            QVERIFY(runner->waitFinished(5000));
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(page->isDone() || page->isFailed() || page->isStopped(), 5000);
        }
    };

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    // Screens section 13.4: two steps, the keys depend on the model.
    void diskModeStepsByModel()
    {
        for (const QString &old : {QStringLiteral("ipod-1g2g"), QStringLiteral("ipod-3g")}) {
            const QStringList steps = InstallPage::diskModeSteps(old);
            QCOMPARE(steps.size(), 2);
            QVERIFY(steps.at(0).contains(QStringLiteral("MENU and PLAY")));
            QVERIFY(steps.at(1).contains(QStringLiteral("PREVIOUS and NEXT")));
        }
        for (const QString &wheel : {QStringLiteral("ipod-video"), QStringLiteral("nano-2g")}) {
            const QStringList steps = InstallPage::diskModeSteps(wheel);
            QCOMPARE(steps.size(), 2);
            QVERIFY(steps.at(0).contains(QStringLiteral("MENU and the center button")));
            QVERIFY(steps.at(1).contains(QStringLiteral("center button and PLAY")));
        }
    }

    // Screens section 7.3: five tones, from the high beep to the three low ones.
    void beepTableHasFiveRows()
    {
        const auto rows = InstallPage::beepTable();
        QCOMPARE(rows.size(), 5);
        QCOMPARE(rows.at(0).first, QStringLiteral("one short high beep"));
        QCOMPARE(rows.at(4).first, QStringLiteral("three long low beeps"));
    }

    // Section 7.1: the title, the version, the bytes, the stage in words.
    void firmwareShowsProgress()
    {
        Fixture f(QStringLiteral("firmware"), {progress(QStringLiteral("unpack"), 250, 1000),
                                               progress(QStringLiteral("check"))});
        f.page->setVersion(QStringLiteral("0.2.0"));
        f.next();
        QCOMPARE(f.page->titleText(), QStringLiteral("Putting TacetOS on the iPod"));
        QCOMPARE(f.page->versionText(), QStringLiteral("0.2.0"));
        QCOMPARE(f.page->stageText(), QStringLiteral("Unpacking the firmware"));
        auto *bar = f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"));
        QCOMPARE(bar->value(), 250);
        QVERIFY(label(f.page.get(), "installWarning")->isVisibleTo(f.page.get()));
        f.next();
        QCOMPARE(f.page->stageText(), QStringLiteral("Checking the files"));
        f.finish();
        QVERIFY(f.page->isDone());
    }

    // Section 7.1: the offer of the settings, both answers.
    void firmwareOfferOfSettings()
    {
        for (bool yes : {true, false}) {
            Fixture f(QStringLiteral("firmware"), {wait(WaitKind::Offer, offer("settings"))});
            f.next();
            QVERIFY(f.page->isAsking());
            QVERIFY(label(f.page.get(), "installMessage")
                        ->text()
                        .startsWith(QStringLiteral("Put back your settings and themes from")));
            QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Put them back"));
            QCOMPARE(f.page->noButton()->text(), QStringLiteral("Start fresh"));
            (yes ? f.page->yesButton() : f.page->noButton())->click();
            f.finish();
            QCOMPARE(int(f.script.yes), yes ? 1 : 0);
            QCOMPARE(int(f.script.no), yes ? 0 : 1);
        }
    }

    // A package refused: the wrong model, a damaged file (the sha256 does not match), a package
    // that changed.
    void packageRefused_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("wrong-model") << QString(error::packageWrongModel) << "not for this iPod";
        QTest::newRow("sha256") << QString(error::packageDamaged) << "damaged or incomplete";
        QTest::newRow("changed") << QString(error::packageChanged) << "changed after";
    }

    void packageRefused()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        Fixture f(QStringLiteral("firmware"), {});
        f.script.code = code;
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(text));
        QVERIFY(!f.page->isPlainStop());
        QVERIFY(!f.page->yesButton()->isVisibleTo(f.page.get()));
    }

    // Section 7.2: the page of Disk Mode, then the wait for the iPod with the seconds left.
    void diskModeThenWait()
    {
        QJsonObject info;
        info.insert(QStringLiteral("model"), QStringLiteral("ipod-3g"));
        Fixture f(QStringLiteral("disk-mode"), {wait(WaitKind::DiskMode, info)}, "ipod-3g");
        f.next();
        QCOMPARE(f.page->titleText(), QStringLiteral("Put the iPod in Disk Mode"));
        QCOMPARE(f.page->diskModeLines().size(), 2);
        QVERIFY(f.page->diskModeLines().at(1).contains(QStringLiteral("PREVIOUS and NEXT")));
        QVERIFY(f.page->diskModeButton()->isVisibleTo(f.page.get()));
        QCOMPARE(f.page->diskModeButton()->text(), QStringLiteral("The iPod is in Disk Mode"));
        auto *drawing = label(f.page.get(), "diskModeDrawing");
        QVERIFY(!drawing->pixmap().isNull());
        f.page->diskModeButton()->click();
        QCOMPARE(f.page->titleText(), QStringLiteral("Waiting for the iPod"));
        QVERIFY(f.page->isRestarting());
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 60 s"));
        f.clock = 12 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 48 s"));
        f.finish();
        QVERIFY(f.page->isDone());
    }

    // The iPod that does not come back after the Disk Mode.
    void diskModeIpodDoesNotReturn()
    {
        Fixture f(QStringLiteral("disk-mode"), {});
        f.script.code = QString(error::ipodNotBack);
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(QStringLiteral("DFU mode")));
        QVERIFY(!f.page->yesButton()->isVisibleTo(f.page.get()));
    }

    // Section 7.2: "Installing" or "Updating", and no cancel after the first write.
    void bootloaderTitleAndWrite()
    {
        for (bool update : {false, true}) {
            Fixture f(QStringLiteral("bootloader"),
                      {progress(QStringLiteral("check")), firstWrite(),
                       progress(QStringLiteral("write"), 1, 4)});
            f.page->setUpdate(update);
            f.next();
            // The page was started before the title was set from the state: set again.
            QCOMPARE(f.page->titleText().startsWith(update ? "Updating" : "Installing"), true);
            QCOMPARE(f.page->stageText(), QStringLiteral("Checking the iPod"));
            QVERIFY(f.page->closeRefusal().isEmpty());
            f.next();
            QVERIFY(f.page->isWriting());
            QVERIFY(!f.page->closeRefusal().isEmpty());
            f.next();
            QCOMPARE(f.page->stageText(), QStringLiteral("Writing"));
            f.finish();
            QVERIFY(f.page->closeRefusal().isEmpty());
        }
    }

    // Section 7.2: `offer-retry` gives "Try again", which asks the window to resume.
    void offerRetry()
    {
        Fixture f(QStringLiteral("bootloader"), {});
        f.page->setHelperNext(QStringLiteral("offer-retry"));
        f.script.code = QStringLiteral("firmware-write-failed");
        f.finish();
        QVERIFY(f.page->isFailed());
        QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Try again"));
        QSignalSpy spy(f.page.get(), &InstallPage::retryRequested);
        f.page->yesButton()->click();
        QCOMPARE(spy.count(), 1);
    }

    // `offer-restore`: the wait of the offer, then the helper restores, then `restored`.
    void offerRestoreThenRestored()
    {
        Fixture f(QStringLiteral("bootloader"), {wait(WaitKind::Offer, offer("restore-backup"))});
        f.script.code = QString(error::bootloaderRestored);
        f.next();
        QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Write the backup back"));
        QCOMPARE(f.page->noButton()->text(), QStringLiteral("Not now"));
        f.page->yesButton()->click();
        f.finish();
        QCOMPARE(int(f.script.yes), 1);
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(QStringLiteral("written back")));
        QVERIFY(f.page->closeButton()->isVisibleTo(f.page.get()));
        QVERIFY(!f.page->folderButton()->isVisibleTo(f.page.get()));
        QSignalSpy spy(f.page.get(), &InstallPage::closeRequested);
        f.page->closeButton()->click();
        QCOMPARE(spy.count(), 1);
    }

    // `restore-failed`: the plain words, Open folder, and the Report button as the main one.
    void restoreFailed()
    {
        Fixture f(QStringLiteral("bootloader"), {});
        f.page->setHelperNext(QStringLiteral("restore-failed"));
        f.page->setBackupFolder(QStringLiteral("/b/2026-11-02"));
        QString opened;
        f.page->setFolderOpener([&opened](const QString &folder) { opened = folder; });
        f.script.code = QStringLiteral("firmware-write-failed");
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(QStringLiteral("may not start")));
        QVERIFY(f.page->messageText().contains(QStringLiteral("Disk Mode")));
        QVERIFY(f.page->messageText().contains(QStringLiteral("Discord")));
        QVERIFY(f.page->reportIsMain());
        QVERIFY(!f.page->yesButton()->isVisibleTo(f.page.get()));
        f.page->folderButton()->click();
        QCOMPARE(opened, QStringLiteral("/b/2026-11-02"));
    }

    // The offer of a half written partition after a cut, refused: "Not now".
    void offerRestoreRefused()
    {
        Fixture f(QStringLiteral("bootloader"), {wait(WaitKind::Offer, offer("restore-backup"))});
        f.next();
        f.page->noButton()->click();
        f.finish();
        QCOMPARE(int(f.script.no), 1);
    }

    // Section 7.3: the DFU guide shows the stages before; the restart page comes with `resume`.
    void classicRestartWithBeeps()
    {
        Fixture f(QStringLiteral("classic-bootloader"),
                  {progress(QStringLiteral("send"), 10, 100), progress(QStringLiteral("resume"))});
        f.next();
        QVERIFY(!f.page->isRestarting());
        QVERIFY(f.page->titleText().isEmpty());
        f.next();
        QVERIFY(f.page->isRestarting());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod restarts"));
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 60 s"));
        auto *beeps = label(f.page.get(), "restartBeeps");
        QVERIFY(beeps->isVisibleTo(f.page.get()));
        for (const auto &row : InstallPage::beepTable())
            QVERIFY(beeps->text().contains(row.first));
        f.clock = 12 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 48 s"));
        f.clock = 90 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 0 s"));
        f.finish();
        QVERIFY(f.page->isDone());
        QVERIFY(!f.page->isRestarting());
    }

    // Wizard section 9.4: the bootloader did not take, once more the first time, the causes the
    // second time.
    void classicNotTaken()
    {
        {
            Fixture f(QStringLiteral("classic-bootloader"), {});
            f.script.code = QString(error::bootloaderNotTaken);
            f.finish();
            QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Try once more"));
            QSignalSpy spy(f.page.get(), &InstallPage::retryRequested);
            f.page->yesButton()->click();
            QCOMPARE(spy.count(), 1);
        }
        {
            Fixture f(QStringLiteral("classic-bootloader"), {});
            f.page->setTakeFailures(1);
            f.script.code = QString(error::bootloaderNotTaken);
            f.finish();
            QVERIFY(f.page->messageText().contains(QStringLiteral("Common causes")));
            QVERIFY(!f.page->yesButton()->isVisibleTo(f.page.get()));
        }
    }

    // `ipod-not-back` and a three-tone error never offer to install again.
    void classicNeverOffersInstallAgain()
    {
        Fixture f(QStringLiteral("classic-bootloader"), {progress(QStringLiteral("resume"))});
        f.script.code = QString(error::ipodNotBack);
        f.next();
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(QStringLiteral("Reset the iPod")));
        QVERIFY(f.page->messageText().contains(QStringLiteral("DFU mode")));
        QVERIFY(!f.page->yesButton()->isVisibleTo(f.page.get()));
        QVERIFY(!f.page->isRestarting());
    }

    // A development build stops with `dev-build`: a plain note, never the colour of a failure.
    void devBuildIsNotAFailure()
    {
        Fixture f(QStringLiteral("classic-bootloader"), {});
        f.script.code = QStringLiteral("dev-build");
        f.finish();
        QVERIFY(f.page->isStopped());
        QVERIFY(!f.page->isFailed());
        QVERIFY(f.page->isPlainStop());
        QCOMPARE(f.page->messageText(),
                 QStringLiteral("This development build does not send anything to the iPod. "
                                "Everything up to this point was checked"));
        QCOMPARE(label(f.page.get(), "installMessage")->property("role").toString(),
                 QStringLiteral("secondary"));
    }

    // Every button has an accessible name.
    void buttonsHaveNames()
    {
        Fixture f(QStringLiteral("firmware"), {wait(WaitKind::Offer, offer("settings"))});
        f.next();
        QVERIFY(!f.page->yesButton()->accessibleName().isEmpty());
        QVERIFY(!f.page->noButton()->accessibleName().isEmpty());
        QVERIFY(!f.page->diskModeButton()->accessibleName().isEmpty());
        QVERIFY(!f.page->folderButton()->accessibleName().isEmpty());
        QVERIFY(!f.page->closeButton()->accessibleName().isEmpty());
    }
};

// The end page.
class TestDonePage : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        Script verify;
        QJsonObject finishFacts;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<DonePage> page;

        Fixture(bool classic, const QString &model, const QString &name, bool clean,
                const QList<Event> &events = {}, bool hold = false)
        {
            verify.events = events;
            verify.allowed = hold ? 0 : 1000;
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &step) -> std::unique_ptr<Step> {
                    if (step == QLatin1String("verify"))
                        return std::make_unique<RecordedStep>(verify);
                    if (step == QLatin1String("finish")) {
                        class Finish : public Step
                        {
                        public:
                            explicit Finish(const QJsonObject &facts) : facts_(facts) {}
                            StepOutcome run(StepContext &context) override
                            {
                                for (auto it = facts_.begin(); it != facts_.end(); ++it)
                                    context.setFact(it.key(), it.value());
                                return StepOutcome::done();
                            }

                        private:
                            QJsonObject facts_;
                        };
                        return std::make_unique<Finish>(finishFacts);
                    }
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<DonePage>(runner.get());
            page->setIpod(model, name);
            page->setClassic(classic);
            page->setCleanInstall(clean);
            page->setBackup(QStringLiteral("/b/2026-11-02"), 1234, qint64(71) * 1000 * 1000 * 1000);
            page->show();
            FlowShape shape;
            shape.kind = FlowKind::Install;
            shape.path = clean ? InstallPath::Clean : InstallPath::Keep;
            shape.family = classic ? Family::Dfu : Family::Partition;
            WizardRunner::Start s;
            s.serial = QStringLiteral("ABC123");
            s.model = model;
            s.shape = shape;
            s.packageManifest = QStringLiteral("/p/m.json");
            QCOMPARE(runner->start(s), QString());
        }

        ~Fixture()
        {
            verify.allowed = 1000;
            runner->cancel();
            runner->waitFinished(5000);
        }
    };

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    // Section 8: "Checking the result" while `verify` runs, then the end page of the Classic.
    void classicEndPage()
    {
        Fixture f(true, QStringLiteral("classic-6g"), QStringLiteral("iPod Classic 120 GB"), true,
                  {progress(QStringLiteral("check"))}, true);
        f.finishFacts.insert(QStringLiteral("version"), QStringLiteral("0.2.0"));
        f.finishFacts.insert(QStringLiteral("backup"), QStringLiteral("/b/2026-11-02"));
        QTRY_VERIFY(f.page->isChecking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Checking the result"));
        f.verify.allowed = 1000;
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
        QCOMPARE(f.page->titleText(), QStringLiteral("TacetOS is installed"));
        QCOMPARE(f.page->subtitleText(), QStringLiteral("TacetOS 0.2.0 on iPod Classic 120 GB."));
        QVERIFY(f.page->startingText().contains(QStringLiteral("it starts by itself")));
        QVERIFY(f.page->startingText().contains(QStringLiteral("holding MENU for a second")));
        QVERIFY(f.page->startingText().contains(QStringLiteral("then let go at once")));
        QCOMPARE(f.page->backupText(), QStringLiteral("Backup checked: 1234 files, 71.0 GB"));
        QVERIFY(f.page->musicButton()->isVisibleTo(f.page.get()));
        QVERIFY(f.page->ejectButton()->isVisibleTo(f.page.get()));
        QCOMPARE(f.page->ejectNote(), QStringLiteral("Eject the iPod before you unplug it."));
        QVERIFY(!f.page->isChecking());
    }

    // An older iPod: its lines, or no section; "Put my music back" only after a clean install.
    void legacyEndPage()
    {
        {
            Fixture f(false, QStringLiteral("ipod-video"), QStringLiteral("iPod Video"), false);
            f.page->setStartLines({QStringLiteral("Hold the center button at start.")});
            QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
            QVERIFY(f.page->startingText().contains(QStringLiteral("Hold the center button")));
            QVERIFY(!f.page->musicButton()->isVisibleTo(f.page.get()));
        }
        {
            Fixture f(false, QStringLiteral("ipod-video"), QStringLiteral("iPod Video"), false);
            QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
            auto *starting = label(f.page.get(), "doneStarting");
            QVERIFY(!starting->isVisibleTo(f.page.get()));
        }
    }

    // A difference found by `verify` is a failure page, with what differs.
    void verifyFailed()
    {
        Fixture f(true, QStringLiteral("classic-6g"), QStringLiteral("iPod Classic"), true, {},
                  true);
        f.verify.code = QString(error::verifyFailed);
        f.verify.detail = QStringLiteral("the iPod does not run TacetOS");
        f.verify.allowed = 1000;
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFailed(), 5000);
        QVERIFY(f.page->failureText().contains(QStringLiteral("does not run TacetOS")));
        QVERIFY(!f.page->isFinished());
    }

    // Eject runs the eject, then the note says the iPod can be unplugged; a refused eject says so.
    void ejectThenUnplug()
    {
        Fixture f(true, QStringLiteral("classic-6g"), QStringLiteral("iPod Classic"), false);
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
        int calls = 0;
        bool works = false;
        f.page->setEjecter([&] {
            calls++;
            return works;
        });
        f.page->ejectButton()->click();
        QCOMPARE(calls, 1);
        QVERIFY(!f.page->isEjected());
        QVERIFY(f.page->ejectNote().contains(QStringLiteral("could not be ejected")));
        works = true;
        f.page->ejectButton()->click();
        QCOMPARE(calls, 2);
        QVERIFY(f.page->isEjected());
        QCOMPARE(f.page->ejectNote(), QStringLiteral("You can unplug the iPod"));
        QVERIFY(!f.page->ejectButton()->isEnabled());
    }

    // "Put my music back" opens the restore with this backup; Close and Open folder work.
    void musicBackAndClose()
    {
        Fixture f(true, QStringLiteral("classic-6g"), QStringLiteral("iPod Classic"), true);
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
        QSignalSpy music(f.page.get(), &DonePage::putMusicBackRequested);
        f.page->musicButton()->click();
        QCOMPARE(music.count(), 1);
        QCOMPARE(music.at(0).at(0).toString(), QStringLiteral("/b/2026-11-02"));
        QString opened;
        f.page->setFolderOpener([&opened](const QString &folder) { opened = folder; });
        f.page->folderButton()->click();
        QCOMPARE(opened, QStringLiteral("/b/2026-11-02"));
        QSignalSpy close(f.page.get(), &DonePage::closeRequested);
        f.page->closeButton()->click();
        QCOMPARE(close.count(), 1);
        for (QPushButton *b : f.page->findChildren<QPushButton *>())
            QVERIFY(!b->accessibleName().isEmpty());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TestInstallPage install;
    TestDonePage done;
    return QTest::qExec(&install, argc, argv) | QTest::qExec(&done, argc, argv);
}

#include "tst_installpage.moc"
