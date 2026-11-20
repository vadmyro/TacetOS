// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-screens.md sections 4 and 14.2: the page of the backup (bytes, time left,
// file line, end line, stop, failure with names) and the page of the restore (list from
// manifests made by the test, incomplete greyed, questions, counts). The runner has steps made
// by the test, the backups are made by the real steps on temporary folders.

#include "backup.h"
#include "constants.h"
#include "runner.h"
#include "wizard/backuppage.h"
#include "wizard/restorepage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;
namespace device = tacetos::device;

namespace {

const QString kSerial = QStringLiteral("8K1234ABCD");
const QString kModel = QStringLiteral("classic-6g");

// What the test tells a step to do.
struct Script
{
    QSemaphore gate;
    QList<qint64> points;
    qint64 total = 0;
    QString stage = QStringLiteral("copy");
    enum class End { Done, Fail, Hold } end = End::Done;
    QString code;
};

class ScriptStep : public Step
{
public:
    explicit ScriptStep(Script *script) : script_(script) {}
    StepOutcome run(StepContext &context) override
    {
        for (const qint64 done : std::as_const(script_->points)) {
            while (!script_->gate.tryAcquire(1, 20)) {
                if (context.stopRequested())
                    return StepOutcome::cancelled();
            }
            context.progress(script_->stage, done, script_->total);
        }
        if (script_->end == Script::End::Hold) {
            while (!context.stopRequested())
                QThread::msleep(5);
            return StepOutcome::cancelled();
        }
        if (script_->end == Script::End::Fail)
            return StepOutcome::failed(script_->code);
        return StepOutcome::done();
    }

private:
    Script *script_;
};

class DoneStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

// The real step, with its report kept for the test (the window reads it the same way).
class KeepReport : public Step
{
public:
    KeepReport(std::unique_ptr<BackupStep> step, BackupReport *report)
        : step_(std::move(step)), report_(report)
    {
    }
    StepOutcome run(StepContext &context) override
    {
        const StepOutcome outcome = step_->run(context);
        *report_ = step_->report();
        return outcome;
    }

private:
    std::unique_ptr<BackupStep> step_;
    BackupReport *report_;
};

class FakePartition : public FirmwarePartition
{
public:
    HelperAnswer backup(const QString &dataDir) override
    {
        const QString path = QDir(dataDir).filePath(QStringLiteral("firmware-partition.bin"));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray(4096, 'F'));
        HelperAnswer answer;
        answer.data.insert(QStringLiteral("path"), path);
        return answer;
    }
    HelperAnswer restore(const QString &file, const QString &) override
    {
        restored << file;
        return {};
    }
    QStringList restored;
};

class FakeSources : public BackupSources
{
public:
    QString volumeRoot() const override { return volume; }
    QString volumeLabel() const override { return QStringLiteral("IPOD"); }
    QString backupRoot() const override { return root; }
    qint64 freeBytes(const QString &) const override { return -1; }
    qint64 volumeUsedBytes() const override { return used; }
    FirmwarePartition *firmwarePartition() const override { return partition; }
    qint64 firmwarePartitionBytes() const override { return 4096; }
    QDateTime now() const override { return when; }

    QString volume;
    QString root;
    qint64 used = 0;
    FirmwarePartition *partition = nullptr;
    QDateTime when;
};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QLabel *label(QWidget *page, const char *name)
{
    return page->findChild<QLabel *>(QString::fromLatin1(name));
}

QPushButton *button(QWidget *page, const char *name)
{
    return page->findChild<QPushButton *>(QString::fromLatin1(name));
}

FlowShape shape()
{
    FlowShape s;
    s.family = Family::Dfu;
    s.kind = FlowKind::Install;
    s.path = InstallPath::Keep;
    return s;
}

WizardRunner::Start startOf()
{
    WizardRunner::Start s;
    s.serial = kSerial;
    s.model = kModel;
    s.shape = shape();
    s.packageManifest = QStringLiteral("/p/m.json");
    return s;
}

