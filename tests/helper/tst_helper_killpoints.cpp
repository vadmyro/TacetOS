// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md section 15.2 and docs/specs/bootloader-legacy.md section
// 19: the helper is killed (SIGKILL) at chosen points of each write command. The
// state read afterwards is one of section 19, the backup is still there, and
// writing it back gives the original image. The program with the kill points is
// `tacet-helper-killtest`; the normal helper has none (a test below looks for it).

#include "bootloaderfile.h"
#include "crashrecovery.h"
#include "fakepod.h"
#include "helperclient.h"
#include "helperconstants.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using tacetos::fakepod::BootloaderFile;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodResult;
using tacetos::fakepod::FakePodSpec;
using tacetos::fakepod::TableKind;
using namespace tacetos::helper;

namespace {

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

// The size, then each block that is not all zeros with its place: two files have
// the same hash when they have the same bytes. The large images are mostly zeros,
// and hashing those blocks took most of the time of the test.
QByteArray fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    const qint64 size = f.size();
    h.addData(QByteArrayView(reinterpret_cast<const char *>(&size), sizeof size));
    constexpr qint64 kBlock = 64 * 1024;
    const QByteArray zeros(kBlock, '\0');
    for (qint64 at = 0; !f.atEnd(); at += kBlock) {
        const QByteArray block = f.read(kBlock);
        if (block.isEmpty())
            return {};
        if (block == QByteArrayView(zeros).first(block.size()))
            continue;
        h.addData(QByteArrayView(reinterpret_cast<const char *>(&at), sizeof at));
        h.addData(block);
    }
    return h.result();
}

QByteArray bytesAt(const QString &path, qint64 offset, qint64 length)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return {};
    return f.read(length);
}

struct Call
{
    QJsonObject result;
    int finishedCount = 0;
    bool ok() const { return result.value(QStringLiteral("ok")).toBool(); }
    QJsonObject data() const { return result.value(QStringLiteral("data")).toObject(); }
    QString error() const { return str(result, "error"); }
};

// What one run of a command, with a kill point, left behind.
struct Cut
{
    Call call;
    FakePodResult pod;
    QString image;
    QString dir;
    // The whole image, and the firmware partition, as they were before the
    // command (for the restore scenario: before the install that made the backup).
    QByteArray originalHash;
    QByteArray firmware;
    QByteArray head;
    // The backup of the setup of the restore scenario.
    QString setupBackup;
    QByteArray setupBackupHash;
    bool killed() const { return call.error() == QString(err::helperCrashed); }
};

struct Scenario
{
    QString model;
    TableKind table = TableKind::Dos;
    bool bootloader = false;
    QString command;
};

const QStringList kPoints = {QStringLiteral("read-before"), QStringLiteral("write-before"),
                             QStringLiteral("write-torn"), QStringLiteral("write-after"),
                             QStringLiteral("flush-after")};

} // namespace

