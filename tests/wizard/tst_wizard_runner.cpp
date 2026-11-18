// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The runner and the progress file (docs/specs/install-wizard.md sections 2.4
// and 3), with steps that do nothing. Every file goes in a temporary folder.

#include "progress.h"
#include "runner.h"
#include "wizardconstants.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace tacetos::wizard;

Q_DECLARE_METATYPE(tacetos::wizard::FlowShape)

namespace {

const QString kSerial = QStringLiteral("8K1234ABCD");
const QString kModel = QStringLiteral("classic-6g");

enum class Act { Done, Fail, Block, Wait, SetFacts, CheckFacts, ReadProgress };

// What the fake steps do and what they saw. Shared by the steps of one test.
struct Script
{
    QMutex mutex;
    QHash<QString, Act> acts;
    QString failCode = QStringLiteral("some-failure");
    QStringList ran;
    QStringList resumed;
    QStringList answers;
    QStringList seen;

    void note(const QString &name, bool resume)
    {
        QMutexLocker locker(&mutex);
        ran << name;
        if (resume)
            resumed << name;
    }
    QStringList ranNow()
    {
        QMutexLocker locker(&mutex);
        return ran;
    }
};

class FakeStep : public Step
{
public:
    FakeStep(std::shared_ptr<Script> script, QString name, QString dataDir)
        : m_script(std::move(script)), m_name(std::move(name)), m_dataDir(std::move(dataDir))
    {
    }

    StepOutcome run(StepContext &context) override
    {
        m_script->note(m_name, context.isResume());
        const Act act = m_script->acts.value(m_name, Act::Done);
        switch (act) {
        case Act::Done:
            // A backup step names its folder, as the real ones do (section 3.2).
            if (m_name.startsWith(QLatin1String("backup-")))
                context.setBackupFolder(m_dataDir);
            break;
        case Act::Fail:
            return StepOutcome::failed(m_script->failCode, QStringLiteral("detail"));
        case Act::Block:
            while (!context.stopRequested())
                QThread::msleep(2);
            return StepOutcome::cancelled();
        case Act::Wait: {
            const UserAnswer answer = context.waitForUser(WaitKind::DiskMode);
            if (answer.cancelled)
                return StepOutcome::cancelled();
            QMutexLocker locker(&m_script->mutex);
            m_script->answers << answer.text;
            break;
        }
        case Act::SetFacts:
            context.setBackupFolder(m_dataDir);
            context.setFact(QStringLiteral("manifest"), QStringLiteral("m.json"));
            break;
        case Act::CheckFacts: {
            QMutexLocker locker(&m_script->mutex);
            m_script->seen << context.backupFolder()
                           << context
                                  .fact(QStringLiteral("backup-keep"), QStringLiteral("manifest"))
                                  .toString();
            break;
        }
        case Act::ReadProgress: {
            Progress progress;
            QFile file(ProgressStore(m_dataDir).filePath(context.serial()));
            const QString why = file.open(QIODevice::ReadOnly)
                                    ? parseProgress(file.readAll(), context.serial(), &progress)
                                    : QStringLiteral("not opened");
            QMutexLocker locker(&m_script->mutex);
            m_script->seen << why << statusName(progress.status)
                           << QString::number(progress.current);
            break;
        }
        }
        return StepOutcome::done();
    }

private:
    std::shared_ptr<Script> m_script;
    QString m_name;
    QString m_dataDir;
};

StepFactory factoryFor(const std::shared_ptr<Script> &script, const QString &dataDir)
{
    return [script, dataDir](const QString &name) -> std::unique_ptr<Step> {
        return std::make_unique<FakeStep>(script, name, dataDir);
    };
}

const FlowShape kClassicKeep{Family::Dfu, FlowKind::Install, InstallPath::Keep, false};
const FlowShape kOlderClean{Family::Partition, FlowKind::Install, InstallPath::Clean, false};

WizardRunner::Start startOf(const FlowShape &shape, const QString &serial = kSerial)
{
    return {serial, kModel, shape, QStringLiteral("/p/m.json"), QStringLiteral("ab")};
}

QJsonObject readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

// A valid progress file of a Classic keep install stopped at `current`.
QJsonObject validProgress(int current, const QString &status = QStringLiteral("next"))
{
    Progress progress;
    progress.appVersion = QStringLiteral("0.0.1");
    progress.serial = kSerial;
    progress.model = kModel;
    progress.shape = kClassicKeep;
    progress.steps = stepsFor(kClassicKeep);
    progress.current = current;
    progress.status = StepStatus::Next;
    progress.started = progress.updated = QDateTime::currentDateTimeUtc();
    QJsonObject object = toJson(progress);
    object.insert(QStringLiteral("status"), status);
    return object;
}

} // namespace