bool isBackupStep(const QString &name)
{
    return name.startsWith(QLatin1String("backup-"));
}

} // namespace

class TestBackupPage : public QObject
{
    Q_OBJECT

private:
    // A runner whose backup step follows a script, a page, and a clock that the test sets.
    struct Fixture
    {
        QTemporaryDir dir;
        Script script;
        std::atomic<qint64> clock{0};
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<BackupPage> page;

        Fixture()
        {
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (isBackupStep(name))
                        return std::make_unique<ScriptStep>(&script);
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<BackupPage>(runner.get());
            page->setClock([this] { return clock.load(); });
            page->setPause(30);
            page->resize(600, 400);
            page->show();
        }

        void start() { QCOMPARE(runner->start(startOf()), QString()); }

        // One more progress point of the step, at `ms` of the fake clock.
        void step(qint64 ms, const QString &expected)
        {
            clock = ms;
            script.gate.release();
            QTRY_VERIFY2(label(page.get(), "stepProgressText")->text().contains(expected),
                         qPrintable(label(page.get(), "stepProgressText")->text()));
        }
    };

private slots:
    void sizesAndTimes()
    {
        QCOMPARE(backupSizeText(71000000000LL), QStringLiteral("71.0 GB"));
        QCOMPARE(backupSizeText(2100000), QStringLiteral("2.1 MB"));
        QCOMPARE(backupSizeText(12000), QStringLiteral("12.0 kB"));
        QCOMPARE(backupTimeLeftText(30), QStringLiteral("less than a minute left"));
        QCOMPARE(backupTimeLeftText(22 * 60), QStringLiteral("about 22 min left"));
        QCOMPARE(backupTimeLeftText(3900), QStringLiteral("about 1 h 05 min left"));
    }

    void progressInBytesAndTimeLeft()
    {
        Fixture f;
        f.script.total = 100000000000LL;
        f.script.points = {0, 5000000000LL, 10000000000LL, 20000000000LL};
        f.script.end = Script::End::Hold;
        f.start();
        f.step(0, QStringLiteral("0.0 kB of 100.0 GB"));
        QVERIFY(f.page->isActive());
        QCOMPARE(label(f.page.get(), "backupWhat")->text(),
                 QStringLiteral("Copying your settings, themes and playlists"));
        // The first 10 s only tell the speed of the caches.
        f.step(5000, QStringLiteral("5.0 GB of 100.0 GB \u00b7 Computing the time left"));
        // 10 GB in 10 s: 90 s left, written as 2 minutes.
        f.step(10000, QStringLiteral("10.0 GB of 100.0 GB \u00b7 about 2 min left"));
        auto *bar = f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"));
        QCOMPARE(bar->value(), 100);
        // The window moves: 20 GB at 20 s, still 1 GB per second, 80 s left.
        f.step(20000, QStringLiteral("20.0 GB of 100.0 GB \u00b7 about 2 min left"));
        QCOMPARE(bar->value(), 200);
        f.runner->cancel();
        QVERIFY(f.runner->waitFinished(5000));
    }

    void fileLineAndStage()
    {
        Fixture f;
        f.script.total = 1000;
        f.script.points = {10};
        f.script.end = Script::End::Hold;
        f.start();
        f.step(0, QStringLiteral("of"));
        f.page->setCurrentFile(QStringLiteral("Music/Artist/Album/03 Song.mp3"));
        QCOMPARE(label(f.page.get(), "stepCurrentFile")->text(),
                 QStringLiteral("Music/Artist/Album/03 Song.mp3"));
        // A path too long for the line is cut in the middle with "...".
        const QString longPath =
            QStringLiteral("Music/") + QString(300, QLatin1Char('x')) + QStringLiteral("/last.mp3");
        f.page->setCurrentFile(longPath);
        const QString shown = label(f.page.get(), "stepCurrentFile")->text();
        QVERIFY(shown.contains(QStringLiteral("...")));
        QVERIFY(!shown.contains(QChar(0x2026)));
        QVERIFY(shown.startsWith(QStringLiteral("Music/")));
        QVERIFY(shown.endsWith(QStringLiteral("last.mp3")));
        QVERIFY(shown.size() < longPath.size());
        f.runner->cancel();
        QVERIFY(f.runner->waitFinished(5000));
    }

