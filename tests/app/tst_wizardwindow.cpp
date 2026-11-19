// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/homescreen.h"
#include "home/ipodcard.h"
#include "models.h"
#include "runner.h"
#include "wizard/wizardmanager.h"
#include "wizard/wizardwindow.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <functional>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

// What a step of the test does, by name of step.
using Script = std::function<StepOutcome(StepContext &)>;

class ScriptStep : public Step
{
public:
    explicit ScriptStep(Script script) : m_script(std::move(script)) {}
    StepOutcome run(StepContext &context) override
    {
        return m_script ? m_script(context) : StepOutcome::done();
    }

private:
    Script m_script;
};

QPushButton *buttonNamed(QWidget *parent, const char *name)
{
    return parent->findChild<QPushButton *>(QString::fromLatin1(name));
}

} // namespace

class TestWizardWindow : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QHash<QString, Script> m_scripts;

    StepFactory factory()
    {
        return [this](const QString &name) {
            return std::make_unique<ScriptStep>(m_scripts.value(name));
        };
    }

    WizardTarget target(Family family = Family::Partition) const
    {
        WizardTarget t;
        t.serial = QStringLiteral("SER1");
        t.model = QStringLiteral("video");
        t.name = QStringLiteral("iPod Video");
        t.bootloaderTarget = QStringLiteral("ipodvideo");
        t.family = family;
        t.volumeUsed = qint64(10) << 30;
        t.keepBytes = qint64(1) << 20;
        return t;
    }

    WizardOptions options(bool realWrites = false) const
    {
        WizardOptions o;
        o.dataDir = m_dir.path();
        o.backupRoot = m_dir.filePath(QStringLiteral("backups"));
        o.realWrites = realWrites;
        o.freeBytes = [](const QString &) { return qint64(500) << 30; };
        return o;
    }