class TstWizardRunner : public QObject
{
    Q_OBJECT

private slots:
    void runsEveryStep();
    void finishFactsReachTheEnd();
    void progressWrittenBeforeEachStep();
    void cutAtEachStep_data();
    void cutAtEachStep();
    void cutWhileWaiting();
    void failureStopsTheFlow();
    void cancelDuringWait();
    void answerReachesTheStep();
    void factsSurviveACut();
    void refusedFiles_data();
    void refusedFiles();
    void noProgress();
    void otherFamilyIsMismatch();
    void twoFlowsOnOneIpod();
    void lockOfAnotherCopy();
    void staleLockTakenOver();
    void startRefusals();
    void safeSerial();
};

void TstWizardRunner::runsEveryStep()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QSignalSpy done(&runner, &WizardRunner::stepDone);

    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(finished.wait(5000));
    QCOMPARE(script->ranNow(), stepsFor(kClassicKeep));
    QCOMPARE(done.count(), stepsFor(kClassicKeep).size());
    QVERIFY(script->resumed.isEmpty());
    // Section 3.1: the file is deleted when the flow ends well, and the lock too.
    const ProgressStore store(dir.path());
    QVERIFY(!QFile::exists(store.filePath(kSerial)));
    QVERIFY(!QFile::exists(store.lockPath(kSerial)));
}

// Section 11.2: the facts of `finish` come with the end of the flow, after the
// progress file that held them is gone.
void TstWizardRunner::finishFactsReachTheEnd()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("finish"), Act::SetFacts);
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);

    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(finished.wait(5000));
    const QJsonObject summary = finished.at(0).at(0).toJsonObject();
    QCOMPARE(summary.value(QStringLiteral("manifest")).toString(), QStringLiteral("m.json"));
    QVERIFY(!QFile::exists(ProgressStore(dir.path()).filePath(kSerial)));
}

void TstWizardRunner::progressWrittenBeforeEachStep()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("firmware"), Act::ReadProgress);
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(finished.wait(5000));
    // `firmware` is the third step: the file says it runs.
    QCOMPARE(script->seen,
             (QStringList{QString(), QStringLiteral("running"), QStringLiteral("2")}));
}

void TstWizardRunner::cutAtEachStep_data()
{
    QTest::addColumn<FlowShape>("shape");
    QTest::addColumn<int>("cut");
    for (const auto &[label, shape] :
         {std::pair{"classic keep", kClassicKeep}, std::pair{"older clean", kOlderClean}}) {
        const int n = stepsFor(shape).size();
        for (int i = 0; i < n; ++i)
            QTest::addRow("%s, step %d", label, i) << shape << i;
    }
}

void TstWizardRunner::cutAtEachStep()
{
    QFETCH(FlowShape, shape);
    QFETCH(int, cut);
    const QStringList steps = stepsFor(shape);
    QTemporaryDir dir;
    const ProgressStore store(dir.path());

    auto first = std::make_shared<Script>();
    first->acts.insert(steps.at(cut), Act::Block);
    {
        WizardRunner runner(dir.path(), factoryFor(first, dir.path()));
        QCOMPARE(runner.start(startOf(shape)), QString());
        QTRY_VERIFY_WITH_TIMEOUT(first->ranNow().contains(steps.at(cut)), 5000);
        // The runner goes away in the middle of the step: a cut.
    }
    const QJsonObject saved = readFile(store.filePath(kSerial));
    QCOMPARE(saved.value(QStringLiteral("status")).toString(), QStringLiteral("running"));
    QCOMPARE(saved.value(QStringLiteral("current")).toInt(), cut);

    auto second = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(second, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.resume(kSerial, kModel, shape.family), QString());
    QVERIFY(finished.wait(5000));

    // Section 3.3: the diagnosis first, then the step that was cut, run with its
    // rule of resume, then the rest.
    QStringList expected = steps.mid(cut);
    if (cut > 0)
        expected.prepend(steps.first());
    QCOMPARE(second->ranNow(), expected);
    QCOMPARE(second->resumed, QStringList{steps.at(cut)});
    QVERIFY(!QFile::exists(store.filePath(kSerial)));
}

void TstWizardRunner::cutWhileWaiting()
{
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    auto first = std::make_shared<Script>();
    first->acts.insert(QStringLiteral("firmware"), Act::Wait);
    {
        WizardRunner runner(dir.path(), factoryFor(first, dir.path()));
        QSignalSpy waiting(&runner, &WizardRunner::stepWaiting);
        QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
        QVERIFY(waiting.wait(5000));
        QCOMPARE(waiting.first().at(0).toString(), QStringLiteral("firmware"));
        QCOMPARE(waiting.first().at(1).value<WaitKind>(), WaitKind::DiskMode);
    }
    const QJsonObject saved = readFile(store.filePath(kSerial));
    QCOMPARE(saved.value(QStringLiteral("status")).toString(), QStringLiteral("waiting"));
    QCOMPARE(saved.value(QStringLiteral("current")).toInt(), 2);

    auto second = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(second, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.resume(kSerial, kModel, Family::Dfu), QString());
    QVERIFY(finished.wait(5000));
    QCOMPARE(second->resumed, QStringList{QStringLiteral("firmware")});
}