    void firmwarePartitionStage()
    {
        Fixture f;
        f.script.stage = QStringLiteral("firmware-partition");
        f.script.points = {0};
        f.script.end = Script::End::Hold;
        f.start();
        f.page->setCurrentFile(QStringLiteral("Music/a.mp3"));
        f.script.gate.release();
        QTRY_COMPARE(label(f.page.get(), "stepCurrentFile")->text(),
                     QStringLiteral("Copying the firmware partition"));
        f.runner->cancel();
        QVERIFY(f.runner->waitFinished(5000));
    }

    void endLineAndPause()
    {
        Fixture f;
        f.script.total = 100000000000LL;
        f.script.points = {100000000000LL};
        f.page->setFileCount(1234);
        QSignalSpy ready(f.page.get(), &BackupPage::readyToGoOn);
        f.clock = 1000;
        f.script.gate.release();
        f.start();
        QTRY_VERIFY(!f.page->checkedLine().isEmpty());
        QCOMPARE(f.page->checkedLine(), QStringLiteral("Backup checked: 1,234 files, 100.0 GB"));
        QCOMPARE(label(f.page.get(), "backupNote")->text(), f.page->checkedLine());
        QVERIFY(!f.page->isActive());
        QTRY_COMPARE(ready.count(), 1);
    }

    void cancelKeepsTheFiles()
    {
        Fixture f;
        f.script.total = 1000;
        f.script.points = {100};
        f.script.end = Script::End::Hold;
        f.start();
        f.step(0, QStringLiteral("of"));
        QVERIFY(f.page->isActive());
        f.runner->cancel();
        QTRY_VERIFY(f.page->isStopped());
        QVERIFY(!f.page->isActive());
        QVERIFY(!f.page->isFailed());
        QVERIFY(label(f.page.get(), "backupNote")->text().startsWith(QStringLiteral("Stopped.")));
        QVERIFY(label(f.page.get(), "backupNote")
                    ->text()
                    .contains(QStringLiteral("The files already checked are kept")));
        QVERIFY(label(f.page.get(), "backupBanner")->isHidden());
        QVERIFY(f.page->checkedLine().isEmpty());
    }

    void failureListsNamesButLogsNone()
    {
        Fixture f;
        f.script.end = Script::End::Fail;
        f.script.code = QString(error::backupReadFailed);
        QStringList names;
        for (int i = 0; i < 57; ++i)
            names << QStringLiteral("Music/Zéro/secret-name-%1.mp3").arg(i);
        f.start();
        QTRY_VERIFY(f.page->isFailed());
        f.page->setFileNames(names);
        QCOMPARE(label(f.page.get(), "backupBanner")->text(),
                 QStringLiteral("Some files of the iPod could not be read."));
        QVERIFY(!label(f.page.get(), "backupBanner")->isHidden());
        auto *list = f.page->findChild<QPlainTextEdit *>(QStringLiteral("backupNames"));
        QVERIFY(list && !list->isHidden());
        QVERIFY(list->isReadOnly());
        const QStringList lines = list->toPlainText().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 21);
        QCOMPARE(lines.first(), names.first());
        QCOMPARE(lines.last(), QStringLiteral("and 37 more"));
        QCOMPARE(label(f.page.get(), "backupNothingWritten")->text(),
                 QStringLiteral("Nothing was written to the iPod."));
        QVERIFY(f.runner->waitFinished(5000));
        // The names stay on the screen: the files of the data folder never hold one.
        QDirIterator it(f.dir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            QVERIFY(!readFile(it.next()).contains("secret-name"));
    }