class TstHelperKillpoints : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_runtime;
    int m_count = 0;

    FakePodResult build(const Scenario &sc, QString *path)
    {
        FakePodSpec spec;
        spec.modelId = sc.model;
        spec.path = m_dir.filePath(QStringLiteral("disk%1.img").arg(++m_count));
        spec.bootloaderPresent = sc.bootloader;
        spec.table = sc.table;
        *path = spec.path;
        FakePodResult r = FakePod::build(spec);
        if (r.ok && r.sectorSize == 512)
            r.ok = growMusic(spec.path, r.musicStart / 512, sc.table);
        return r;
    }

    // As in tst_helper_commands: a music partition large enough for FAT32, sparse.
    static bool growMusic(const QString &path, qint64 musicFirst, TableKind table)
    {
        constexpr qint64 kMusicTotal = 600 * 1024 * 2;
        QFile f(path);
        if (!f.open(QIODevice::ReadWrite) || !f.resize((musicFirst + kMusicTotal) * 512))
            return false;
        QByteArray count(4, '\0');
        for (int k = 0; k < 4; ++k) {
            const int shift = table == TableKind::Apm ? 24 - 8 * k : 8 * k;
            count[k] = char((quint32(kMusicTotal) >> shift) & 0xFF);
        }
        f.seek(table == TableKind::Apm ? 1536 + 12 : 0x1BE + 16 + 12);
        return f.write(count) == 4;
    }

    HelperClient *newClient(const QString &program, const QString &kill = QString())
    {
        auto *client = new HelperClient(this);
        client->setProgram(program);
        if (!kill.isEmpty()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_KILL"), kill);
            client->setEnvironment(env);
        }
        return client;
    }

    static QString normalProgram() { return QStringLiteral(TACETOS_HELPER_PROGRAM); }
    static QString killProgram() { return QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM); }

    Call call(HelperClient &client, const QString &cmd, const QJsonObject &args)
    {
        Call c;
        QSignalSpy done(&client, &HelperClient::finished);
        client.request(cmd, args);
        if (!done.wait(60000))
            return c;
        c.result = done.first().at(1).toJsonObject();
        c.finishedCount = int(done.size());
        return c;
    }

    static QJsonObject target(const QString &image, int sectorSize)
    {
        return HelperClient::imageTarget(image, sectorSize);
    }

    QJsonObject scan(const QString &image, int sectorSize)
    {
        QScopedPointer<HelperClient> client(newClient(normalProgram()));
        QJsonObject a;
        a.insert(QStringLiteral("target"), target(image, sectorSize));
        const Call c = call(*client, QString(cmd::readState), a);
        return c.ok() ? c.data() : QJsonObject();
    }

    QString writeBootloader(const QString &targetName)
    {
        const QString path = m_dir.filePath(QStringLiteral("boot%1.bin").arg(++m_count));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(BootloaderFile::build(targetName)) <= 0)
            return {};
        return path;
    }

    // The arguments of the command of the scenario on the image as it is now.
    QJsonObject commandArgs(const Scenario &sc, const Cut &cut, const QJsonObject &state)
    {
        QJsonObject a;
        a.insert(QStringLiteral("target"), target(cut.image, cut.pod.sectorSize));
        a.insert(QStringLiteral("scanToken"), str(state, "scanToken"));
        // The format has no data folder (nothing is backed up).
        if (sc.command != cmd::formatMusic)
            a.insert(QStringLiteral("dataDir"), cut.dir);
        if (sc.command == cmd::installBootloader || sc.command == cmd::updateBootloader ||
            sc.command == cmd::uninstallBootloader) {
            a.insert(QStringLiteral("expectedState"), sc.command == cmd::installBootloader
                                                          ? QStringLiteral("clean")
                                                          : QStringLiteral("installed"));
            if (sc.command != cmd::uninstallBootloader) {
                a.insert(QStringLiteral("bootloaderFile"), writeBootloader(str(state, "target")));
                a.insert(QStringLiteral("bootloaderVersion"), QStringLiteral("0.1.0"));
            }
        } else if (sc.command == cmd::restoreBackup) {
            a.insert(QStringLiteral("backupFile"), cut.setupBackup);
        } else {
            a.insert(QStringLiteral("ipodName"), QStringLiteral("Test iPod"));
            a.insert(QStringLiteral("typedName"), QStringLiteral("Test iPod"));
            a.insert(QStringLiteral("volumeLabel"), QStringLiteral("TESTPOD"));
        }
        return a;
    }

    // Builds a fresh image, and runs the command of the scenario on it with the
    // kill point `kill` ("point:n", empty for none).
    bool run(const Scenario &sc, const QString &kill, Cut *cut)
    {
        cut->pod = build(sc, &cut->image);
        if (!cut->pod.ok)
            return false;
        cut->dir = m_dir.filePath(QStringLiteral("data%1").arg(++m_count));
        cut->originalHash = fileHash(cut->image);
        cut->firmware = bytesAt(cut->image, cut->pod.firmwareStart, cut->pod.firmwareBytes);
        cut->head = bytesAt(cut->image, 0, cut->pod.firmwareStart);
        const int ss = cut->pod.sectorSize;

        if (sc.command == cmd::restoreBackup) {
            // The setup: an install, which leaves the backup that is to be written back.
            Scenario install = sc;
            install.command = QString(cmd::installBootloader);
            const QJsonObject state = scan(cut->image, ss);
            QScopedPointer<HelperClient> client(newClient(normalProgram()));
            const Call c = call(*client, install.command, commandArgs(install, *cut, state));
            if (!c.ok())
                return false;
            cut->setupBackup = str(c.data(), "backup");
            cut->setupBackupHash = fileHash(cut->setupBackup);
        }
        const QJsonObject state = scan(cut->image, ss);
        if (state.isEmpty())
            return false;
        QScopedPointer<HelperClient> client(kill.isEmpty() ? newClient(normalProgram())
                                                           : newClient(killProgram(), kill));
        cut->call = call(*client, sc.command, commandArgs(sc, *cut, state));
        // The app starts nothing by itself after the end of the helper.
        if (cut->killed()) {
            if (client->isRunning())
                return false;
        }
        return true;
    }

    // The smallest `n` for which the point is not reached `n` times: the point is
    // reached `n - 1` times by the command.
    int timesReached(const Scenario &sc, const QString &point)
    {
        int low = 0; // killed at `low` (or 0)
        int high = 0;
        for (int n = 1;; n *= 2) {
            Cut cut;
            if (!run(sc, QStringLiteral("%1:%2").arg(point).arg(n), &cut))
                return -1;
            if (!cut.killed()) {
                if (!cut.call.ok())
                    return -1;
                high = n;
                break;
            }
            low = n;
            if (n > (1 << 16))
                return -1;
        }
        while (high - low > 1) {
            const int mid = (low + high) / 2;
            Cut cut;
            if (!run(sc, QStringLiteral("%1:%2").arg(point).arg(mid), &cut))
                return -1;
            (cut.killed() ? low : high) = mid;
        }
        return low;
    }

    // Every n up to `head`, then `head` more spread to the end (the last one too).
    static QList<int> picks(int times, int head)
    {
        QList<int> out;
        for (int n = 1; n <= times && n <= head; ++n)
            out.append(n);
        if (times > head) {
            const int step = qMax(1, (times - head) / head);
            for (int n = head + step; n < times; n += step)
                out.append(n);
            out.append(times);
        }
        return out;
    }

    // The files of the data folder that are copies of the original.
    QStringList backupsOf(const Cut &cut)
    {
        QStringList out;
        const QDir d(cut.dir);
        for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) {
            if (fi.size() == cut.pod.firmwareBytes &&
                bytesAt(fi.filePath(), 0, fi.size()) == cut.firmware)
                out.append(fi.filePath());
        }
        return out;
    }

    void checkAfterCut(const Scenario &sc, const QString &where, const Cut &cut, int n,
                       const QString &point)
    {
        QVERIFY2(cut.killed(), qPrintable(where + ": " + str(cut.call.result, "error")));
        QCOMPARE(cut.call.finishedCount, 1);
        const int ss = cut.pod.sectorSize;
        const bool boot = sc.command == cmd::installBootloader ||
                          sc.command == cmd::updateBootloader ||
                          sc.command == cmd::uninstallBootloader;
        const bool changed = fileHash(cut.image) != cut.originalHash;
        // A cut before the first write of an operation: the image is as it was.
        if (point == QLatin1String("write-before") && n == 1 && sc.command != cmd::restoreBackup)
            QVERIFY2(!changed, qPrintable(where));

        // The state read again, by a new helper.
        const QJsonObject state = scan(cut.image, ss);
        QVERIFY2(!state.isEmpty(), qPrintable(where + ": the state cannot be read"));
        const QString now = str(state, "state");
        QVERIFY2(now == QLatin1String("clean") || now == QLatin1String("installed") ||
                     now == QLatin1String("inconsistent"),
                 qPrintable(where + ": " + now));

        const QStringList backups = backupsOf(cut);
        if (boot || sc.command == cmd::convertToFat32) {
            // The backup is made before the first write, and is still there, intact.
            if (changed)
                QVERIFY2(!backups.isEmpty(), qPrintable(where + ": no backup"));
        }
        if (boot) {
            // Section 19: a cut between two writes is a failed write. Only a torn
            // write of a directory sector could leave something else than the old
            // state or the new one.
            if (point != QLatin1String("write-torn"))
                QVERIFY2(now != QLatin1String("inconsistent"), qPrintable(where));
            if (sc.command == cmd::installBootloader && point == QLatin1String("write-before"))
                QCOMPARE(now, QStringLiteral("clean")); // the directory is the last write
            if (sc.command == cmd::uninstallBootloader && point == QLatin1String("write-before") &&
                n == 1)
                QCOMPARE(now, QStringLiteral("installed"));
        }

        // What the app says, from the state read: no second operation is started.
        const CrashRecovery rec = recoveryAfterCrash(now, !backups.isEmpty());
        if (now == QLatin1String("inconsistent")) {
            QCOMPARE(rec.next, QStringLiteral("offer-restore"));
        } else {
            QCOMPARE(rec.next, QStringLiteral("report-state"));
            QVERIFY(rec.message.contains(QStringLiteral("in order")));
        }

        if (boot || sc.command == cmd::restoreBackup) {
            const QString backup =
                sc.command == cmd::restoreBackup ? cut.setupBackup : backups.value(0);
            if (changed || sc.command == cmd::restoreBackup) {
                QVERIFY2(!backup.isEmpty(), qPrintable(where));
                // The way back to the original image works.
                QJsonObject a;
                a.insert(QStringLiteral("target"), target(cut.image, ss));
                a.insert(QStringLiteral("scanToken"), str(state, "scanToken"));
                a.insert(QStringLiteral("dataDir"), cut.dir);
                a.insert(QStringLiteral("backupFile"), backup);
                QScopedPointer<HelperClient> client(newClient(normalProgram()));
                const Call r = call(*client, QString(cmd::restoreBackup), a);
                QVERIFY2(r.ok(), qPrintable(where + ": " + QJsonDocument(r.result).toJson()));
                QVERIFY2(fileHash(cut.image) == cut.originalHash, qPrintable(where));
                QVERIFY2(QFileInfo::exists(backup), qPrintable(where));
                if (sc.command == cmd::restoreBackup)
                    QCOMPARE(fileHash(backup), cut.setupBackupHash);
            }
        } else {
            // Formatting and conversion never touch the table of the firmware
            // partition, and the partition itself.
            QCOMPARE(bytesAt(cut.image, cut.pod.firmwareStart, cut.pod.firmwareBytes),
                     cut.firmware);
            if (sc.command == cmd::formatMusic) {
                QCOMPARE(bytesAt(cut.image, 0, cut.pod.firmwareStart), cut.head);
                QCOMPARE(str(state, "table"), QStringLiteral("dos"));
            } else {
                const QString table = str(state, "table");
                QVERIFY2(table == QLatin1String("apple") || table == QLatin1String("dos"),
                         qPrintable(where));
                if (changed) {
                    // The map before the change is in the data folder, whole.
                    bool mapKept = false;
                    for (const QFileInfo &fi : QDir(cut.dir).entryInfoList(QDir::Files))
                        mapKept = mapKept || (fi.size() == cut.pod.firmwareStart &&
                                              bytesAt(fi.filePath(), 0, fi.size()) == cut.head);
                    QVERIFY2(mapKept, qPrintable(where + ": no copy of the map"));
                }
            }
            // The command can be run again, whatever the point was.
            if (sc.command == cmd::formatMusic || str(state, "table") == QLatin1String("apple")) {
                Cut again = cut;
                QScopedPointer<HelperClient> client(newClient(normalProgram()));
                const Call r = call(*client, sc.command, commandArgs(sc, again, state));
                QVERIFY2(r.ok(), qPrintable(where + ": " + QJsonDocument(r.result).toJson()));
                QCOMPARE(bytesAt(cut.image, cut.pod.firmwareStart, cut.pod.firmwareBytes),
                         cut.firmware);
                if (sc.command == cmd::formatMusic)
                    QCOMPARE(bytesAt(cut.image, cut.pod.musicStart + 0x52, 8),
                             QByteArray("FAT32   "));
            }
        }
    }

    void cutAtEveryPoint(const Scenario &sc)
    {
        for (const QString &point : kPoints) {
            const int times = timesReached(sc, point);
            QVERIFY2(times >= 0, qPrintable(point));
            // A command that writes reaches every point of the list at least once.
            QVERIFY2(times >= 1, qPrintable(point));
            qInfo("%s %s: reached %d times", qPrintable(sc.command), qPrintable(point), times);
            for (const int n : picks(times, point == QLatin1String("read-before") ? 4 : 8)) {
                const QString where =
                    QStringLiteral("%1 %2:%3 of %4").arg(sc.command, point).arg(n).arg(times);
                Cut cut;
                QVERIFY2(run(sc, QStringLiteral("%1:%2").arg(point).arg(n), &cut),
                         qPrintable(where));
                checkAfterCut(sc, where, cut, n, point);
                if (QTest::currentTestFailed())
                    return;
            }
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_runtime.isValid());
        // A killed helper leaves its lock, which the next one finds stale.
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    void onlyTheTestProgramHasKillPoints()
    {
        const QByteArray name("TACETOS_TEST_KILL");
        QFile normal(normalProgram());
        QVERIFY(normal.open(QIODevice::ReadOnly));
        QVERIFY2(!normal.readAll().contains(name), "the normal helper has a kill point");
        QFile test(killProgram());
        QVERIFY(test.open(QIODevice::ReadOnly));
        QVERIFY(test.readAll().contains(name));

        // The variable is not read by the normal helper: the command ends right.
        Scenario sc{QStringLiteral("ipod-video"), TableKind::Dos, false,
                    QString(cmd::installBootloader)};
        QString image;
        const FakePodResult pod = build(sc, &image);
        QVERIFY(pod.ok);
        Cut cut;
        cut.pod = pod;
        cut.image = image;
        cut.dir = m_dir.filePath(QStringLiteral("data%1").arg(++m_count));
        const QJsonObject state = scan(image, pod.sectorSize);
        QScopedPointer<HelperClient> client(
            newClient(normalProgram(), QStringLiteral("write-before:1")));
        const Call c = call(*client, sc.command, commandArgs(sc, cut, state));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
    }

    void aKilledHelperLeavesNothingRunning()
    {
        Scenario sc{QStringLiteral("ipod-video"), TableKind::Dos, false,
                    QString(cmd::installBootloader)};
        Cut cut;
        QScopedPointer<HelperClient> keep;
        QVERIFY(run(sc, QStringLiteral("write-after:1"), &cut));
        QVERIFY(cut.killed());
        QCOMPARE(cut.call.finishedCount, 1);
        // The detail names the end of the helper, and the app shows its own message.
        QVERIFY(str(cut.call.result, "detail").contains(QStringLiteral("ended")));
        // Nothing starts by itself: the data folder does not change while we wait.
        const QStringList before = QDir(cut.dir).entryList(QDir::Files);
        const QByteArray hash = fileHash(cut.image);
        QTest::qWait(400);
        QCOMPARE(QDir(cut.dir).entryList(QDir::Files), before);
        QCOMPARE(fileHash(cut.image), hash);
    }

    void install_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("video") << QStringLiteral("ipod-video");
        QTest::newRow("nano-2g") << QStringLiteral("nano-2g");
    }
    void install()
    {
        QFETCH(QString, model);
        cutAtEveryPoint({model, TableKind::Dos, false, QString(cmd::installBootloader)});
    }

    void update_data() { install_data(); }
    void update()
    {
        QFETCH(QString, model);
        cutAtEveryPoint({model, TableKind::Dos, true, QString(cmd::updateBootloader)});
    }

    void uninstall_data() { install_data(); }
    void uninstall()
    {
        QFETCH(QString, model);
        cutAtEveryPoint({model, TableKind::Dos, true, QString(cmd::uninstallBootloader)});
    }

    void restoreBackup()
    {
        cutAtEveryPoint(
            {QStringLiteral("ipod-video"), TableKind::Dos, false, QString(cmd::restoreBackup)});
    }

    void formatMusic()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        cutAtEveryPoint(
            {QStringLiteral("ipod-video"), TableKind::Dos, false, QString(cmd::formatMusic)});
    }

    void convertToFat32()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        cutAtEveryPoint(
            {QStringLiteral("ipod-video"), TableKind::Apm, false, QString(cmd::convertToFat32)});
    }
};

QTEST_GUILESS_MAIN(TstHelperKillpoints)
#include "tst_helper_killpoints.moc"