void TstWizardRunner::failureStopsTheFlow()
{
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("backup-keep"), Act::Fail);
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
    QSignalSpy failed(&runner, &WizardRunner::stepFailed);
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(stopped.wait(5000));
    QCOMPARE(stopped.first().at(0).toString(), QStringLiteral("some-failure"));
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("backup-keep"));
    QCOMPARE(failed.first().at(3).toString(), QStringLiteral("detail"));
    // Section 2.4 rule 6: nothing after a failure.
    QCOMPARE(script->ranNow(),
             (QStringList{QStringLiteral("diagnose"), QStringLiteral("backup-keep")}));
    const QJsonObject saved = readFile(store.filePath(kSerial));
    QCOMPARE(saved.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QCOMPARE(saved.value(QStringLiteral("error")).toString(), QStringLiteral("some-failure"));
    QVERIFY(!QFile::exists(store.lockPath(kSerial)));

    // Resume runs the failed step again, with its rule of resume.
    QVERIFY(runner.waitFinished(5000));
    auto again = std::make_shared<Script>();
    WizardRunner second(dir.path(), factoryFor(again, dir.path()));
    QSignalSpy finished(&second, &WizardRunner::flowFinished);
    QCOMPARE(second.resume(kSerial, kModel, Family::Dfu), QString());
    QVERIFY(finished.wait(5000));
    QCOMPARE(again->resumed, QStringList{QStringLiteral("backup-keep")});
}

void TstWizardRunner::cancelDuringWait()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("diagnose"), Act::Wait);
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy waiting(&runner, &WizardRunner::stepWaiting);
    QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(waiting.wait(5000));
    runner.cancel();
    QVERIFY(stopped.wait(5000));
    QCOMPARE(stopped.first().at(0).toString(), QStringLiteral("cancelled"));
    // Section 2.4 rule 5: the progress file stays, Resume starts the step again.
    const QJsonObject saved = readFile(ProgressStore(dir.path()).filePath(kSerial));
    QCOMPARE(saved.value(QStringLiteral("error")).toString(), QStringLiteral("cancelled"));
    QCOMPARE(saved.value(QStringLiteral("current")).toInt(), 0);
}

void TstWizardRunner::answerReachesTheStep()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("backup-keep"), Act::Wait);
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy waiting(&runner, &WizardRunner::stepWaiting);
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(waiting.wait(5000));
    runner.answer(UserAnswer{false, true, QStringLiteral("MY IPOD")});
    QVERIFY(finished.wait(5000));
    QCOMPARE(script->answers, QStringList{QStringLiteral("MY IPOD")});
}

void TstWizardRunner::factsSurviveACut()
{
    QTemporaryDir dir;
    auto first = std::make_shared<Script>();
    first->acts.insert(QStringLiteral("backup-keep"), Act::SetFacts);
    first->acts.insert(QStringLiteral("firmware"), Act::Block);
    {
        WizardRunner runner(dir.path(), factoryFor(first, dir.path()));
        QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
        QTRY_VERIFY_WITH_TIMEOUT(first->ranNow().contains(QStringLiteral("firmware")), 5000);
    }
    auto second = std::make_shared<Script>();
    second->acts.insert(QStringLiteral("firmware"), Act::CheckFacts);
    WizardRunner runner(dir.path(), factoryFor(second, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.resume(kSerial, kModel, Family::Dfu), QString());
    QVERIFY(finished.wait(5000));
    QCOMPARE(second->seen, (QStringList{dir.path(), QStringLiteral("m.json")}));
}

void TstWizardRunner::refusedFiles_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const auto json = [](const QJsonObject &object) { return QJsonDocument(object).toJson(); };

    QTest::newRow("not JSON") << QByteArray("{\"format\": 1,");
    QTest::newRow("a list") << QByteArray("[1, 2]");
    QJsonObject o = validProgress(1);
    o.remove(QStringLiteral("steps"));
    QTest::newRow("no steps") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("format"), 2);
    QTest::newRow("format 2") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("serial"), QStringLiteral("OTHER"));
    QTest::newRow("other serial") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("steps"),
             QJsonArray{QStringLiteral("diagnose"), QStringLiteral("firmware")});
    QTest::newRow("other list of steps") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("current"), 6);
    QTest::newRow("current out of range") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("current"), 1.5);
    QTest::newRow("current not whole") << json(o);
    o = validProgress(1, QStringLiteral("paused"));
    QTest::newRow("unknown status") << json(o);
    o = validProgress(1, QStringLiteral("failed"));
    QTest::newRow("failed without error") << json(o);
    o = validProgress(2);
    QTest::newRow("after the backup, no backup folder") << json(o);
    o = validProgress(2);
    o.insert(QStringLiteral("backup"), QStringLiteral("/no/such/folder/of/tacetos"));
    QTest::newRow("backup folder gone") << json(o);
    o = validProgress(1);
    o.insert(QStringLiteral("path"), QStringLiteral("everything"));
    QTest::newRow("unknown path") << json(o);
}