    void eachCodeHasItsMessage_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("full") << QString(error::backupFullDisk)
                              << QStringLiteral(
                                     "The backup folder is full. Choose another folder.");
        QTest::newRow("verify") << QString(error::backupVerifyFailed)
                                << QStringLiteral("The copy on this computer does not match");
        QTest::newRow("changed") << QStringLiteral("source-changed")
                                 << QStringLiteral("Files changed on the iPod during the backup");
        QTest::newRow("name") << QString(error::backupNameRefused)
                              << QStringLiteral("Some names of the iPod cannot be used");
    }

    void eachCodeHasItsMessage()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        Fixture f;
        f.script.end = Script::End::Fail;
        f.script.code = code;
        f.start();
        QTRY_VERIFY(f.page->isFailed());
        QVERIFY(label(f.page.get(), "backupBanner")->text().startsWith(text));
        QVERIFY(!label(f.page.get(), "backupBanner")->text().contains(code));
    }

    void folderLineAndOpen()
    {
        Fixture f;
        QString opened;
        f.page->setOpener([&opened](const QString &folder) { opened = folder; });
        QVERIFY(button(f.page.get(), "openBackupFolder")->isHidden());
        f.page->setBackupFolder(QStringLiteral("/home/anna/Backups/8K1234ABCD/2026-11-18_201430"));
        QVERIFY(label(f.page.get(), "backupFolderLine")
                    ->text()
                    .startsWith(QStringLiteral("Saved to ")));
        button(f.page.get(), "openBackupFolder")->click();
        QCOMPARE(opened, QStringLiteral("/home/anna/Backups/8K1234ABCD/2026-11-18_201430"));
    }
};

class TestRestorePage : public QObject
{
    Q_OBJECT

private:
    // Backups made by the real steps, from a temporary folder that plays the music volume.
    struct World
    {
        QTemporaryDir dir;
        QString volume;
        QString root;
        FakePartition partition;
        int made = 0;

        World()
        {
            volume = dir.filePath(QStringLiteral("ipod"));
            root = dir.filePath(QStringLiteral("backups"));
            const QString fw = QString(device::kFirmwareFolder);
            writeFile(volume + QStringLiteral("/") + fw + QStringLiteral("/config.cfg"),
                      QByteArray("volume: 11\n"));
            writeFile(volume + QStringLiteral("/Playlists/road trip.m3u"), QByteArray("#EXTM3U\n"));
            writeFile(volume + QStringLiteral("/Music/Zéro/song.mp3"), QByteArray(5000, 'm'));
            writeFile(volume + QStringLiteral("/Music/empty.mp3"), QByteArray());
        }

        // Runs `backup-<kind>` through a runner and returns the dated folder.
        QString backup(BackupKind kind, bool withPartition, const QDateTime &when,
                       BackupReport *out = nullptr)
        {
            FakeSources sources;
            sources.volume = volume;
            sources.root = root;
            sources.used = 5000;
            sources.when = when;
            sources.partition = withPartition ? &partition : nullptr;
            BackupReport report;
            const QString stepName =
                kind == BackupKind::Full
                    ? QString(step::backupFull)
                    : (kind == BackupKind::Keep ? QString(step::backupKeep)
                                                : QString(step::backupSettings));
            WizardRunner runner(dir.filePath(QStringLiteral("data%1").arg(++made)),
                                [&](const QString &name) -> std::unique_ptr<Step> {
                                    if (name == stepName)
                                        return std::make_unique<KeepReport>(
                                            std::make_unique<BackupStep>(kind, sources), &report);
                                    return std::make_unique<DoneStep>();
                                });
            QSignalSpy finished(&runner, &WizardRunner::flowFinished);
            QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
            WizardRunner::Start s = startOf();
            s.shape.family = withPartition ? Family::Partition : Family::Dfu;
            s.shape.path = kind == BackupKind::Full ? InstallPath::Clean : InstallPath::Keep;
            if (runner.start(s) != QString())
                return QString();
            // A slow runner (Windows, debug, four tests at once) can take more than 10 s.
            if (!QTest::qWaitFor([&] { return finished.count() + stopped.count() > 0; }, 60000)) {
                qWarning("backup: still running after 60 s");
                return QString();
            }
            if (out)
                *out = report;
            if (stopped.count() > 0) {
                qWarning("backup: stopped with %s", qPrintable(stopped.first().first().toString()));
                return QString();
            }
            return report.folder;
        }
    };

