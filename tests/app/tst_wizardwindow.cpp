// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "../wizard/testzip.h"
#include "constants.h"
#include "home/homescreen.h"
#include "home/ipodcard.h"
#include "models.h"
#include "runner.h"
#include "wizard/backuppage.h"
#include "wizard/dfututorial.h"
#include "wizard/diagnosepage.h"
#include "wizard/donepage.h"
#include "wizard/formatpage.h"
#include "wizard/installpage.h"
#include "wizard/reportpage.h"
#include "wizard/uninstallpage.h"
#include "wizard/wizardmanager.h"
#include "wizard/wizardwindow.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
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

Check check(const char *id, Side side, CheckResult result, const QString &message = QString())
{
    Check c;
    c.id = QString::fromLatin1(id);
    c.side = side;
    c.result = result;
    c.message = message;
    return c;
}

QString sha(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// A good package of the Classic in `folder`; returns the path of its manifest.
QString writePackage(const QString &folder)
{
    QDir().mkpath(folder);
    const QByteArray folderName(device::kFirmwareFolder.data(), device::kFirmwareFolder.size());
    const QByteArray zip = tacetos::wizard::testing::storedZip(
        {{folderName + '/', QByteArray()}, {folderName + "/tacetos.ipod", QByteArray(4000, 'f')}});
    const QByteArray bootloader(2000, 'b');
    const QList<QPair<QString, QByteArray>> files = {
        {QStringLiteral("tacetos-ipod6g-0.2.0.zip"), zip},
        {QStringLiteral("tacetos-bootloader-ipod6g-0.2.0.ipod"), bootloader}};
    QJsonArray list;
    for (const auto &[name, bytes] : files) {
        QFile f(folder + QLatin1Char('/') + name);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write(bytes);
        QJsonObject o;
        o.insert(QStringLiteral("name"), name);
        o.insert(QStringLiteral("kind"), name.endsWith(QLatin1String(".zip"))
                                             ? QStringLiteral("firmware")
                                             : QStringLiteral("bootloader"));
        o.insert(QStringLiteral("size"), double(bytes.size()));
        o.insert(QStringLiteral("sha256"), sha(bytes));
        list.append(o);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), 1);
    manifest.insert(QStringLiteral("target"), QStringLiteral("ipod6g"));
    manifest.insert(QStringLiteral("version"), QStringLiteral("0.2.0+gabc1234"));
    manifest.insert(QStringLiteral("files"), list);
    const QString path = folder + QStringLiteral("/tacetos-ipod6g-0.2.0.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(QJsonDocument(manifest).toJson());
    return path;
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
        // The pages of the steps have their own offer buttons: the one of the offer page.
        QTest::mouseClick(w.findChild<QLabel *>("offerTitle")
                              ->parentWidget()
                              ->findChild<QPushButton *>("offerYes"),
                          Qt::LeftButton);
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

    // The real trial of the Classic (2026-11-20) stopped on a blocking check with an empty page:
    // the checks have their own page, which keeps the failed check, its action and Check again.
    void blockingCheckShowsOnItsPage()
    {
        std::atomic<bool> ruleThere{false};
        m_scripts.insert(QString(step::diagnose), [&](StepContext &c) {
            Check rule = check("udev-rule", Side::Computer, CheckResult::Fail,
                               QStringLiteral("TacetOS needs a USB permission"));
            rule.action = QStringLiteral("install-udev-rule");
            if (ruleThere)
                rule = check("udev-rule", Side::Computer, CheckResult::Pass);
            c.reportChecks({check("system", Side::Computer, CheckResult::Pass), rule});
            if (!ruleThere)
                return StepOutcome::failed(QString(error::checkBlocking), rule.id);
            return StepOutcome::done();
        });
        WizardRunner runner(m_dir.path(), factory());
        std::atomic<int> installs{0};
        WizardOptions o = options();
        o.udevInstaller = [&] {
            ++installs;
            ruleThere = true;
            return QString();
        };
        WizardWindow w(target(), o, &runner, WizardEntry::Uninstall);
        w.diagnosePage()->setPause(0);
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QCOMPARE(w.pageName(), QStringLiteral("diagnose"));
        auto *row = w.findChild<QWidget *>(QStringLiteral("check-udev-rule"));
        QVERIFY(row);
        QCOMPARE(row->property("result").toString(), QStringLiteral("fail"));
        QVERIFY(w.findChild<QLabel *>("stepFailure")->text().contains(QLatin1String("udev-rule")));
        QCOMPARE(buttonNamed(&w, "wizardMain")->text(), QStringLiteral("Close"));
        QPushButton *install = buttonNamed(&w, "action-udev-rule");
        QVERIFY(install);
        QVERIFY(install->isEnabled());

        // The rule goes in place, the checks run again by themselves and the flow goes on.
        QTest::mouseClick(install, Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 2);
        QCOMPARE(installs.load(), 1);
        QVERIFY(runner.waitFinished(5000));
        for (int i = 0; i < w.stepLabels().size(); ++i)
            QCOMPARE(w.stepState(i), QStringLiteral("done"));
    }

    // The question of the battery is a button on the page of the checks.
    void batteryQuestionOnTheChecks()
    {
        m_scripts.insert(QString(step::diagnose), [&](StepContext &c) {
            c.reportChecks({check("battery", Side::Ipod, CheckResult::Ask,
                                  QStringLiteral("Is the iPod charged?"))});
            const UserAnswer a = c.waitForUser(WaitKind::Battery);
            if (a.cancelled || !a.yes)
                return StepOutcome::failed(QString(error::checkBlocking),
                                           QStringLiteral("battery"));
            c.reportChecks({check("battery", Side::Ipod, CheckResult::Pass)});
            return StepOutcome::done();
        });
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(), options(), &runner, WizardEntry::Uninstall);
        // The steps after the checks end at once: with a long pause the flow always ends during
        // it, so the window stays on the step page (with no pause, the order changes with the
        // runner).
        w.diagnosePage()->setPause(60000);
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_COMPARE(w.pageName(), QStringLiteral("diagnose"));
        QTRY_VERIFY(buttonNamed(&w, "batteryYes") && buttonNamed(&w, "batteryYes")->isEnabled());
        QCOMPARE(w.stepState(0), QStringLiteral("waiting"));
        QTest::mouseClick(buttonNamed(&w, "batteryYes"), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(runner.waitFinished(5000));
        QCOMPARE(w.pageName(), QStringLiteral("step"));
        QCOMPARE(w.stepState(0), QStringLiteral("done"));
    }

    // A clean install of the Classic: the checks, then the backup, then the erase with the typed
    // name, each on its own page.
    void eachStepShowsItsPage()
    {
        QStringList seen;
        WizardWindow *window = nullptr;
        auto pageWhile = [&](const QString &name) {
            return [&, name](StepContext &) {
                QMetaObject::invokeMethod(
                    window, [&, name] { seen.append(name + '=' + window->pageName()); },
                    Qt::BlockingQueuedConnection);
                return StepOutcome::done();
            };
        };
        m_scripts.insert(QString(step::diagnose), [&](StepContext &c) {
            c.reportChecks({check("system", Side::Computer, CheckResult::Pass)});
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::backupFull), [&](StepContext &c) {
            c.setBackupFolder(m_dir.filePath(QStringLiteral("backups/SER1/2026-11-20")));
            c.progress(QStringLiteral("copy"), 10, 100);
            // The page of the checks keeps its end line for its pause (0 ms here), then the
            // page of the backup comes.
            QString page;
            for (int i = 0; i < 200 && page != QLatin1String("backup"); ++i) {
                QThread::msleep(5);
                QMetaObject::invokeMethod(
                    window, [&] { page = window->pageName(); }, Qt::BlockingQueuedConnection);
            }
            seen.append(QStringLiteral("backup=") + page);
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::format), [&](StepContext &c) {
            QJsonObject info;
            info.insert(QStringLiteral("ipodName"), QStringLiteral("IPOD"));
            const UserAnswer a = c.waitForUser(WaitKind::TypedName, info);
            if (a.cancelled)
                return StepOutcome::cancelled();
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::firmware), pageWhile(QString(step::firmware)));

        WizardTarget t = target(Family::Dfu);
        t.bootloaderTarget = QStringLiteral("ipod6g");
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(t, options(), &runner, WizardEntry::Install);
        window = &w;
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        w.setPackage(writePackage(m_dir.filePath(QStringLiteral("package"))));
        QVERIFY2(buttonNamed(&w, "wizardMain")->isEnabled(),
                 qPrintable(w.findChild<QLabel *>("packageWhy")->text()));
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);

        // The erase asks for the name on its own page.
        QTRY_COMPARE(w.pageName(), QStringLiteral("format"));
        QTRY_VERIFY(w.formatPage()->isAsking());
        auto *field = w.formatPage()->findChild<QLineEdit *>();
        QVERIFY(field);
        field->setText(QStringLiteral("IPOD"));
        QVERIFY(w.formatPage()->confirmButton()->isEnabled());
        QTest::mouseClick(w.formatPage()->confirmButton(), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(runner.waitFinished(5000));
        QCOMPARE(seen,
                 QStringList({QStringLiteral("backup=backup"), QStringLiteral("firmware=write")}));
        for (int i = 0; i < w.stepLabels().size(); ++i)
            QCOMPARE(w.stepState(i), QStringLiteral("done"));
    }

    // What a step script (on the worker thread) sees of the window.
    static QString pageAfter(WizardWindow *w, const QString &want)
    {
        QString page;
        for (int i = 0; i < 200 && page != want; ++i) {
            QMetaObject::invokeMethod(
                w, [&] { page = w->pageName(); }, Qt::BlockingQueuedConnection);
            if (page != want)
                QThread::msleep(5);
        }
        return page;
    }

    // An uninstall of an older iPod: each step and each wait shows its page.
    void uninstallStepsAndWaitsShowTheirPages()
    {
        QStringList seen;
        WizardWindow *window = nullptr;
        auto record = [&](const QString &name, const QString &want) {
            return [&, name, want](StepContext &) {
                seen.append(name + '=' + pageAfter(window, want));
                return StepOutcome::done();
            };
        };
        m_scripts.insert(QString(step::diagnose), record(QString(step::diagnose), "diagnose"));
        m_scripts.insert(QString(step::backupSettings), record("backup", "backup"));
        m_scripts.insert(QString(step::diskMode), [&](StepContext &c) {
            QJsonObject info;
            info.insert(QStringLiteral("model"), QStringLiteral("video"));
            const UserAnswer a = c.waitForUser(WaitKind::DiskMode, info);
            return a.cancelled ? StepOutcome::cancelled() : StepOutcome::done();
        });
        m_scripts.insert(QString(step::removeBootloader), [&](StepContext &c) {
            QJsonObject info;
            info.insert(QStringLiteral("offer"), QStringLiteral("remove-anyway"));
            const UserAnswer a = c.waitForUser(WaitKind::Offer, info);
            return a.cancelled ? StepOutcome::cancelled() : StepOutcome::done();
        });
        m_scripts.insert(QString(step::removeFirmware), record("remove-firmware", "remove"));
        m_scripts.insert(QString(step::verify), record("verify", "remove"));
        m_scripts.insert(QString(step::finish), record("finish", "remove"));
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(), options(), &runner, WizardEntry::Uninstall);
        window = &w;
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);

        // Disk Mode waits on the page of the install steps.
        QTRY_VERIFY(w.installPage()->isAsking());
        QCOMPARE(w.pageName(), QStringLiteral("write"));
        QTest::mouseClick(w.installPage()->diskModeButton(), Qt::LeftButton);
        // The offer of the removal waits on the page of the uninstall.
        QTRY_VERIFY(w.uninstallPage()->isAsking());
        QCOMPARE(w.pageName(), QStringLiteral("remove"));
        QTest::mouseClick(w.uninstallPage()->yesButton(), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(runner.waitFinished(5000));
        QCOMPARE(seen, QStringList({"diagnose=diagnose", "backup=backup", "remove-firmware=remove",
                                    "verify=remove", "finish=remove"}));
        QCOMPARE(w.pageName(), QStringLiteral("remove"));
    }

    // An uninstall of the Classic: the DFU guide waits for Start, then the restart is the
    // page of the uninstall.
    void classicRemoveShowsTheDfuGuide()
    {
        m_scripts.insert(QString(step::classicRemove), [&](StepContext &c) {
            const UserAnswer a = c.waitForUser(WaitKind::ReadyForDfu);
            if (a.cancelled)
                return StepOutcome::cancelled();
            c.progress(QStringLiteral("resume"), 0, 0);
            return StepOutcome::done();
        });
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(target(Family::Dfu), options(), &runner, WizardEntry::Uninstall);
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_VERIFY(w.dfuPage()->isAsking());
        QCOMPARE(w.pageName(), QStringLiteral("dfu"));
        QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(runner.waitFinished(5000));
        QCOMPARE(w.pageName(), QStringLiteral("remove"));
    }

    // An install of the Classic from the firmware on: the firmware, the DFU guide then the
    // restart, the check and the end each show their page.
    void installLastStepsShowTheirPages()
    {
        QStringList seen;
        WizardWindow *window = nullptr;
        auto record = [&](const QString &name, const QString &want) {
            return [&, name, want](StepContext &) {
                seen.append(name + '=' + pageAfter(window, want));
                return StepOutcome::done();
            };
        };
        m_scripts.insert(QString(step::diagnose), [](StepContext &c) {
            c.reportChecks({check("system", Side::Computer, CheckResult::Pass)});
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::backupFull), [&](StepContext &c) {
            c.setBackupFolder(m_dir.filePath(QStringLiteral("backups/SER1/2026-11-20")));
            c.progress(QStringLiteral("copy"), 10, 100);
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::format), [&](StepContext &c) {
            QJsonObject info;
            info.insert(QStringLiteral("ipodName"), QStringLiteral("IPOD"));
            const UserAnswer a = c.waitForUser(WaitKind::TypedName, info);
            return a.cancelled ? StepOutcome::cancelled() : StepOutcome::done();
        });
        m_scripts.insert(QString(step::firmware), record("firmware", "write"));
        m_scripts.insert(QString(step::classicBootloader), [&](StepContext &c) {
            const UserAnswer a = c.waitForUser(WaitKind::ReadyForDfu);
            if (a.cancelled)
                return StepOutcome::cancelled();
            seen.append("dfu=" + pageAfter(window, "dfu"));
            c.progress(QStringLiteral("resume"), 0, 0);
            seen.append("restart=" + pageAfter(window, "write"));
            return StepOutcome::done();
        });
        m_scripts.insert(QString(step::verify), record("verify", "done"));
        m_scripts.insert(QString(step::finish), record("finish", "done"));

        WizardTarget t = target(Family::Dfu);
        t.bootloaderTarget = QStringLiteral("ipod6g");
        WizardRunner runner(m_dir.path(), factory());
        WizardWindow w(t, options(), &runner, WizardEntry::Install);
        window = &w;
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        w.setPackage(writePackage(m_dir.filePath(QStringLiteral("package"))));
        QSignalSpy ended(&w, &WizardWindow::flowEnded);
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QTRY_VERIFY(w.formatPage()->isAsking());
        auto *field = w.formatPage()->findChild<QLineEdit *>();
        field->setText(QStringLiteral("IPOD"));
        QTest::mouseClick(w.formatPage()->confirmButton(), Qt::LeftButton);
        QTRY_VERIFY(w.dfuPage()->isAsking());
        QTest::mouseClick(w.dfuPage()->startButton(), Qt::LeftButton);
        QTRY_COMPARE(ended.count(), 1);
        QVERIFY(runner.waitFinished(5000));
        QCOMPARE(seen, QStringList({"firmware=write", "dfu=dfu", "restart=write", "verify=done",
                                    "finish=done"}));
        QCOMPARE(w.pageName(), QStringLiteral("done"));
    }

    // Screens section 10: Report opens the report page over the page of the step, with the facts
    // of the flow; Back returns to it. A failure shows the button of its page too.
    void reportOpensOverTheStepPage()
    {
        m_scripts.insert(QString(step::backupSettings),
                         [](StepContext &) { return StepOutcome::failed(QStringLiteral("boom")); });
        WizardRunner runner(m_dir.path(), factory());
        WizardOptions o = options();
        o.reportInput = [] {
            journal::ReportInput in;
            in.appVersion = QStringLiteral("0.2.0");
            return in;
        };
        WizardWindow w(target(), o, &runner, WizardEntry::Uninstall);
        w.diagnosePage()->setPause(0);
        w.backupPage()->setPause(0);
        QVERIFY(!buttonNamed(&w, "wizardReport")->isVisibleTo(&w));
        QTest::mouseClick(buttonNamed(&w, "wizardMain"), Qt::LeftButton);
        QVERIFY(runner.waitFinished(5000));
        QTRY_VERIFY(!w.isBusy());
        const QString before = w.pageName();
        QVERIFY(buttonNamed(&w, "wizardReport")->isVisibleTo(&w));
        QTest::mouseClick(buttonNamed(&w, "wizardReport"), Qt::LeftButton);
        QVERIFY(w.isReportOpen());
        QCOMPARE(w.pageName(), QStringLiteral("report"));
        QVERIFY(w.reportPage()->summaryText().contains(QLatin1String("boom")) ||
                w.reportPage()->summaryText().contains(QLatin1String("uninstall")));
        QVERIFY(!buttonNamed(&w, "wizardReport")->isVisibleTo(&w));
        QTest::mouseClick(w.reportPage()->backButton(), Qt::LeftButton);
        QVERIFY(!w.isReportOpen());
        QCOMPARE(w.pageName(), before);
        // The button of the failure page, when the failure shows on the page with the bar.
        if (before == QLatin1String("step")) {
            auto *button = w.findChild<QPushButton *>("reportFromFailure");
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            QVERIFY(w.isReportOpen());
        }
    }

    // Bug 3 of the real trial: the cross closes the window out of a write, and the card goes
    // back to its own buttons.
    void crossClosesOutOfAWrite()
    {
        m_scripts.insert(QString(step::backupSettings), [&](StepContext &c) {
            const UserAnswer a = c.waitForUser(WaitKind::Offer);
            Q_UNUSED(a);
            return StepOutcome::cancelled();
        });
        WizardRunner runner(m_dir.path(), factory());
        auto *w = new WizardWindow(target(), options(), &runner, WizardEntry::Uninstall);
        w->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<WizardWindow> guard(w);
        bool asked = false;
        w->setAsker([&](const QString &, const QString &) {
            asked = true;
            return true;
        });
        w->show();
        // Before the flow starts: closes at once, without a question.
        QVERIFY(w->close());
        QTRY_VERIFY(!guard);
        QVERIFY(!asked);

        auto *w2 = new WizardWindow(target(), options(), &runner, WizardEntry::Uninstall);
        w2->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<WizardWindow> guard2(w2);
        w2->setAsker([&](const QString &, const QString &) {
            asked = true;
            return true;
        });
        w2->show();
        QTest::mouseClick(buttonNamed(w2, "wizardMain"), Qt::LeftButton);
        QTRY_VERIFY(w2->isBusy());
        QTRY_COMPARE(w2->pageName(), QStringLiteral("offer"));
        QVERIFY(!w2->isWriting());
        // A wait is not a write: the cross asks, then closes the window.
        w2->close();
        QVERIFY(asked);
        QTRY_VERIFY(!guard2);
        QVERIFY(runner.waitFinished(5000));
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
        // The window closes with its progress kept: the card leaves "Show the wizard", and Open
        // and Eject come back.
        card.setWizardState(true, false);
        QCOMPARE(card.wizardButton()->text(), QStringLiteral("Resume"));
        QVERIFY(card.openButton()->isVisibleTo(&card));
        QVERIFY(card.ejectButton()->isVisibleTo(&card));
    }
};

QTEST_MAIN(TestWizardWindow)
#include "tst_wizardwindow.moc"