void TstWizardRunner::refusedFiles()
{
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    writeFile(store.filePath(kSerial), bytes);

    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(runner.resume(kSerial, kModel, Family::Dfu), QStringLiteral("progress-refused"));
    QVERIFY(!runner.refusedReason().isEmpty());
    QVERIFY(!runner.isRunning());
    // Section 3.4: renamed, never resumed blindly.
    QVERIFY(!QFile::exists(store.filePath(kSerial)));
    QVERIFY(QFile::exists(store.filePath(kSerial) + QStringLiteral(".rejected")));
    QVERIFY(script->ranNow().isEmpty());
    // The lock is released: a new flow can start.
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(runner.waitFinished(5000));
}

void TstWizardRunner::noProgress()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(runner.resume(kSerial, kModel, Family::Dfu), QStringLiteral("no-progress"));
}

void TstWizardRunner::otherFamilyIsMismatch()
{
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    writeFile(store.filePath(kSerial), QJsonDocument(validProgress(1)).toJson());
    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(runner.resume(kSerial, kModel, Family::Partition),
             QStringLiteral("progress-mismatch"));
    QCOMPARE(runner.resume(kSerial, QStringLiteral("nano-2g"), Family::Dfu),
             QStringLiteral("progress-mismatch"));
    QVERIFY(QFile::exists(store.filePath(kSerial)));
    QVERIFY(script->ranNow().isEmpty());
}

void TstWizardRunner::twoFlowsOnOneIpod()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    script->acts.insert(QStringLiteral("diagnose"), Act::Block);
    WizardRunner first(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(first.start(startOf(kClassicKeep)), QString());

    auto other = std::make_shared<Script>();
    WizardRunner second(dir.path(), factoryFor(other, dir.path()));
    QCOMPARE(second.start(startOf(kClassicKeep)), QStringLiteral("flow-running"));
    QCOMPARE(second.resume(kSerial, kModel, Family::Dfu), QStringLiteral("flow-running"));
    QCOMPARE(first.start(startOf(kOlderClean, QStringLiteral("OTHER"))),
             QStringLiteral("flow-running"));

    // Another iPod is not blocked.
    QSignalSpy finished(&second, &WizardRunner::flowFinished);
    QCOMPARE(second.start(startOf(kOlderClean, QStringLiteral("OTHER"))), QString());
    QVERIFY(finished.wait(5000));
    first.cancel();
    QVERIFY(first.waitFinished(5000));
}

void TstWizardRunner::lockOfAnotherCopy()
{
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    QDir().mkpath(store.folder());
    QLockFile held(store.lockPath(kSerial));
    QVERIFY(held.tryLock(0));

    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(runner.start(startOf(kClassicKeep)), QStringLiteral("flow-running"));
    held.unlock();
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(runner.waitFinished(5000));
}

void TstWizardRunner::staleLockTakenOver()
{
    QTemporaryDir dir;
    const ProgressStore store(dir.path());
    // A lock left by a process that does not run any more.
    writeFile(store.lockPath(kSerial), QByteArray("999999999\nTacetOS\n\n"));
    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QSignalSpy finished(&runner, &WizardRunner::flowFinished);
    QCOMPARE(runner.start(startOf(kClassicKeep)), QString());
    QVERIFY(finished.wait(5000));
}

void TstWizardRunner::startRefusals()
{
    QTemporaryDir dir;
    auto script = std::make_shared<Script>();
    WizardRunner runner(dir.path(), factoryFor(script, dir.path()));
    QCOMPARE(runner.start(startOf(kClassicKeep, QString())), QStringLiteral("no-serial"));
    QCOMPARE(runner.start(startOf({Family::Dfu, FlowKind::Install, InstallPath::Keep, true})),
             QStringLiteral("bad-flow"));
    QVERIFY(script->ranNow().isEmpty());
}

void TstWizardRunner::safeSerial()
{
    QCOMPARE(ProgressStore::safeSerial(QStringLiteral("000A:27-F_x/y")),
             QStringLiteral("000A_27-F_x_y"));
    QCOMPARE(ProgressStore::safeSerial(QStringLiteral("é")), QStringLiteral("_"));
}

QTEST_GUILESS_MAIN(TstWizardRunner)
#include "tst_wizard_runner.moc"