    static RestorePage *makePage(World &w, QWidget *parent = nullptr)
    {
        auto *page = new RestorePage(w.root, kSerial, parent);
        page->setIpodName(QStringLiteral("iPod Classic 120 GB"));
        page->resize(640, 480);
        page->show();
        return page;
    }

    static QRadioButton *radio(RestorePage *page, const QString &name)
    {
        return page->findChild<QRadioButton *>(QStringLiteral("backup-") + name);
    }

public:
    // The folders hold a date by name.
    static QDateTime day(int d, int h = 20, int m = 14)
    {
        return QDateTime(QDate(2026, 11, d), QTime(h, m, 30));
    }

private slots:
    void listsNewestFirstWithIncompleteGreyed()
    {
        World w;
        QVERIFY(!w.backup(BackupKind::Keep, false, day(12)).isEmpty());
        QVERIFY(!w.backup(BackupKind::Full, false, day(18)).isEmpty());
        // A copy that stopped: the manifest of the end is not there.
        const QString partial =
            QDir(w.root).filePath(serialFolderName(kSerial) + QStringLiteral("/2026-11-10_090200"));
        QVERIFY(writeFile(partial + QStringLiteral("/manifest.partial"), QByteArray("{}")));

        std::unique_ptr<RestorePage> page(makePage(w));
        QCOMPARE(page->entries().size(), 3);
        QCOMPARE(page->entries()[0].name, QStringLiteral("2026-11-18_201430"));
        QCOMPARE(page->entries()[1].name, QStringLiteral("2026-11-12_201430"));
        QCOMPARE(page->entries()[2].name, QStringLiteral("2026-11-10_090200"));
        QVERIFY(page->entries()[0].usable);
        QVERIFY(!page->entries()[2].usable);

        QVERIFY(radio(page.get(), QStringLiteral("2026-11-18_201430"))->text().contains("Full"));
        QVERIFY(radio(page.get(), QStringLiteral("2026-11-12_201430"))
                    ->text()
                    .contains("Keep my music"));
        QRadioButton *incomplete = radio(page.get(), QStringLiteral("2026-11-10_090200"));
        QVERIFY(incomplete->text().contains("Incomplete"));
        QVERIFY(!incomplete->isEnabled());
        // None is chosen at the opening, and Restore is off until one is.
        QCOMPARE(page->chosen(), -1);
        page->setVolume(w.volume);
        QVERIFY(!button(page.get(), "restoreStart")->isEnabled());
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        QVERIFY(button(page.get(), "restoreStart")->isEnabled());
    }

    void otherIpodOrNotMounted()
    {
        World w;
        QVERIFY(!w.backup(BackupKind::Keep, false, day(12)).isEmpty());
        std::unique_ptr<RestorePage> page(makePage(w));
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        QVERIFY(!button(page.get(), "restoreStart")->isEnabled());
        QVERIFY(!label(page.get(), "restorePlugIn")->isHidden());
        QCOMPARE(label(page.get(), "restorePlugIn")->text(),
                 QStringLiteral("Plug in the iPod of this backup"));
        page->setVolume(w.volume);
        QVERIFY(button(page.get(), "restoreStart")->isEnabled());
        QVERIFY(label(page.get(), "restorePlugIn")->isHidden());
    }

