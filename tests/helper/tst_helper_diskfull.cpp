// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md section 15.2 and docs/specs/bootloader-legacy.md section 11
// check 6 and section 18: the data folder of the computer is full. The test program
// of the helper answers "no space left" to its writes there (`TACETOS_TEST_FULL`).
// A full folder for the backup writes nothing to the iPod and leaves no half copy;
// a full folder for the install record, after a successful install, is a message and
// the iPod is exactly what an install with a record leaves.

#include "bootloaderfile.h"
#include "fakepod.h"
#include "fixtures.h"
#include "helperclient.h"
#include "helperconstants.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd())
        h.addData(f.read(1 << 20));
    return h.result();
}

struct Call
{
    QJsonObject result;
    bool ok() const { return result.value(QStringLiteral("ok")).toBool(); }
    QJsonObject data() const { return result.value(QStringLiteral("data")).toObject(); }
    QString error() const { return str(result, "error"); }
    QString detail() const { return str(result, "detail"); }
};

} // namespace

class TstHelperDiskFull : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_runtime;
    int m_count = 0;

    struct Pod
    {
        FakePodResult built;
        QString image;
    };

    Pod build(bool bootloader, TableKind table)
    {
        Pod p;
        FakePodSpec spec;
        spec.modelId = QStringLiteral("ipod-video");
        spec.path = m_dir.filePath(QStringLiteral("disk%1.img").arg(++m_count));
        spec.bootloaderPresent = bootloader;
        spec.table = table;
        p.image = spec.path;
        p.built = FakePod::build(spec);
        if (p.built.ok && table == TableKind::Apm)
            p.built.ok = growMusic(spec.path, p.built.musicStart / 512);
        return p;
    }

    // As in tst_helper_commands: a music partition large enough for FAT32, sparse.
    static bool growMusic(const QString &path, qint64 musicFirst)
    {
        constexpr qint64 kMusicTotal = 600 * 1024 * 2;
        QFile f(path);
        if (!f.open(QIODevice::ReadWrite) || !f.resize((musicFirst + kMusicTotal) * 512))
            return false;
        QByteArray count(4, '\0');
        for (int k = 0; k < 4; ++k)
            count[k] = char((quint32(kMusicTotal) >> (24 - 8 * k)) & 0xFF);
        f.seek(1536 + 12);
        return f.write(count) == 4;
    }

    // `full` is the value of TACETOS_TEST_FULL, empty for none; the program is the
    // one with the test hooks in both cases.
    Call call(const QString &full, const QString &command, const QJsonObject &args)
    {
        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
        if (!full.isEmpty()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TACETOS_TEST_FULL"), full);
            client.setEnvironment(env);
        }
        Call c;
        QSignalSpy done(&client, &HelperClient::finished);
        client.request(command, args);
        if (done.wait(60000))
            c.result = done.first().at(1).toJsonObject();
        return c;
    }

    QJsonObject scan(const Pod &p)
    {
        QJsonObject a;
        a.insert(QStringLiteral("target"), HelperClient::imageTarget(p.image, p.built.sectorSize));
        const Call c = call({}, QString(cmd::readState), a);
        return c.ok() ? c.data() : QJsonObject();
    }

    QJsonObject args(const Pod &p, const QString &command, const QString &dataDir)
    {
        const QJsonObject state = scan(p);
        QJsonObject a;
        a.insert(QStringLiteral("target"), HelperClient::imageTarget(p.image, p.built.sectorSize));
        a.insert(QStringLiteral("scanToken"), str(state, "scanToken"));
        a.insert(QStringLiteral("dataDir"), dataDir);
        if (command == cmd::convertToFat32) {
            a.insert(QStringLiteral("ipodName"), QStringLiteral("Test iPod"));
            a.insert(QStringLiteral("typedName"), QStringLiteral("Test iPod"));
            a.insert(QStringLiteral("volumeLabel"), QStringLiteral("TESTPOD"));
            return a;
        }
        if (command == cmd::backupFirmware)
            return a;
        a.insert(QStringLiteral("expectedState"), command == cmd::installBootloader
                                                      ? QStringLiteral("clean")
                                                      : QStringLiteral("installed"));
        if (command != cmd::uninstallBootloader) {
            const QString file = m_dir.filePath(QStringLiteral("boot%1.bin").arg(++m_count));
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly) ||
                f.write(BootloaderFile::build(str(state, "target"))) <= 0)
                return {};
            a.insert(QStringLiteral("bootloaderFile"), file);
            a.insert(QStringLiteral("bootloaderVersion"), QStringLiteral("0.1.0"));
        }
        return a;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_runtime.isValid());
        // The folder of the disk locks.
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    // Only the test program reads the variable: the normal helper does not hold its name.
    void onlyTheTestProgramHasTheFullFolder()
    {
        const QByteArray name("TACETOS_TEST_FULL");
        QFile normal(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QVERIFY(normal.open(QIODevice::ReadOnly));
        QVERIFY2(!normal.readAll().contains(name), "the normal helper reads the variable");
        QFile test(QStringLiteral(TACETOS_HELPER_KILLTEST_PROGRAM));
        QVERIFY(test.open(QIODevice::ReadOnly));
        QVERIFY(test.readAll().contains(name));
    }

    void fullBackupFolderWritesNothing_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("bootloader");
        QTest::addColumn<QString>("code");
        QTest::newRow("install") << QString(cmd::installBootloader) << false
                                 << QString(err::backupFailed);
        QTest::newRow("update") << QString(cmd::updateBootloader) << true
                                << QString(err::backupFailed);
        QTest::newRow("uninstall")
            << QString(cmd::uninstallBootloader) << true << QString(err::backupFailed);
        // The conversion makes its two copies as a check of its own (section 21).
        QTest::newRow("convert") << QString(cmd::convertToFat32) << false
                                 << QString(err::checkFailed);
    }

    // The copy is cut after its first piece: nothing is written to the iPod, the
    // half copy is removed, and the answer says that the backup is what failed.
    void fullBackupFolderWritesNothing()
    {
        QFETCH(QString, command);
        QFETCH(bool, bootloader);
        QFETCH(QString, code);
        const bool convert = command == cmd::convertToFat32;
        const Pod p = build(bootloader, convert ? TableKind::Apm : TableKind::Dos);
        QVERIFY(p.built.ok);
        const QString folder = m_dir.filePath(QStringLiteral("data%1").arg(++m_count));
        const QJsonObject a = args(p, command, folder);
        QVERIFY(!a.isEmpty());
        const QByteArray before = fileHash(p.image);
        const Call c = call(QStringLiteral("backup"), command, a);
        QVERIFY2(!c.result.isEmpty(), "the helper did not answer");
        QVERIFY2(!c.ok(), qPrintable(command));
        QCOMPARE(c.error(), code);
        QVERIFY(!c.detail().isEmpty());
        if (!convert)
            QVERIFY2(c.detail().contains(QStringLiteral("backup"), Qt::CaseInsensitive),
                     qPrintable(c.detail()));
        QCOMPARE(fileHash(p.image), before);
        QVERIFY2(QDir(folder).entryList(QDir::Files).isEmpty(),
                 qPrintable(QDir(folder).entryList(QDir::Files).join(' ')));
        // The same command with room goes through, so the refusal came from the folder.
        const Call fine = call({}, command, args(p, command, folder));
        QVERIFY2(fine.ok(), qPrintable(fine.error() + ": " + fine.detail()));
        QVERIFY(fileHash(p.image) != before);
    }

    // The backup command alone: the same folder, the same answer, no half copy.
    void fullBackupFolderForTheBackupCommand()
    {
        const Pod p = build(false, TableKind::Dos);
        QVERIFY(p.built.ok);
        const QString folder = m_dir.filePath(QStringLiteral("data%1").arg(++m_count));
        const QByteArray before = fileHash(p.image);
        const Call c = call(QStringLiteral("backup"), QString(cmd::backupFirmware),
                            args(p, QString(cmd::backupFirmware), folder));
        QVERIFY(!c.ok());
        QCOMPARE(c.error(), QString(err::backupFailed));
        QCOMPARE(fileHash(p.image), before);
        QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());
    }

    // Section 18: the record comes after the verification. The install is a success
    // with a message, and the iPod holds the same bytes as after an install whose
    // record could be written.
    void fullRecordFolderAfterAnInstall()
    {
        const Pod good = build(false, TableKind::Dos);
        const Pod full = build(false, TableKind::Dos);
        QVERIFY(good.built.ok && full.built.ok);
        const QString goodFolder = m_dir.filePath(QStringLiteral("data-good"));
        const QString fullFolder = m_dir.filePath(QStringLiteral("data-full"));
        const Call ok = call({}, QString(cmd::installBootloader),
                             args(good, QString(cmd::installBootloader), goodFolder));
        QVERIFY2(ok.ok(), qPrintable(ok.error() + ": " + ok.detail()));
        QVERIFY(!str(ok.data(), "record").isEmpty());

        const Call c = call(QStringLiteral("record"), QString(cmd::installBootloader),
                            args(full, QString(cmd::installBootloader), fullFolder));
        QVERIFY2(c.ok(), qPrintable(c.error() + ": " + c.detail()));
        QVERIFY(str(c.data(), "record").isEmpty());
        QVERIFY2(str(c.data(), "recordMessage").contains(QStringLiteral("No space")),
                 qPrintable(str(c.data(), "recordMessage")));
        QVERIFY(QFile::exists(str(c.data(), "backup")));
        QCOMPARE(fileHash(full.image), fileHash(good.image));
        // The state read afterwards is Installed, and no record file is left.
        QCOMPARE(str(scan(full), "state"), QStringLiteral("installed"));
        for (const QString &name : QDir(fullFolder).entryList(QDir::Files))
            QVERIFY2(!name.startsWith(QStringLiteral("record-")), qPrintable(name));
    }

    // Check 5 (section 11): a partition without room for the new data is refused
    // before the backup and before any write: nothing is left in the folder.
    void noRoomInThePartitionWritesNothing()
    {
        Pod p;
        p.image = m_dir.filePath(QStringLiteral("noroom.img"));
        const tacetos::fakepod::FixtureBuild b =
            tacetos::fakepod::Fixtures::build(tacetos::fakepod::FixtureId::Check5NoRoom, p.image);
        QVERIFY2(b.ok, qPrintable(b.error));
        p.built = b.pod;
        const QString folder = m_dir.filePath(QStringLiteral("data-noroom"));
        const QJsonObject a = args(p, QString(cmd::installBootloader), folder);
        QVERIFY(!a.isEmpty());
        const QByteArray before = fileHash(p.image);
        const Call c = call({}, QString(cmd::installBootloader), a);
        QVERIFY2(!c.result.isEmpty(), "the helper did not answer");
        QCOMPARE(c.error(), QString(err::checkFailed));
        QCOMPARE(c.result.value(QStringLiteral("data"))
                     .toObject()
                     .value(QStringLiteral("check"))
                     .toInt(),
                 5);
        QVERIFY2(c.detail().contains(QStringLiteral("not enough room")), qPrintable(c.detail()));
        QCOMPARE(fileHash(p.image), before);
        QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstHelperDiskFull)
#include "tst_helper_diskfull.moc"
