// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "runner.h"
#include "wizard/uninstallpage.h"
#include "wizarderrors.h"

#include <QApplication>
#include <QDir>
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

bool shown(QWidget *page, QWidget *w)
{
    return w->isVisibleTo(page);
}

} // namespace

class TestUninstallPage : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        Script script;
        qint64 clock = 0;
        QString stepName;
        bool classic;
        int seen = 0;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<UninstallPage> page;

        explicit Fixture(const QString &step, const QList<Event> &events, bool dfu = false,
                         const QString &model = QStringLiteral("ipod-video"))
            : stepName(step), classic(dfu), modelName(model)
        {
            script.events = events;
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (name == stepName)
                        return std::make_unique<RecordedStep>(script);
                    return std::make_unique<DoneStep>();
                });
            runner->setAppVersion(QStringLiteral("0.0.1"));
            page = std::make_unique<UninstallPage>(runner.get());
            page->setClock([this] { return clock; });
            page->setRestartWindowMs(60 * 1000);
            page->setIpod(modelName, QStringLiteral("iPod Test 80 GB"));
            page->show();
            const auto count = [this] { ++seen; };
            QObject::connect(runner.get(), &WizardRunner::stepProgress, page.get(), count);
            QObject::connect(runner.get(), &WizardRunner::stepFirstWrite, page.get(), count);
            QObject::connect(runner.get(), &WizardRunner::stepWaiting, page.get(), count);
        }

        ~Fixture()
        {
            script.allowed = 1000;
            runner->cancel();
            runner->waitFinished(5000);
        }

        void start()
        {
            FlowShape shape;
            shape.kind = FlowKind::Uninstall;
            shape.path = InstallPath::None;
            shape.family = classic ? Family::Dfu : Family::Partition;
            WizardRunner::Start s;
            s.serial = QStringLiteral("ABC123");
            s.model = modelName;
            s.shape = shape;
            QCOMPARE(runner->start(s), QString());
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

    private:
        QString modelName;
    };

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    // Section 9, the first screen: what will happen, the folder, the button Uninstall.
    void firstScreenConfirms()
    {
        Fixture f(QStringLiteral("remove-firmware"), {});
        f.page->setBackupFolder(QDir::homePath() + QStringLiteral("/Documents/TacetOS Backups"));
        QCOMPARE(f.page->firstKind(), UninstallPage::First::Confirm);
        QCOMPARE(f.page->titleText(), QStringLiteral("Back to Apple's software only"));
        auto *text = label(f.page.get(), "uninstallFirstText");
        QVERIFY(text->text().contains(QStringLiteral("TacetOS and its bootloader are removed")));
        QVERIFY(text->text().contains(QStringLiteral("Your music stays on the iPod")));
        QCOMPARE(f.page->folderText(),
                 QStringLiteral("Backup folder: ~/Documents/TacetOS Backups"));
        QCOMPARE(f.page->startButton()->text(), QStringLiteral("Uninstall"));
        QCOMPARE(f.page->startButton()->property("role").toString(), QStringLiteral("main"));
        QVERIFY(shown(f.page.get(), f.page->startButton()));

        QSignalSpy started(f.page.get(), &UninstallPage::startRequested);
        f.page->startButton()->click();
        f.page->startButton()->click();
        QCOMPARE(started.count(), 1);
        QVERIFY(f.page->isStarted());
        QVERIFY(!f.page->startButton()->isEnabled());

        QSignalSpy change(f.page.get(), &UninstallPage::changeFolderRequested);
        f.page->changeFolderButton()->click();
        QCOMPARE(change.count(), 0);
    }

    void changeFolderIsAskedToTheWindow()
    {
        Fixture f(QStringLiteral("remove-firmware"), {});
        QSignalSpy change(f.page.get(), &UninstallPage::changeFolderRequested);
        f.page->changeFolderButton()->click();
        QCOMPARE(change.count(), 1);
    }

    // Section 9: an older iPod with nothing installed has no button but Close.
    void firstScreenNothingToDo()
    {
        Fixture f(QStringLiteral("remove-firmware"), {});
        f.page->showFirst(UninstallPage::First::NothingToDo);
        auto *text = label(f.page.get(), "uninstallFirstText");
        QCOMPARE(text->text(),
                 QStringLiteral("This iPod has no TacetOS. There is nothing to remove"));
        QVERIFY(!shown(f.page.get(), f.page->startButton()));
        auto *close = f.page->findChild<QPushButton *>(QStringLiteral("uninstallFirstClose"));
        QVERIFY(shown(f.page.get(), close));
        QSignalSpy closed(f.page.get(), &UninstallPage::closeRequested);
        close->click();
        QCOMPARE(closed.count(), 1);
    }

    // Section 9: a Classic with no trace says "Remove anyway".
    void firstScreenRemoveAnyway()
    {
        Fixture f(QStringLiteral("classic-remove"), {}, true);
        f.page->showFirst(UninstallPage::First::RemoveAnyway);
        auto *text = label(f.page.get(), "uninstallFirstText");
        QCOMPARE(text->text(),
                 QStringLiteral("TacetOS does not seem to be on this iPod. Remove it anyway?"));
        QCOMPARE(f.page->startButton()->text(), QStringLiteral("Remove anyway"));
        QVERIFY(shown(f.page.get(), f.page->startButton()));
    }

    // The offer of the step, once "Remove anyway" was said on the first screen, is answered yes
    // without a second question.
    void removeAnywayIsNotAskedTwice()
    {
        Fixture f(QStringLiteral("classic-remove"), {wait(WaitKind::Offer, offer("remove-anyway"))},
                  true);
        f.page->showFirst(UninstallPage::First::RemoveAnyway);
        f.page->startButton()->click();
        f.start();
        f.next();
        QTRY_COMPARE(int(f.script.yes), 1);
        QVERIFY(!f.page->isAsking());
        QVERIFY(!shown(f.page.get(), f.page->yesButton()));
        f.finish();
    }

    // The same offer on a resume (no first screen): the page asks, both answers.
    void removeAnywayOfferBothAnswers()
    {
        for (bool yes : {true, false}) {
            Fixture f(QStringLiteral("classic-remove"),
                      {wait(WaitKind::Offer, offer("remove-anyway"))}, true);
            f.start();
            f.next();
            QVERIFY(f.page->isAsking());
            QVERIFY(f.page->messageText().contains(QStringLiteral("Remove it anyway?")));
            QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Remove anyway"));
            QCOMPARE(f.page->noButton()->text(), QStringLiteral("Not now"));
            QCOMPARE(f.page->yesButton()->property("role").toString(), QStringLiteral("main"));
            (yes ? f.page->yesButton() : f.page->noButton())->click();
            f.finish();
            QCOMPARE(int(f.script.yes), yes ? 1 : 0);
            QCOMPARE(int(f.script.no), yes ? 0 : 1);
            if (!yes)
                QVERIFY(f.page->isStopped() || f.page->isFailed() || f.page->isDone());
        }
    }

    // Older iPods: "Removing the bootloader", the stages of the helper, the first write.
    void removeBootloaderOnOlderIpods_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("video") << "ipod-video";
        QTest::newRow("nano2g") << "nano-2g";
    }

    void removeBootloaderOnOlderIpods()
    {
        QFETCH(QString, model);
        Fixture f(QStringLiteral("remove-bootloader"),
                  {progress(QStringLiteral("check"), 0, 0),
                   progress(QStringLiteral("write"), 250, 1000), firstWrite(),
                   progress(QStringLiteral("verify"), 900, 1000)},
                  false, model);
        f.start();
        f.next();
        QCOMPARE(f.page->titleText(), QStringLiteral("Removing the bootloader"));
        QCOMPARE(f.page->stageText(), QStringLiteral("Checking the iPod"));
        QVERIFY(shown(f.page.get(), label(f.page.get(), "uninstallWarning")));
        QVERIFY(f.page->closeRefusal().isEmpty());
        f.next();
        auto *bar = f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"));
        QCOMPARE(bar->value(), 250);
        QCOMPARE(f.page->stageText(), QStringLiteral("Writing"));
        f.next();
        QVERIFY(f.page->isWriting());
        QVERIFY(f.page->closeRefusal().contains(QStringLiteral("writing to the iPod")));
        f.next();
        QCOMPARE(f.page->stageText(), QStringLiteral("Reading back and checking"));
        f.finish();
        QVERIFY(f.page->isDone());
        QVERIFY(!f.page->isWriting());
        QCOMPARE(bar->value(), 1000);
    }

    // "Removing the TacetOS files": a bar with no end, then done.
    void removeFirmwareFiles()
    {
        Fixture f(QStringLiteral("remove-firmware"), {progress(QStringLiteral("x"))});
        f.start();
        f.next();
        QCOMPARE(f.page->titleText(), QStringLiteral("Removing the TacetOS files"));
        auto *bar = f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"));
        QCOMPARE(bar->maximum(), 0);
        f.finish();
        QVERIFY(f.page->isDone());
    }

    // A step that is not the page's (the backup, the DFU guide's wait) leaves it as it is.
    void otherStepsAreIgnored()
    {
        Fixture f(QStringLiteral("backup-settings"), {progress(QStringLiteral("copy"), 1, 2)});
        f.start();
        f.next();
        QCOMPARE(f.page->titleText(), QStringLiteral("Back to Apple's software only"));
        QVERIFY(!f.page->isActive());
        QVERIFY(shown(f.page.get(), label(f.page.get(), "uninstallFirstText")));
    }

    // Section 9, the Classic: the restart with the beeps and the seconds left; Apple's firmware
    // is the success.
    void classicRestart()
    {
        Fixture f(QStringLiteral("classic-remove"),
                  {progress(QStringLiteral("send"), 10, 100), progress(QStringLiteral("resume"))},
                  true, QStringLiteral("classic-6g"));
        f.start();
        f.next();
        QVERIFY(!f.page->isRestarting());
        f.next();
        QVERIFY(f.page->isRestarting());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod restarts"));
        QVERIFY(label(f.page.get(), "restartListen")
                    ->text()
                    .contains(QStringLiteral("Apple's firmware")));
        QVERIFY(label(f.page.get(), "restartBeeps")
                    ->text()
                    .contains(QStringLiteral("three long low beeps")));
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 60 s"));
        f.clock = 12 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(f.page->countdownText(), QStringLiteral("Waiting for the iPod: 48 s"));
        f.finish();
        QVERIFY(f.page->isDone());
        QVERIFY(!f.page->isRestarting());
    }

    // Section 9: `still-tacetos` offers "Try again" once.
    void stillTacetosOffersOneRetry()
    {
        Fixture f(QStringLiteral("classic-remove"), {}, true);
        f.script.code = QString(error::stillTacetos);
        f.start();
        f.finish();
        QVERIFY(f.page->isFailed());
        QCOMPARE(f.page->stillFailures(), 1);
        QCOMPARE(f.page->messageText(),
                 QStringLiteral("The iPod still starts TacetOS. Try again."));
        QVERIFY(!f.page->isPlainStop());
        QVERIFY(f.page->isAsking());
        QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Try again"));
        QCOMPARE(f.page->yesButton()->property("role").toString(), QStringLiteral("main"));
        QVERIFY(!shown(f.page.get(), f.page->noButton()));
        QSignalSpy retry(f.page.get(), &UninstallPage::retryRequested);
        f.page->yesButton()->click();
        QCOMPARE(retry.count(), 1);
        QVERIFY(!f.page->isAsking());
    }

    // The second time, no more "Try again": the plain words and Close.
    void stillTacetosTwiceHasNoRetry()
    {
        Fixture f(QStringLiteral("classic-remove"), {}, true, QStringLiteral("classic-6g"));
        f.script.code = QString(error::stillTacetos);
        f.start();
        f.finish();
        QVERIFY(f.page->isAsking());
        f.page->yesButton()->click();
        f.script.allowed = 1000;
        // The window resumes the flow: the step fails the same way again.
        f.runner->waitFinished(5000);
        f.start();
        QTRY_COMPARE_WITH_TIMEOUT(f.page->stillFailures(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFailed() && !f.page->isAsking(), 5000);
        QVERIFY(f.page->messageText().contains(QStringLiteral("after a second try")));
        QVERIFY(!shown(f.page.get(), f.page->yesButton()));
        QSignalSpy closed(f.page.get(), &UninstallPage::closeRequested);
        auto *close = f.page->findChild<QPushButton *>(QStringLiteral("uninstallClose"));
        QVERIFY(shown(f.page.get(), close));
        close->click();
        QCOMPARE(closed.count(), 1);
    }

    // The offer of the helper after a failed removal on an older iPod.
    void removeBootloaderOfferRetry()
    {
        Fixture f(QStringLiteral("remove-bootloader"), {});
        f.script.code = QStringLiteral("write-failed");
        f.page->setHelperNext(QStringLiteral("offer-retry"));
        f.start();
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->isAsking());
        QCOMPARE(f.page->yesButton()->text(), QStringLiteral("Try again"));
    }

    // Codes of these steps: the message, in `danger`, and no offer.
    void failureMessages_data()
    {
        QTest::addColumn<QString>("step");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("backup") << "remove-bootloader" << QString(error::backupNotVerified)
                                << "No verified backup";
        QTest::newRow("gone") << "remove-bootloader" << QString(error::ipodGone) << "unplugged";
        QTest::newRow("verify") << "remove-bootloader" << QString(error::verifyFailed)
                                << "not in the state it should be";
        QTest::newRow("volume") << "remove-firmware" << QString(error::volumeNotBack)
                                << "volume did not come back";
        QTest::newRow("erase") << "remove-firmware" << QString(error::eraseFailed)
                               << "could not be removed";
        QTest::newRow("not-back") << "classic-remove" << QString(error::ipodNotBack)
                                  << "Reset the iPod";
        QTest::newRow("dfu") << "classic-remove" << QString(error::dfuFailed) << "Reset the iPod";
    }

    void failureMessages()
    {
        QFETCH(QString, step);
        QFETCH(QString, code);
        QFETCH(QString, text);
        Fixture f(step, {}, step == QLatin1String("classic-remove"));
        f.script.code = code;
        f.start();
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(f.page->messageText().contains(text));
        QVERIFY(!f.page->isPlainStop());
        QVERIFY(!f.page->isAsking());
        QCOMPARE(label(f.page.get(), "uninstallMessage")->property("role").toString(),
                 QStringLiteral("danger"));
        QVERIFY(shown(f.page.get(), f.page->findChild<QPushButton *>("uninstallClose")));
    }

    // A development build and nothing to do are plain stops, not failures.
    void plainStops_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("dev-build") << "dev-build" << "does not send anything to the iPod";
        QTest::newRow("nothing") << QString(error::nothingToDo) << "There is nothing to remove";
    }

    void plainStops()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        Fixture f(QStringLiteral("remove-bootloader"), {});
        f.script.code = code;
        f.start();
        f.finish();
        QVERIFY(f.page->isStopped());
        QVERIFY(!f.page->isFailed());
        QVERIFY(f.page->isPlainStop());
        QVERIFY(f.page->messageText().contains(text));
        QCOMPARE(label(f.page.get(), "uninstallMessage")->property("role").toString(),
                 QStringLiteral("secondary"));
    }

    void cancelledSaysStopped()
    {
        Fixture f(QStringLiteral("remove-firmware"), {});
        f.script.code = QString(error::cancelled);
        f.start();
        f.finish();
        QVERIFY(f.page->isStopped());
        QVERIFY(f.page->messageText().startsWith(QStringLiteral("Stopped.")));
    }

    // Section 9, the end: "Checking the result", then "Apple software only" with the folder,
    // Eject and Close.
    void endOfTheUninstall()
    {
        Fixture f(QStringLiteral("verify"), {progress(QStringLiteral("x"))});
        f.page->setBackupFolder(QStringLiteral("/b/2026-11-02"));
        f.start();
        f.next();
        QVERIFY(f.page->isChecking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Checking the result"));
        auto *bar = f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"));
        QCOMPARE(bar->maximum(), 0);
        f.script.allowed = 1000;
        QTRY_VERIFY_WITH_TIMEOUT(f.page->isFinished(), 5000);
        QVERIFY(!f.page->isChecking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Apple software only"));
        QCOMPARE(f.page->subtitleText(), QStringLiteral("iPod Test 80 GB"));
        QCOMPARE(f.page->backupText(), QStringLiteral("Your settings were saved in /b/2026-11-02"));
        QVERIFY(
            !f.page->findChild<QLabel *>(QStringLiteral("uninstallDrawing"))->pixmap().isNull());

        QString opened;
        f.page->setFolderOpener([&opened](const QString &folder) { opened = folder; });
        f.page->folderButton()->click();
        QCOMPARE(opened, QStringLiteral("/b/2026-11-02"));

        QCOMPARE(f.page->ejectNote(), QStringLiteral("Eject the iPod before you unplug it."));
        f.page->setEjecter([] { return false; });
        f.page->ejectButton()->click();
        QVERIFY(!f.page->isEjected());
        QVERIFY(f.page->ejectNote().contains(QStringLiteral("could not be ejected")));
        f.page->setEjecter([] { return true; });
        f.page->ejectButton()->click();
        QVERIFY(f.page->isEjected());
        QVERIFY(!f.page->ejectButton()->isEnabled());
        QCOMPARE(f.page->ejectNote(), QStringLiteral("You can unplug the iPod"));

        QSignalSpy closed(f.page.get(), &UninstallPage::closeRequested);
        f.page->findChild<QPushButton *>(QStringLiteral("uninstallDoneClose"))->click();
        QCOMPARE(closed.count(), 1);
    }

    // The result that does not match: a failure with the code, no end page.
    void verifyFailureShowsNoEnd()
    {
        Fixture f(QStringLiteral("verify"), {});
        f.script.code = QString(error::verifyFailed);
        f.start();
        f.finish();
        QVERIFY(f.page->isFailed());
        QVERIFY(!f.page->isFinished());
        QVERIFY(!f.page->isChecking());
    }

    void everyButtonHasAName()
    {
        Fixture f(QStringLiteral("remove-firmware"), {});
        for (QPushButton *b : f.page->findChildren<QPushButton *>())
            QVERIFY2(!b->accessibleName().isEmpty(), qPrintable(b->objectName()));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TestUninstallPage test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_uninstallpage.moc"