    void keepBackupGoesBackWithoutAQuestion()
    {
        World w;
        const QString folder = w.backup(BackupKind::Keep, false, day(12));
        QVERIFY(!folder.isEmpty());
        const QString target = w.dir.filePath(QStringLiteral("fresh"));
        QDir().mkpath(target);
        std::unique_ptr<RestorePage> page(makePage(w));
        int asked = 0;
        page->setAsker([&asked](const QString &, const QString &) {
            ++asked;
            return true;
        });
        page->setVolume(target);
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        QSignalSpy finished(page.get(), &RestorePage::restoreFinished);
        button(page.get(), "restoreStart")->click();
        QVERIFY(page->isRunning());
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString());
        QCOMPARE(asked, 0);
        QVERIFY(page->isDone());
        QVERIFY(label(page.get(), "restoreSummary")->text().startsWith(QStringLiteral("Done: ")));
        QCOMPARE(readFile(target + QStringLiteral("/") + QString(device::kFirmwareFolder) +
                          QStringLiteral("/config.cfg")),
                 QByteArray("volume: 11\n"));
        QCOMPARE(readFile(target + QStringLiteral("/Playlists/road trip.m3u")),
                 QByteArray("#EXTM3U\n"));
        // The music is not in a Keep backup.
        QVERIFY(!QFileInfo::exists(target + QStringLiteral("/Music")));
        QVERIFY(!button(page.get(), "restoreStart")->isEnabled());
    }

    void fullBackupAsksFirstAndCountsSkipped()
    {
        World w;
        QVERIFY(!w.backup(BackupKind::Full, false, day(18)).isEmpty());
        const QString target = w.dir.filePath(QStringLiteral("fresh"));
        QDir().mkpath(target);
        // One file is already on the iPod.
        QVERIFY(writeFile(target + QStringLiteral("/Music/empty.mp3"), QByteArray()));

        std::unique_ptr<RestorePage> page(makePage(w));
        QStringList questions;
        bool answer = false;
        page->setAsker([&](const QString &, const QString &text) {
            questions << text;
            return answer;
        });
        page->setVolume(target);
        radio(page.get(), QStringLiteral("2026-11-18_201430"))->click();
        // No: nothing is written.
        button(page.get(), "restoreStart")->click();
        QCOMPARE(questions.size(), 1);
        QVERIFY(questions.first().startsWith(QStringLiteral("Copy 4 files back to the iPod (")));
        QVERIFY(questions.first().contains(QStringLiteral("Files already there are skipped")));
        QVERIFY(!page->isRunning());
        QVERIFY(!QFileInfo::exists(target + QStringLiteral("/Music/Zéro")));

        answer = true;
        QSignalSpy finished(page.get(), &RestorePage::restoreFinished);
        button(page.get(), "restoreStart")->click();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString());
        QCOMPARE(label(page.get(), "restoreTitle")->text(),
                 QStringLiteral("Putting your music back"));
        QVERIFY(label(page.get(), "restoreSummary")->text().startsWith(QStringLiteral("Done: ")));
        QVERIFY(
            label(page.get(), "restoreSummary")->text().contains(QStringLiteral("already there")));
        QCOMPARE(readFile(target + QStringLiteral("/Music/Zéro/song.mp3")), QByteArray(5000, 'm'));
        QVERIFY(label(page.get(), "stepProgressText")->text().contains(QStringLiteral(" of ")));
    }

    void damagedBackupIsReported()
    {
        World w;
        const QString folder = w.backup(BackupKind::Keep, false, day(12));
        QVERIFY(!folder.isEmpty());
        // A file of the backup is changed after the manifest was written.
        QDirIterator it(backupVolumeFolder(folder), QDir::Files | QDir::Hidden,
                        QDirIterator::Subdirectories);
        QVERIFY(it.hasNext());
        QVERIFY(writeFile(it.next(), QByteArray("damaged")));

        const QString target = w.dir.filePath(QStringLiteral("fresh"));
        QDir().mkpath(target);
        std::unique_ptr<RestorePage> page(makePage(w));
        page->setVolume(target);
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        QSignalSpy finished(page.get(), &RestorePage::restoreFinished);
        button(page.get(), "restoreStart")->click();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString(error::backupDamaged));
        QCOMPARE(label(page.get(), "restoreBanner")->text(),
                 QStringLiteral("This backup is damaged."));
        QVERIFY(!label(page.get(), "restoreBanner")->isHidden());
        QVERIFY(label(page.get(), "restoreSummary")->text().contains(QStringLiteral("0 files")));
        QVERIFY(!page->isDone());
        // Nothing was written, and the page can try another backup.
        QVERIFY(QDir(target).isEmpty());
        QVERIFY(radio(page.get(), QStringLiteral("2026-11-12_201430"))->isEnabled());
    }

    void firmwarePartitionBoxAndItsConfirmation()
    {
        World w;
        QVERIFY(!w.backup(BackupKind::Keep, true, day(12)).isEmpty());
        const QString target = w.dir.filePath(QStringLiteral("fresh"));
        QDir().mkpath(target);
        std::unique_ptr<RestorePage> page(makePage(w));
        QStringList questions;
        bool answer = false;
        page->setAsker([&](const QString &, const QString &text) {
            questions << text;
            return answer;
        });
        page->setVolume(target);
        auto *box = page->findChild<QCheckBox *>(QStringLiteral("restoreFirmwarePartition"));
        QVERIFY(box);
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        // A Classic has no partition to write back: the box is not there.
        QVERIFY(!box->isVisible());
        FakePartition other;
        page->setFirmwarePartition(&other);
        QVERIFY(box->isVisible());
        QVERIFY(!box->isChecked());

        box->setChecked(true);
        button(page.get(), "restoreStart")->click();
        QCOMPARE(questions.size(), 1);
        QVERIFY(questions.first().contains(QStringLiteral("iPod Classic 120 GB")));
        QVERIFY(questions.first().contains(
            QStringLiteral("The firmware partition of iPod Classic 120 GB is written back from "
                           "this backup")));
        QVERIFY(other.restored.isEmpty());
        QVERIFY(!page->isRunning());

        answer = true;
        QSignalSpy finished(page.get(), &RestorePage::restoreFinished);
        button(page.get(), "restoreStart")->click();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString());
        QCOMPARE(other.restored.size(), 1);
    }

    void withoutTheBoxThePartitionStays()
    {
        World w;
        QVERIFY(!w.backup(BackupKind::Keep, true, day(12)).isEmpty());
        const QString target = w.dir.filePath(QStringLiteral("fresh"));
        QDir().mkpath(target);
        std::unique_ptr<RestorePage> page(makePage(w));
        FakePartition other;
        page->setFirmwarePartition(&other);
        page->setVolume(target);
        radio(page.get(), QStringLiteral("2026-11-12_201430"))->click();
        QSignalSpy finished(page.get(), &RestorePage::restoreFinished);
        button(page.get(), "restoreStart")->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(other.restored.isEmpty());
    }

    void noBackupAtAll()
    {
        World w;
        std::unique_ptr<RestorePage> page(makePage(w));
        QVERIFY(page->entries().isEmpty());
        QVERIFY(page->findChild<QLabel *>(QStringLiteral("backupNone")));
        QVERIFY(!button(page.get(), "restoreStart")->isEnabled());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TestBackupPage backup;
    TestRestorePage restore;
    return QTest::qExec(&backup, argc, argv) | QTest::qExec(&restore, argc, argv);
}

#include "tst_backuppage.moc"