private slots:
    void init() { m_scripts.clear(); }

    void devBanner()
    {
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow dev(target(), options(false), &runner, WizardEntry::Install);
        QVERIFY(dev.findChild<QWidget *>("devBuildBanner")->isVisibleTo(&dev));
        QCOMPARE(dev.objectName(), QStringLiteral("wizardWindow"));
        dev.setRealWrites(true);
        QVERIFY(!dev.findChild<QWidget *>("devBuildBanner")->isVisibleTo(&dev));
        WizardWindow real(target(), options(true), &runner, WizardEntry::Install);
        QVERIFY(!real.findChild<QWidget *>("devBuildBanner")->isVisibleTo(&real));
    }

    void firstScreenPaths()
    {
        WizardRunner runner(m_dir.path(), factory());
        {
            WizardWindow w(target(), options(), &runner, WizardEntry::Install);
            QCOMPARE(w.pageName(), QStringLiteral("install"));
            QCOMPARE(w.path(), InstallPath::Clean);
            QVERIFY(w.findChild<QRadioButton *>("pathClean")->isChecked());
            QVERIFY(w.findChild<QLabel *>("planSummary")->text().contains(QLatin1String("checks")));
            const QStringList clean = w.stepLabels();
            w.setPath(InstallPath::Keep);
            QVERIFY(w.stepLabels() != clean);
            // No package: Start stays off.
            QVERIFY(!buttonNamed(&w, "wizardMain")->isEnabled());
            w.setPackage(m_dir.filePath(QStringLiteral("none.json")));
            QVERIFY(!buttonNamed(&w, "wizardMain")->isEnabled());
            QVERIFY(w.findChild<QLabel *>("packageWhy")->isVisibleTo(&w));
        }
        {
            // Not enough room for the clean install: Keep my music is chosen.
            WizardOptions o = options();
            o.freeBytes = [](const QString &) { return qint64(2) << 30; };
            WizardWindow w(target(), o, &runner, WizardEntry::Install);
            QVERIFY(!w.findChild<QRadioButton *>("pathClean")->isEnabled());
            QCOMPARE(w.path(), InstallPath::Keep);
            QVERIFY(w.findChild<QRadioButton *>("pathKeep")->isChecked());
        }
        {
            // A Mac iPod can only take the clean install.
            WizardTarget t = target();
            t.macFormat = true;
            WizardWindow w(t, options(), &runner, WizardEntry::Install);
            QVERIFY(!w.findChild<QRadioButton *>("pathKeep")->isEnabled());
            QCOMPARE(w.path(), InstallPath::Clean);
        }
    }

    void navigationToTheEnd()
    {
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(), options(), &runner, WizardEntry::Uninstall);
        QCOMPARE(w.pageName(), QStringLiteral("uninstall"));
        QVERIFY(buttonNamed(&w, "wizardMain")->isEnabled());
        QVERIFY(!buttonNamed(&w, "wizardReport")->isVisibleTo(&w));
        for (int i = 0; i < w.stepLabels().size(); ++i)
            QCOMPARE(w.stepState(i), QStringLiteral("todo"));
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QCOMPARE(w.pageName(), QStringLiteral("step"));
        for (int i = 0; i < w.stepLabels().size(); ++i)
            QCOMPARE(w.stepState(i), QStringLiteral("done"));
        QCOMPARE(buttonNamed(&w, "wizardMain")->text(), QStringLiteral("Close"));
        QVERIFY(!w.isBusy());
    }

    void offerAndCancel()
    {
        std::atomic<int> answers{0};
        m_scripts.insert(QString(step::diagnose), [&](StepContext &c) {
            const UserAnswer a = c.waitForUser(WaitKind::Offer);
            if (a.cancelled)
                return StepOutcome::cancelled();
            ++answers;
            return StepOutcome::done();
        });
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(), options(), &runner, WizardEntry::Uninstall);
        bool asked = false;
        w.setAsker([&](const QString &, const QString &) {
            asked = true;
            return true;
        });
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(w.pageName(), QStringLiteral("offer"));
        QCOMPARE(w.stepState(0), QStringLiteral("waiting"));
        QVERIFY(buttonNamed(&w, "wizardCancel")->isEnabled());
        QTest::mouseClick(buttonNamed(&w, "offerYes"), Qt::LeftButton);
        QTRY_COMPARE(answers.load(), 1);
        QVERIFY(runner.waitFinished(5000));
        QVERIFY(!asked);

        // A second flow: Cancel asks, then stops the flow.
        QTRY_VERIFY(!w.isBusy());
        WizardRunner runner2(m_dir.path(), factory());
        WizardWindow w2(target(), options(), &runner2, WizardEntry::Uninstall);
        w2.setAsker([&](const QString &, const QString &) {
            asked = true;
            return true;
        });
        QTest::mouseClick(buttonNamed(&w2, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(w2.pageName(), QStringLiteral("offer"));
        QTest::mouseClick(buttonNamed(&w2, "wizardCancel"), Qt::LeftButton);
        QVERIFY(asked);
        QTRY_VERIFY(!w2.isBusy());
        QCOMPARE(w2.pageName(), QStringLiteral("step"));
        QCOMPARE(w2.findChild<QLabel *>("stepText")->text().left(7), QStringLiteral("Stopped"));
        QCOMPARE(buttonNamed(&w2, "wizardMain")->text(), QStringLiteral("Close"));
    }

    void firstWriteBlocksCancelAndClose()
    {
        std::atomic<bool> release{false};
        m_scripts.insert(QString(step::backupSettings), [&](StepContext &c) {
            c.firstWrite();
            while (!release && !c.stopRequested())
                QThread::msleep(5);
            return StepOutcome::done();
        });
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(), options(), &runner, WizardEntry::Uninstall);
        QStringList told;
        w.setNotifier([&](const QString &text) { told.append(text); });
        w.show();
        QSignalSpy wrote(&runner, &WizardRunner::stepFirstWrite);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(wrote.count(), 1);
        QVERIFY(w.isWriting());
        QVERIFY(!buttonNamed(&w, "wizardCancel")->isEnabled());
        QVERIFY(!buttonNamed(&w, "wizardCancel")->toolTip().isEmpty());
        QVERIFY(!w.quitRefusal().isEmpty());
        w.close();
        QVERIFY(w.isVisible());
        QCOMPARE(told.size(), 1);
        release = true;
        QVERIFY(runner.waitFinished(5000));
        QTRY_VERIFY(!w.isBusy());
        QVERIFY(w.quitRefusal().isEmpty());
        w.close();
        QVERIFY(!w.isVisible());
    }

    void resumeProposedAndOffered()
    {
        m_scripts.insert(QString(step::backupSettings),
                         [](StepContext &) { return StepOutcome::failed(QStringLiteral("boom")); });
        {
            WizardRunner first(m_dir.path(), factory());
            WizardWindow w(target(), options(), &first, WizardEntry::Uninstall);
            QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
            QVERIFY(first.waitFinished(5000));
            QTRY_VERIFY(!w.isBusy());
        }
        WizardManager manager(m_dir.path(), nullptr);
        QVERIFY(manager.hasProgress(QStringLiteral("SER1")));
        QSignalSpy proposed(&manager, &WizardManager::resumeProposed);
        QCOMPARE(manager.proposeResume({QStringLiteral("SER1"), QStringLiteral("OTHER")}),
                 QStringList{QStringLiteral("SER1")});
        QCOMPARE(proposed.count(), 1);

        m_scripts.clear();
        WizardRunner second(m_dir.path(), factory());
        WizardWindow w(target(), options(), &second, WizardEntry::Resume);
        QCOMPARE(w.pageName(), QStringLiteral("resume"));
        QVERIFY(w.findChild<QLabel *>("resumeTitle")->text().contains(QLatin1String("Backup")));
        QVERIFY(manager.adopt(&w));
        QVERIFY(!manager.adopt(&w));
        QCOMPARE(manager.windowFor(QStringLiteral("SER1")), &w);

        // Start again: the question, then the file goes.
        w.setAsker([](const QString &, const QString &) { return false; });
        QTest::mouseClick(buttonNamed(&w, "startAgain"), Qt::LeftButton);
        QCOMPARE(w.pageName(), QStringLiteral("resume"));
        QVERIFY(manager.hasProgress(QStringLiteral("SER1")));
        w.setAsker([](const QString &, const QString &) { return true; });
        QTest::mouseClick(buttonNamed(&w, "startAgain"), Qt::LeftButton);
        QCOMPARE(w.pageName(), QStringLiteral("install"));
        QVERIFY(!manager.hasProgress(QStringLiteral("SER1")));
    }

    void resumeRuns()
    {
        m_scripts.insert(QString(step::backupSettings),
                         [](StepContext &) { return StepOutcome::failed(QStringLiteral("boom")); });
        {
            WizardRunner first(m_dir.path(), factory());
            first.start(
                {QStringLiteral("SER1"), QStringLiteral("video"),
                 FlowShape{Family::Partition, FlowKind::Uninstall, InstallPath::None, false},
                 QString(), QString()});
            QVERIFY(first.waitFinished(5000));
        }
        m_scripts.clear();
        WizardRunner second(m_dir.path(), factory());
        WizardWindow w(target(), options(), &second, WizardEntry::Resume);
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "resumeFlow"), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(!QFile::exists(ProgressStore(m_dir.path()).filePath(QStringLiteral("SER1"))));
    }

    void pausedProgramsAtStart()
    {
        QStringList sent;
        WizardManager manager(m_dir.path(), [&](const QString &cmd, const QJsonObject &) {
            sent.append(cmd);
            return HelperAnswer();
        });
        // No state file: nothing is sent.
        QVERIFY(!manager.resumePausedAtStart());
        QVERIFY(sent.isEmpty());
        QFile f(m_dir.filePath(QStringLiteral("paused-programs.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QVERIFY(manager.resumePausedAtStart());
        QCOMPARE(sent, QStringList{QStringLiteral("resume-paused")});
    }

    void cardButtons()
    {
        device::Model classic;
        classic.id = QStringLiteral("classic-6g");
        classic.family = QStringLiteral("classic");
        device::Model video;
        video.id = QStringLiteral("video");
        video.family = QStringLiteral("video");

        auto entry = [](const QString &model, device::FirmwareKind kind) {
            device::ScreenEntry e;
            e.portPath = QStringLiteral("1-2");
            e.state.kind = device::ScreenKind::Ready;
            device::DetectedIpod ipod;
            ipod.serial = QStringLiteral("SER1");
            ipod.model = model;
            ipod.state = device::IpodState::Ready;
            ipod.firmwareLine.kind = kind;
            e.ipod = ipod;
            return e;
        };
        IpodCard card;
        card.show();
        card.setEntry(entry(QStringLiteral("video"), device::FirmwareKind::Apple),
                      {video, classic});
        QVERIFY(card.wizardButton()->isVisibleTo(&card));
        QCOMPARE(card.wizardButton()->text(), QStringLiteral("Install TacetOS"));
        QVERIFY(!card.uninstallButton()->isVisibleTo(&card));
        card.setEntry(entry(QStringLiteral("classic-6g"), device::FirmwareKind::Apple),
                      {video, classic});
        QVERIFY(card.uninstallButton()->isVisibleTo(&card));
        card.setEntry(entry(QStringLiteral("video"), device::FirmwareKind::Tacetos),
                      {video, classic});
        QCOMPARE(card.wizardButton()->text(), QStringLiteral("Uninstall"));
        QSignalSpy asked(&card, &IpodCard::actionRequested);
        card.setWizardState(true, false);
        QCOMPARE(card.wizardButton()->text(), QStringLiteral("Resume"));
        QTest::mouseClick(card.wizardButton(), Qt::LeftButton);
        QCOMPARE(asked.takeFirst().at(0).value<HomeAction>(), HomeAction::ResumeWizard);
        card.setWizardState(true, true);
        QCOMPARE(card.wizardButton()->text(), QStringLiteral("Show the wizard"));
        QVERIFY(!card.openButton()->isVisibleTo(&card));
        QTest::mouseClick(card.wizardButton(), Qt::LeftButton);
        QCOMPARE(asked.takeFirst().at(0).value<HomeAction>(), HomeAction::ShowWizard);
    }
};

QTEST_MAIN(TestWizardWindow)
#include "tst_wizardwindow.moc"
