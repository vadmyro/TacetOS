// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md section 5 and 15.3: the backups of the clean
// install, of "Keep my music" and of an uninstall, and their restore. A
// temporary folder plays the music volume, with files of the fixtures of the
// tests; the firmware partition is a fake one, then the real helper on an image
// from end to end.

#include "backup.h"
#include "constants.h"
#include "fixtures.h"
#include "helperclient.h"
#include "runner.h"
#include "timeleft.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <atomic>

using namespace tacetos::wizard;
using tacetos::fakepod::FixtureId;
using tacetos::fakepod::Fixtures;
namespace device = tacetos::device;
namespace installer = tacetos::installer;

namespace {

const QString kSerial = QStringLiteral("8K7:12/AB");
const QString kModel = QStringLiteral("ipod-video");
const QDateTime kWhen(QDate(2026, 11, 15), QTime(18, 30, 5));
const QDateTime kOld(QDate(2024, 3, 2), QTime(10, 20, 30), QTimeZone::UTC);

QString fw()
{
    return QString(device::kFirmwareFolder);
}

QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(TACETOS_FIXTURES_DIR "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes, const QDateTime &modified = kOld)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.flush())
        return false;
    return f.setFileTime(modified, QFileDevice::FileModificationTime);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString sha(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// Every file of a folder with its bytes, to tell that nothing was written.
QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> all;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        all.insert(QDir(root).relativeFilePath(path), readFile(path));
    }
    return all;
}

class FakePartition : public FirmwarePartition
{
public:
    HelperAnswer backup(const QString &dataDir) override
    {
        ++backups;
        HelperAnswer answer;
        if (!failWith.isEmpty()) {
            answer.code = failWith;
            return answer;
        }
        const QString path = QDir(dataDir).filePath(QStringLiteral("firmware-partition.bin"));
        writeFile(path, bytes);
        answer.data.insert(QStringLiteral("path"), path);
        return answer;
    }
    HelperAnswer restore(const QString &file, const QString &) override
    {
        restored << file;
        return {};
    }

    QByteArray bytes = QByteArray(4096, 'F');
    QString failWith;
    int backups = 0;
    QStringList restored;
};

class FakeSources : public BackupSources
{
public:
    QString volumeRoot() const override { return volume; }
    QString volumeLabel() const override { return QStringLiteral("IPOD"); }
    QString backupRoot() const override { return root; }
    qint64 freeBytes(const QString &) const override { return free; }
    qint64 volumeUsedBytes() const override { return used; }
    FirmwarePartition *firmwarePartition() const override { return partition; }
    qint64 firmwarePartitionBytes() const override { return 4096; }
    QString appVersion() const override { return QStringLiteral("0.2.0"); }
    QDateTime now() const override { return kWhen; }
    installer::FileFault fault() const override { return injected; }

    QString volume;
    QString root;
    qint64 free = -1;
    qint64 used = 0;
    FirmwarePartition *partition = nullptr;
    installer::FileFault injected;
};

class TestContext : public StepContext
{
public:
    QString serial() const override { return kSerial; }
    QString model() const override { return kModel; }
    FlowShape shape() const override { return flow; }
    bool isResume() const override { return false; }
    bool stopRequested() const override { return stop; }
    void progress(const QString &stage, qint64 done, qint64) override
    {
        lastDone = done;
        if (stage == QLatin1String("copy"))
            dones << done;
    }
    UserAnswer waitForUser(WaitKind, const QJsonObject &) override { return {}; }
    QJsonValue fact(const QString &, const QString &key) const override { return facts.value(key); }
    void setFact(const QString &key, const QJsonValue &value) override { facts.insert(key, value); }
    QString backupFolder() const override { return folder; }
    void setBackupFolder(const QString &path) override { folder = path; }
    QString packageManifest() const override { return QString(); }
    QString packageSha256() const override { return QString(); }

    FlowShape flow{Family::Partition, FlowKind::Install, InstallPath::Keep, false};
    std::atomic<bool> stop{false};
    qint64 lastDone = 0;
    QList<qint64> dones;
    QJsonObject facts;
    QString folder;
};

class NoopStep : public Step
{
public:
    explicit NoopStep(QStringList *ran, const QString &name) : m_ran(ran), m_name(name) {}
    StepOutcome run(StepContext &) override
    {
        *m_ran << m_name;
        return StepOutcome::done();
    }

private:
    QStringList *m_ran;
    QString m_name;
};

} // namespace

class TstWizardBackup : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    // The helper keeps its locks in the runtime folder: never the real one.
    QTemporaryDir m_runtime;
    int m_count = 0;

    // A music volume with a firmware folder, playlists, music and a hidden
    // folder. Returns its path.
    QString makeVolume()
    {
        const QString v = m_tmp.filePath(QStringLiteral("volume%1").arg(++m_count));
        const QString f = v + QLatin1Char('/') + fw();
        writeFile(f + QStringLiteral("/config.cfg"), fixture("artists.tsv"));
        writeFile(f + QStringLiteral("/database_0.tcd"), QByteArray(300, 'd'));
        writeFile(f + QLatin1Char('/') + QString(device::kFirmwareInfoFile), "Version: 4\n");
        writeFile(f + QStringLiteral("/themes/Été/a/b/c/theme.cfg"),
                  fixture("reference-device-tree.txt"));
        writeFile(f + QStringLiteral("/WPS/empty.wps"), QByteArray());
        writeFile(f + QStringLiteral("/.hidden/note"), "hidden");
        writeFile(v + QStringLiteral("/Playlists/Été.m3u8"), fixture("README.md"));
        QDir().mkpath(v + QStringLiteral("/Playlists/empty"));
        writeFile(v + QStringLiteral("/Mix.M3U"), "a.mp3\n");
        writeFile(v + QStringLiteral("/Music/a.mp3"), QByteArray(5000, 'm'));
        writeFile(v + QStringLiteral("/notes.txt"), "not kept");
        return v;
    }

    FakeSources sources(const QString &volume)
    {
        FakeSources s;
        s.volume = volume;
        s.root = m_tmp.filePath(QStringLiteral("backups%1").arg(++m_count));
        return s;
    }

    static QStringList manifestPaths(const QString &folder)
    {
        QStringList paths;
        for (const BackupFile &f : readManifest(folder).manifest.files)
            paths << f.path;
        return paths;
    }

    // The volume of a clean install: the one of `makeVolume()`, plus names with
    // accents, an empty file, a deep folder, an empty folder, and hidden things
    // at the root.
    QString makeFullVolume()
    {
        const QString v = makeVolume();
        writeFile(v + QStringLiteral("/Music/Ça va/Öl/1/2/3/4/5/6/7/8/song.flac"),
                  fixture("reference-left-in-inbox.txt"));
        writeFile(v + QStringLiteral("/Music/empty.mp3"), QByteArray());
        writeFile(v + QStringLiteral("/Music/big.flac"), QByteArray(3 << 20, 'g'));
        writeFile(v + QStringLiteral("/.metadata"), "hidden at the root");
        writeFile(v + QStringLiteral("/.Trash-1000/files/old.mp3"), QByteArray(2000, 't'));
        QDir().mkpath(v + QStringLiteral("/Empty folder"));
        return v;
    }

    // A full backup of a Classic's volume, for the restore tests.
    QString fullBackupOf(const QString &volume)
    {
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        BackupStep step(BackupKind::Full, s);
        return step.run(context).kind == StepOutcome::Kind::Done ? context.folder : QString();
    }

    // What the format of a clean install leaves, as far as these tests go.
    static void emptyVolume(const QString &volume)
    {
        QDir(volume).removeRecursively();
        QDir().mkpath(volume);
    }

    static bool noPartFile(const QString &root)
    {
        const auto all = snapshot(root);
        return std::none_of(all.keyBegin(), all.keyEnd(), [](const QString &path) {
            return path.endsWith(QString(kPartSuffix));
        });
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(m_runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    void names()
    {
        QCOMPARE(serialFolderName(QStringLiteral("8K7:12/AB é-x_y")),
                 QStringLiteral("8K7_12_AB__-x_y"));
        QCOMPARE(dateFolderName(kWhen), QStringLiteral("2026-11-15_183005"));
        QVERIFY(defaultBackupRoot().endsWith(QString(kDefaultBackupFolder)));
    }

    void keepBackup()
    {
        const QString volume = makeVolume();
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));

        const QString folder = s.root + QStringLiteral("/8K7_12_AB/2026-11-15_183005");
        QCOMPARE(context.folder, folder);
        QCOMPARE(context.facts.value(QStringLiteral("manifest")).toString(),
                 folder + QLatin1Char('/') + QString(kManifestFile));
        QVERIFY(!QFile::exists(folder + QLatin1Char('/') + QString(kManifestPartialFile)));

        const QStringList expected = {
            fw() + QStringLiteral("/.hidden/note"),
            fw() + QStringLiteral("/WPS/empty.wps"),
            fw() + QStringLiteral("/themes/Été/a/b/c/theme.cfg"),
            fw() + QStringLiteral("/config.cfg"),
            fw() + QStringLiteral("/database_0.tcd"),
            fw() + QLatin1Char('/') + QString(device::kFirmwareInfoFile),
            QStringLiteral("Playlists/Été.m3u8"),
            QStringLiteral("Mix.M3U"),
        };
        QCOMPARE(manifestPaths(folder), expected);

        const ManifestRead read = readManifest(folder);
        QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
        QCOMPARE(read.manifest.kind, BackupKind::Keep);
        QCOMPARE(read.manifest.serial, kSerial);
        QCOMPARE(read.manifest.volumeLabel, QStringLiteral("IPOD"));
        QVERIFY(read.manifest.folders.contains(QStringLiteral("Playlists/empty")));
        QVERIFY(QFileInfo(backupVolumeFolder(folder) + QStringLiteral("/Playlists/empty")).isDir());
        for (const BackupFile &f : read.manifest.files) {
            const QString copy = backupVolumeFolder(folder) + QLatin1Char('/') + f.path;
            QCOMPARE(readFile(copy), readFile(volume + QLatin1Char('/') + f.path));
            QCOMPARE(f.sha256, sha(readFile(copy)));
            QCOMPARE(QFileInfo(copy).lastModified().toUTC(), kOld);
            QCOMPARE(f.modified, kOld);
        }
        QVERIFY(read.manifest.firmwarePartition);
        QCOMPARE(read.manifest.firmwarePartition->sha256, sha(partition.bytes));
        QCOMPARE(partition.backups, 1);
        QVERIFY(isVerifiedBackup(folder, kSerial));
        QVERIFY(!isVerifiedBackup(folder, QStringLiteral("other")));
        QVERIFY(!checkedFirmwarePartition(folder, kSerial).isEmpty());
        QCOMPARE(step.report().copied, expected.size());
    }

    void settingsBackup()
    {
        const QString volume = makeVolume();
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Settings, s);
        QCOMPARE(step.run(context).kind, StepOutcome::Kind::Done);
        const QStringList expected = {
            fw() + QStringLiteral("/WPS/empty.wps"),
            fw() + QStringLiteral("/themes/Été/a/b/c/theme.cfg"),
            fw() + QStringLiteral("/config.cfg"),
        };
        QCOMPARE(manifestPaths(context.folder), expected);
        QCOMPARE(readManifest(context.folder).manifest.kind, BackupKind::Settings);
    }

    void classicHasNoPartition()
    {
        FakeSources s = sources(makeVolume());
        TestContext context;
        context.flow.family = Family::Dfu;
        BackupStep step(BackupKind::Keep, s);
        QCOMPARE(step.run(context).kind, StepOutcome::Kind::Done);
        QVERIFY(!readManifest(context.folder).manifest.firmwarePartition);
        QVERIFY(checkedFirmwarePartition(context.folder, kSerial).isEmpty());
    }

    void partitionFailureWritesNoManifest()
    {
        FakePartition partition;
        partition.failWith = QStringLiteral("backup-failed");
        FakeSources s = sources(makeVolume());
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        const StepOutcome out = step.run(context);
        QCOMPARE(out.code, QStringLiteral("backup-failed"));
        QVERIFY(!QFile::exists(context.folder + QLatin1Char('/') + QString(kManifestFile)));
        QVERIFY(!isVerifiedBackup(context.folder, kSerial));
    }

    void noRoomBeforeCopy()
    {
        FakePartition partition;
        FakeSources s = sources(makeVolume());
        s.partition = &partition;
        s.free = 1000;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        QCOMPARE(step.run(context).code, QString(error::backupFullDisk));
        QVERIFY(snapshot(context.folder).isEmpty());
        QCOMPARE(partition.backups, 0);
    }

    void fullDuringCopy()
    {
        const QString volume = makeVolume();
        writeFile(volume + QStringLiteral("/Playlists/big.m3u"), QByteArray(3 << 20, 'b'));
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        // As the test program of the helper does: the disk fills up after the
        // first piece of a copy.
        s.injected = [](const QString &path, qint64 written) {
            return path.contains(QLatin1String("big.m3u")) && written > 0
                       ? QString::fromLatin1(installer::kNoSpace)
                       : QString();
        };
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        QCOMPARE(step.run(context).code, QString(error::backupFullDisk));
        const auto left = snapshot(context.folder);
        for (auto it = left.cbegin(); it != left.cend(); ++it)
            QVERIFY2(!it.key().endsWith(QString(kPartSuffix)), qPrintable(it.key()));
        QVERIFY(!QFile::exists(context.folder + QLatin1Char('/') + QString(kManifestFile)));
        QCOMPARE(partition.backups, 0);
    }

    void sourceChanged_data()
    {
        QTest::addColumn<bool>("always");
        QTest::addColumn<bool>("full");
        QTest::newRow("keep, once") << false << false;
        QTest::newRow("keep, twice") << true << false;
        QTest::newRow("full, once") << false << true;
        QTest::newRow("full, twice") << true << true;
    }

    void sourceChanged()
    {
        QFETCH(bool, always);
        QFETCH(bool, full);
        const BackupKind kind = full ? BackupKind::Full : BackupKind::Keep;
        const QString volume = makeVolume();
        const QString changing = volume + QStringLiteral("/Mix.M3U");
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        int changes = 0;
        s.injected = [&](const QString &path, qint64 written) {
            if (path.contains(QLatin1String("Mix.M3U")) && written == 0 &&
                (always || changes == 0)) {
                ++changes;
                writeFile(changing, QByteArray(10 + changes, 'x'));
            }
            return QString();
        };
        BackupStep step(kind, s);
        const StepOutcome out = step.run(context);
        if (always) {
            QCOMPARE(out.code, QString(error::sourceChanged));
            QCOMPARE(changes, 2);
        } else {
            QCOMPARE(out.kind, StepOutcome::Kind::Done);
            QCOMPARE(readFile(backupVolumeFolder(context.folder) + QStringLiteral("/Mix.M3U")),
                     QByteArray(11, 'x'));
        }
    }

    void cutThenResume()
    {
        const QString volume = makeVolume();
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        QStringList seen;
        s.injected = [&](const QString &path, qint64 written) {
            if (written == 0)
                seen << QFileInfo(path).fileName();
            if (seen.size() == 4)
                context.stop = true;
            return QString();
        };
        {
            BackupStep step(BackupKind::Keep, s);
            QCOMPARE(step.run(context).kind, StepOutcome::Kind::Cancelled);
            QCOMPARE(step.report().copied, 5);
        }
        const QString partial = context.folder + QLatin1Char('/') + QString(kManifestPartialFile);
        QVERIFY(QFile::exists(partial));
        const QStringList first = seen;

        seen.clear();
        context.stop = false;
        BackupStep again(BackupKind::Keep, s);
        QCOMPARE(again.run(context).kind, StepOutcome::Kind::Done);
        QCOMPARE(again.report().skipped, 5);
        for (const QString &name : first)
            QVERIFY2(!seen.contains(name), qPrintable(name));
        QCOMPARE(manifestPaths(context.folder).size(), 8);
        QVERIFY(!QFile::exists(partial));
        QVERIFY(isVerifiedBackup(context.folder, kSerial));
    }

    void unreadableFile()
    {
#ifdef Q_OS_WIN
        QSKIP("no read permission to take away on Windows");
#else
        const QString volume = makeVolume();
        const QString locked = volume + QStringLiteral("/Mix.M3U");
        QFile::setPermissions(locked, QFileDevice::Permissions());
        if (QFile(locked).open(QIODevice::ReadOnly))
            QSKIP("the file is still readable (run as root)");
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        BackupStep step(BackupKind::Keep, s);
        const StepOutcome out = step.run(context);
        QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(out.code, QString(error::backupReadFailed));
        QCOMPARE(out.detail, QStringLiteral("1"));
        QCOMPARE(step.report().unread, QStringList{QStringLiteral("Mix.M3U")});
        QVERIFY(!QFile::exists(context.folder + QLatin1Char('/') + QString(kManifestFile)));
#endif
    }

    void restore()
    {
        const QString volume = makeVolume();
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        QCOMPARE(step.run(context).kind, StepOutcome::Kind::Done);
        const auto before = snapshot(volume);

        const QString config = volume + QLatin1Char('/') + fw() + QStringLiteral("/config.cfg");
        writeFile(config, "changed");
        QFile::remove(volume + QStringLiteral("/Mix.M3U"));
        QDir(volume + QStringLiteral("/Playlists/empty")).removeRecursively();

        RestoreOptions options;
        options.firmwarePartition = true;
        const RestoreResult r =
            restoreFromBackup(context.folder, volume, kSerial, &partition, options);
        QVERIFY2(r.ok(), qPrintable(r.code + r.detail));
        QCOMPARE(r.restored, 8);
        QCOMPARE(snapshot(volume), before);
        QVERIFY(QFileInfo(volume + QStringLiteral("/Playlists/empty")).isDir());
        QCOMPARE(partition.restored.size(), 1);
        QVERIFY(partition.restored.first().startsWith(context.folder));
    }

    void restoreRefused_data()
    {
        QTest::addColumn<QString>("damage");
        QTest::addColumn<QString>("code");
        QTest::newRow("manifest not JSON") << "manifest" << QString(error::backupNotVerified);
        QTest::newRow("no manifest") << "no-manifest" << QString(error::backupNotVerified);
        QTest::newRow("file changed") << "file" << QString(error::backupDamaged);
        QTest::newRow("other iPod") << "serial" << QString(error::backupNotVerified);
    }

    void restoreRefused()
    {
        QFETCH(QString, damage);
        QFETCH(QString, code);
        const QString volume = makeVolume();
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        BackupStep step(BackupKind::Keep, s);
        QCOMPARE(step.run(context).kind, StepOutcome::Kind::Done);
        const QString manifest = context.folder + QLatin1Char('/') + QString(kManifestFile);
        if (damage == QLatin1String("manifest")) {
            writeFile(manifest, "{");
        } else if (damage == QLatin1String("no-manifest")) {
            QFile::remove(manifest);
        } else if (damage == QLatin1String("file")) {
            // The same size: only the sha256 can tell.
            const QString copy = backupVolumeFolder(context.folder) + QStringLiteral("/Mix.M3U");
            writeFile(copy, "b.mp3\n");
        }
        writeFile(volume + QStringLiteral("/Mix.M3U"), "mine\n");
        const auto before = snapshot(volume);
        const QString serial =
            damage == QLatin1String("serial") ? QStringLiteral("other") : kSerial;
        const RestoreResult r = restoreFromBackup(context.folder, volume, serial, nullptr);
        QCOMPARE(r.code, code);
        QCOMPARE(snapshot(volume), before);
    }

    // Section 5.3, `backup-full`: the whole volume as it is (D36).
    void fullBackup()
    {
        const QString volume = makeFullVolume();
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        s.used = 8 << 20;
        // Exactly the room of section 5.2.
        s.free = s.used + partition.bytes.size() + kBackupMargin;
        TestContext context;
        context.flow.path = InstallPath::Clean;
        BackupStep step(BackupKind::Full, s);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));

        const ManifestRead read = readManifest(context.folder);
        QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
        QCOMPARE(read.manifest.kind, BackupKind::Full);
        const auto source = snapshot(volume);
        QStringList paths = manifestPaths(context.folder);
        // Section 5.5: folders first, names sorted, hidden ones included.
        QCOMPARE(paths.first(), QStringLiteral(".Trash-1000/files/old.mp3"));
        QCOMPARE(paths.last(), QStringLiteral("notes.txt"));
        paths.sort();
        QCOMPARE(paths, source.keys());
        QCOMPARE(snapshot(backupVolumeFolder(context.folder)), source);
        for (const BackupFile &f : read.manifest.files) {
            const QString copy = backupVolumeFolder(context.folder) + QLatin1Char('/') + f.path;
            QCOMPARE(f.sha256, sha(readFile(copy)));
            QCOMPARE(QFileInfo(copy).lastModified().toUTC(), kOld);
        }
        for (const char *folder :
             {"Empty folder", "Playlists/empty", "Music/Ça va/Öl/1/2/3/4/5/6/7/8"}) {
            QVERIFY2(read.manifest.folders.contains(QString::fromUtf8(folder)), folder);
            QVERIFY(QFileInfo(backupVolumeFolder(context.folder) + QLatin1Char('/') +
                              QString::fromUtf8(folder))
                        .isDir());
        }
        QVERIFY(read.manifest.firmwarePartition);
        QVERIFY(isVerifiedFullBackup(context.folder, kSerial, Family::Partition));
        QCOMPARE(step.report().copied, source.size());
        // Bytes done and total, from 0 to every byte of the files.
        qint64 total = 0;
        for (const QByteArray &bytes : source)
            total += bytes.size();
        QCOMPARE(context.dones.first(), 0);
        QCOMPARE(context.dones.last(), total);
    }

    // Section 5.2: the room of a clean install is the used space of the
    // volume, plus the firmware partition, plus the margin.
    void fullNoRoom()
    {
        FakePartition partition;
        FakeSources s = sources(makeFullVolume());
        s.partition = &partition;
        s.used = 50LL * 1000 * 1000 * 1000;
        s.free = s.used + kBackupMargin;
        TestContext context;
        BackupStep step(BackupKind::Full, s);
        const StepOutcome out = step.run(context);
        QCOMPARE(out.code, QString(error::backupFullDisk));
        QCOMPARE(out.detail, QString::number(s.used + 4096 + kBackupMargin));
        QVERIFY(snapshot(context.folder).isEmpty());
        QCOMPARE(partition.backups, 0);

        QCOMPARE(backupSpaceNeeded(BackupKind::Full, 100, 7, 4), 104 + kBackupMargin);
        QCOMPARE(backupSpaceNeeded(BackupKind::Keep, 100, 7, 0), 7 + kBackupMargin);
    }

    // Section 2.2 rules 1 and 2 (D38): a path that cannot be taken is greyed
    // with its reason.
    void offer_data()
    {
        QTest::addColumn<bool>("classic");
        QTest::addColumn<bool>("mac");
        QTest::addColumn<qint64>("free");
        QTest::addColumn<int>("chosen");
        QTest::addColumn<QString>("cleanWhy");
        QTest::addColumn<QString>("keepWhy");
        const qint64 gb = 1000LL * 1000 * 1000;
        const QString mac = QStringLiteral("This iPod is formatted for Mac. TacetOS needs a "
                                           "Windows format, which erases the iPod");
        const QString clean40 =
            QStringLiteral("Not enough space on this computer: 92 GB needed, 40 GB free");
        QTest::newRow("room for both")
            << false << false << 100 * gb << int(InstallPath::Clean) << QString() << QString();
        QTest::newRow("no room for clean")
            << false << false << 40 * gb << int(InstallPath::Keep) << clean40 << QString();
        QTest::newRow("no room at all")
            << false << false << gb << int(InstallPath::None)
            << QStringLiteral("Not enough space on this computer: 92 GB needed, 1.0 GB free")
            << QStringLiteral("Not enough space on this computer: 2.0 GB needed, 1.0 GB free");
        QTest::newRow("mac") << false << true << 100 * gb << int(InstallPath::Clean) << QString()
                             << mac;
        QTest::newRow("mac, no room")
            << false << true << 40 * gb << int(InstallPath::None) << clean40 << mac;
        QTest::newRow("free not known")
            << false << false << qint64(-1) << int(InstallPath::Clean) << QString() << QString();
        // Section 6.4: an iPod Classic formatted for Mac takes no path yet.
        const QString classic = QStringLiteral(
            "This iPod Classic is formatted for Mac, which TacetOS cannot change yet. Restore it "
            "with Apple's software on a Windows computer first");
        QTest::newRow("Classic, mac")
            << true << true << 100 * gb << int(InstallPath::None) << classic << classic;
        QTest::newRow("Classic") << true << false << 100 * gb << int(InstallPath::Clean)
                                 << QString() << QString();
    }

    void offer()
    {
        QFETCH(bool, classic);
        QFETCH(bool, mac);
        QFETCH(qint64, free);
        QFETCH(int, chosen);
        QFETCH(QString, cleanWhy);
        QFETCH(QString, keepWhy);
        const qint64 gb = 1000LL * 1000 * 1000;
        const PathOffer offer =
            offerPaths(classic ? Family::Dfu : Family::Partition, mac, 92 * gb, 2 * gb, free);
        QCOMPARE(int(offer.chosen), chosen);
        QCOMPARE(offer.clean, cleanWhy.isEmpty());
        QCOMPARE(offer.keep, keepWhy.isEmpty());
        QCOMPARE(offer.cleanWhy, cleanWhy);
        QCOMPARE(offer.keepWhy, keepWhy);
    }

    // Sections 2.2 rule 3 and 6.1: nothing is erased without a verified full
    // backup of this iPod.
    void eraseNeedsFullBackup()
    {
        const QString volume = makeFullVolume();
        FakePartition partition;
        QString why;
        QVERIFY(!isVerifiedFullBackup(m_tmp.filePath(QStringLiteral("nowhere")), kSerial,
                                      Family::Dfu, &why));
        QCOMPARE(why, QStringLiteral("no manifest"));

        FakeSources keepSources = sources(volume);
        keepSources.partition = &partition;
        TestContext keep;
        QCOMPARE(BackupStep(BackupKind::Keep, keepSources).run(keep).kind, StepOutcome::Kind::Done);
        QVERIFY(isVerifiedBackup(keep.folder, kSerial));
        QVERIFY(!isVerifiedFullBackup(keep.folder, kSerial, Family::Partition, &why));
        QCOMPARE(why, QStringLiteral("the backup is not of the whole volume"));

        // A Classic's backup has no firmware partition: enough for a Classic only.
        const QString classic = fullBackupOf(volume);
        QVERIFY(isVerifiedFullBackup(classic, kSerial, Family::Dfu));
        QVERIFY(!isVerifiedFullBackup(classic, kSerial, Family::Partition, &why));
        QCOMPARE(why, QStringLiteral("the backup holds no firmware partition"));

        FakeSources fullSources = sources(volume);
        fullSources.partition = &partition;
        TestContext full;
        QCOMPARE(BackupStep(BackupKind::Full, fullSources).run(full).kind, StepOutcome::Kind::Done);
        QVERIFY(isVerifiedFullBackup(full.folder, kSerial, Family::Partition));
        QVERIFY(!isVerifiedFullBackup(full.folder, QStringLiteral("other"), Family::Partition));

        // A file of the copy gone, then the manifest gone (a cut copy has only
        // `manifest.partial`).
        QVERIFY(QFile::remove(backupVolumeFolder(full.folder) + QStringLiteral("/Music/a.mp3")));
        QVERIFY(!isVerifiedFullBackup(full.folder, kSerial, Family::Partition, &why));
        QCOMPARE(why, QStringLiteral("a file of the backup is missing or has another size"));
        QVERIFY(QFile::remove(full.folder + QLatin1Char('/') + QString(kManifestFile)));
        writeFile(full.folder + QLatin1Char('/') + QString(kManifestPartialFile), "{}\n");
        QVERIFY(!isVerifiedFullBackup(full.folder, kSerial, Family::Partition));
    }

    // Section 5.5: a cut, then the resume. The files already verified are not
    // copied again, a `.part` file left by the cut is copied again, and the
    // progress starts from the bytes already there.
    void fullCutThenResume()
    {
        const QString volume = makeFullVolume();
        FakeSources s = sources(volume);
        TestContext context;
        context.flow.family = Family::Dfu;
        QStringList seen;
        bool cut = true;
        s.injected = [&](const QString &path, qint64 written) {
            if (written == 0)
                seen << QFileInfo(path).fileName();
            if (cut && seen.size() == 6)
                context.stop = true;
            return QString();
        };
        int firstCopied = 0;
        {
            BackupStep step(BackupKind::Full, s);
            QCOMPARE(step.run(context).kind, StepOutcome::Kind::Cancelled);
            firstCopied = step.report().copied;
        }
        QVERIFY(firstCopied > 0);
        QVERIFY(noPartFile(context.folder));
        const QString partial = context.folder + QLatin1Char('/') + QString(kManifestPartialFile);
        qint64 doneBytes = 0;
        QFile lines(partial);
        QVERIFY(lines.open(QIODevice::ReadOnly));
        while (!lines.atEnd())
            doneBytes += QJsonDocument::fromJson(lines.readLine())
                             .object()
                             .value(QStringLiteral("size"))
                             .toInteger();
        lines.close();
        // A process killed in the middle of a copy leaves its `.part` file.
        writeFile(backupVolumeFolder(context.folder) + QStringLiteral("/notes.txt.part"), "cut");
        const QStringList first = seen;

        seen.clear();
        cut = false;
        context.stop = false;
        context.dones.clear();
        BackupStep again(BackupKind::Full, s);
        QCOMPARE(again.run(context).kind, StepOutcome::Kind::Done);
        QCOMPARE(again.report().skipped, firstCopied);
        QCOMPARE(context.dones.first(), doneBytes);
        for (const QString &name : first)
            QVERIFY2(!seen.contains(name), qPrintable(name));
        QVERIFY(noPartFile(context.folder));
        QCOMPARE(snapshot(backupVolumeFolder(context.folder)), snapshot(volume));
        QVERIFY(isVerifiedFullBackup(context.folder, kSerial, Family::Dfu));
    }

    // Section 5.7: "Put my music back" after a clean install. The firmware
    // folder of the backup is the old one: TacetOS keeps its own.
    void putMusicBack()
    {
        const QString volume = makeFullVolume();
        const auto before = snapshot(volume);
        const QString folder = fullBackupOf(volume);
        QVERIFY(!folder.isEmpty());

        emptyVolume(volume);
        const QString config = fw() + QStringLiteral("/config.cfg");
        writeFile(volume + QLatin1Char('/') + config, "new install");
        QList<QPair<qint64, qint64>> steps;
        RestoreOptions options;
        options.progress = [&](qint64 done, qint64 total) { steps << qMakePair(done, total); };
        const RestoreResult r = restoreFromBackup(folder, volume, kSerial, nullptr, options);
        QVERIFY2(r.ok(), qPrintable(r.code + r.detail));

        auto expected = before;
        for (auto it = expected.begin(); it != expected.end();)
            it = it.key().startsWith(fw() + QLatin1Char('/')) ? expected.erase(it) : std::next(it);
        QCOMPARE(r.restored, expected.size());
        expected.insert(config, "new install");
        QCOMPARE(snapshot(volume), expected);
        QVERIFY(QFileInfo(volume + QStringLiteral("/Empty folder")).isDir());
        QVERIFY(QFileInfo(volume + QStringLiteral("/Playlists/empty")).isDir());
        QCOMPARE(QFileInfo(volume + QStringLiteral("/Music/big.flac")).lastModified().toUTC(),
                 kOld);
        QCOMPARE(steps.first().first, 0);
        QCOMPARE(steps.last().first, steps.last().second);

        // Once more: every file is there already, with the same sha256.
        const RestoreResult again = restoreFromBackup(folder, volume, kSerial, nullptr);
        QVERIFY2(again.ok(), qPrintable(again.code + again.detail));
        QCOMPARE(again.restored, 0);
        QCOMPARE(again.skipped, r.restored);
        QCOMPARE(snapshot(volume), expected);
    }

    // With no firmware folder on the iPod, the whole backup goes back.
    void putEverythingBack()
    {
        const QString volume = makeFullVolume();
        const auto before = snapshot(volume);
        const QString folder = fullBackupOf(volume);
        emptyVolume(volume);
        const RestoreResult r = restoreFromBackup(folder, volume, kSerial, nullptr);
        QVERIFY2(r.ok(), qPrintable(r.code + r.detail));
        QCOMPARE(r.restored, before.size());
        QCOMPARE(snapshot(volume), before);
    }

    // A damaged file is never placed: the restore stops there and says how
    // many files were put back.
    void putMusicBackDamaged()
    {
        const QString volume = makeFullVolume();
        const QString folder = fullBackupOf(volume);
        const QString damaged = QStringLiteral("Music/a.mp3");
        // The same size: only the sha256 can tell.
        writeFile(backupVolumeFolder(folder) + QLatin1Char('/') + damaged, QByteArray(5000, 'x'));
        emptyVolume(volume);
        const RestoreResult r = restoreFromBackup(folder, volume, kSerial, nullptr);
        QCOMPARE(r.code, QString(error::backupDamaged));
        QVERIFY(r.restored > 0);
        QCOMPARE(r.detail, QString::number(r.restored));
        QVERIFY(!QFile::exists(volume + QLatin1Char('/') + damaged));
        QVERIFY(noPartFile(volume));
        QCOMPARE(snapshot(volume).size(), r.restored);
    }

    void putMusicBackStopped_data()
    {
        QTest::addColumn<bool>("full");
        QTest::addColumn<QString>("code");
        QTest::newRow("cancel") << false << QString(error::cancelled);
        QTest::newRow("iPod full") << true << QString(error::restoreWriteFailed);
    }

    // A cancel and an iPod that fills up, in the middle of a big file.
    void putMusicBackStopped()
    {
        QFETCH(bool, full);
        QFETCH(QString, code);
        const QString volume = makeFullVolume();
        const QString folder = fullBackupOf(volume);
        emptyVolume(volume);
        std::atomic<bool> stop{false};
        RestoreOptions options;
        options.stop = [&stop] { return stop.load(); };
        options.fault = [&](const QString &path, qint64 written) {
            if (!path.contains(QLatin1String("big.flac")) || written == 0)
                return QString();
            if (full)
                return QString::fromLatin1(installer::kNoSpace);
            stop = true;
            return QString();
        };
        const RestoreResult r = restoreFromBackup(folder, volume, kSerial, nullptr, options);
        QCOMPARE(r.code, code);
        QCOMPARE(r.detail, QString::number(r.restored));
        QVERIFY(!QFile::exists(volume + QStringLiteral("/Music/big.flac")));
        QVERIFY(noPartFile(volume));
    }

    // Section 5.5: the time left, from the speed of the last 10 seconds.
    void timeLeft()
    {
        TimeLeft t;
        QCOMPARE(t.secondsLeft(1000), -1);
        t.add(0, 0);
        t.add(500, 50);
        // Under a second, the speed is not known yet.
        QCOMPARE(t.speed(), -1.0);
        t.add(1000, 100);
        QCOMPARE(t.speed(), 100.0);
        QCOMPARE(t.secondsLeft(1000), 9);
        // Ten bytes a second from then on: after 10 seconds, only that counts.
        for (qint64 ms = 2000; ms <= 11000; ms += 1000)
            t.add(ms, 100 + (ms - 1000) / 100);
        QCOMPARE(t.speed(), 10.0);
        QCOMPARE(t.secondsLeft(1000), 80);
        QCOMPARE(t.secondsLeft(200), 0);
        // A new copy starts from nothing.
        t.add(12000, 0);
        QCOMPARE(t.speed(), -1.0);
        // A resume starts from the bytes already there, not from 0.
        TimeLeft resumed;
        resumed.add(0, 5000000000);
        resumed.add(2000, 5002000000);
        QCOMPARE(resumed.speed(), 1000000.0);
        QCOMPARE(resumed.secondsLeft(6000000000), 998);
    }

    // Section 5.6: a failed backup stops the flow before any step that writes.
    void runnerStopsBeforeWrites()
    {
        const QString volume = makeVolume();
        writeFile(volume + QStringLiteral("/Playlists/big.m3u"), QByteArray(3 << 20, 'b'));
        FakePartition partition;
        FakeSources s = sources(volume);
        s.partition = &partition;
        s.injected = [](const QString &, qint64 written) {
            return written > 0 ? QString::fromLatin1(installer::kNoSpace) : QString();
        };
        QStringList ran;
        WizardRunner runner(m_tmp.filePath(QStringLiteral("data")),
                            [&](const QString &name) -> std::unique_ptr<Step> {
                                if (name == step::backupKeep)
                                    return std::make_unique<BackupStep>(BackupKind::Keep, s);
                                return std::make_unique<NoopStep>(&ran, name);
                            });
        QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
        WizardRunner::Start start;
        start.serial = kSerial;
        start.model = kModel;
        start.shape = {Family::Partition, FlowKind::Install, InstallPath::Keep, false};
        QCOMPARE(runner.start(start), QString());
        QVERIFY(stopped.wait(20000));
        QCOMPARE(stopped.first().first().toString(), QString(error::backupFullDisk));
        QCOMPARE(ran, QStringList{QString(step::diagnose)});
    }

    // From end to end: the client, the helper program, an image of an older
    // iPod. The step runs on another thread, as in the runner.
    void helperEndToEnd()
    {
        const QString image = m_tmp.filePath(QStringLiteral("video.img"));
        const auto built = Fixtures::build(FixtureId::DosIpodVideo, image);
        QVERIFY2(built.ok, qPrintable(built.error));
        const QByteArray original = readFile(image);

        tacetos::helper::HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        HelperFirmwarePartition partition(
            client, tacetos::helper::HelperClient::imageTarget(image, built.pod.sectorSize));
        FakeSources s = sources(makeVolume());
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);

        StepOutcome out;
        std::atomic<bool> finished{false};
        QThread *worker = QThread::create([&] {
            out = step.run(context);
            finished = true;
        });
        worker->start();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 60000);
        worker->wait();
        delete worker;
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        const QString copy = checkedFirmwarePartition(context.folder, kSerial);
        QVERIFY(!copy.isEmpty());
        QVERIFY(QFileInfo(copy).size() > 0);

        RestoreResult r;
        finished = false;
        worker = QThread::create([&] {
            RestoreOptions options;
            options.firmwarePartition = true;
            r = restoreFromBackup(context.folder, s.volume, kSerial, &partition, options);
            finished = true;
        });
        worker->start();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 60000);
        worker->wait();
        delete worker;
        QVERIFY2(r.ok(), qPrintable(r.code + r.detail));
        QCOMPARE(readFile(image), original);
        client.shutdown();
    }

    // The same way of making a folder full as the tests of the helper.
    void helperFullFolder()
    {
#ifndef TACETOS_HELPER_KILLTEST_PROGRAM
        QSKIP("the test program of the helper is built on Unix only");
#else
        const QString image = m_tmp.filePath(QStringLiteral("full.img"));
        const auto built = Fixtures::build(FixtureId::DosIpodVideo, image);
        QVERIFY(built.ok);
        tacetos::helper::HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("TACETOS_TEST_FULL"), QStringLiteral("backup"));
        client.setEnvironment(env);
        HelperFirmwarePartition partition(
            client, tacetos::helper::HelperClient::imageTarget(image, built.pod.sectorSize));
        FakeSources s = sources(makeVolume());
        s.partition = &partition;
        TestContext context;
        BackupStep step(BackupKind::Keep, s);
        // On the thread of the client this time: the call runs its own loop.
        const StepOutcome out = step.run(context);
        QCOMPARE(out.kind, StepOutcome::Kind::Failed);
        QCOMPARE(out.code, QStringLiteral("backup-failed"));
        QVERIFY(!QFile::exists(context.folder + QLatin1Char('/') + QString(kManifestFile)));
        client.shutdown();
#endif
    }
};

QTEST_GUILESS_MAIN(TstWizardBackup)
#include "tst_wizard_backup.moc"
