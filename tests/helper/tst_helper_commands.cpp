// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/helper.md section 15.2: one test from end to end per command (the
// client, the helper program, an image file), each checking the result, the image
// after, and the files of the data folder.

#include "bootloaderfile.h"
#include "fakepod.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "lock.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
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

// `length` bytes of the file from `offset`.
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
    QList<QJsonObject> events;
    bool ok() const { return result.value(QStringLiteral("ok")).toBool(); }
    QJsonObject data() const { return result.value(QStringLiteral("data")).toObject(); }
    QString error() const { return str(result, "error"); }
};

} // namespace

class TstHelperCommands : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_runtime;
    int m_count = 0;

    // A fresh image, so that no test sees what another one wrote.
    FakePodResult build(const QString &model, bool boot, TableKind table = TableKind::Dos,
                        QString *path = nullptr)
    {
        FakePodSpec spec;
        spec.modelId = model;
        spec.path = m_dir.filePath(QStringLiteral("disk%1.img").arg(++m_count));
        spec.bootloaderPresent = boot;
        spec.table = table;
        if (path)
            *path = spec.path;
        FakePodResult r = FakePod::build(spec);
        if (r.ok && r.sectorSize == 512)
            r.ok = growMusic(spec.path, r.musicStart / 512, table);
        return r;
    }

    // The music partition of a fake image is too small for FAT32: it is made
    // larger, as a sparse file, and its size is written in the table.
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

    QString dataDir() { return m_dir.filePath(QStringLiteral("data%1").arg(++m_count)); }

    Call call(HelperClient &client, const QString &cmd, const QJsonObject &args)
    {
        Call c;
        QSignalSpy done(&client, &HelperClient::finished);
        QSignalSpy events(&client, &HelperClient::eventReceived);
        client.request(cmd, args);
        if (!done.wait(30000))
            return c;
        c.result = done.first().at(1).toJsonObject();
        for (const QList<QVariant> &e : events)
            c.events.append(e.at(1).toJsonObject());
        return c;
    }

    HelperClient *newClient()
    {
        auto *client = new HelperClient(this);
        client->setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        return client;
    }

    static QJsonObject args(const QString &image, const QString &token, const QString &dataDir)
    {
        QJsonObject a;
        a.insert(QStringLiteral("target"), HelperClient::imageTarget(image, 512));
        a.insert(QStringLiteral("scanToken"), token);
        a.insert(QStringLiteral("dataDir"), dataDir);
        return a;
    }

    // The `data` of read-state: token, target, state.
    QJsonObject scan(HelperClient &client, const QString &image)
    {
        QJsonObject a;
        a.insert(QStringLiteral("target"), HelperClient::imageTarget(image, 512));
        const Call c = call(client, QString(cmd::readState), a);
        return c.ok() ? c.data() : QJsonObject();
    }

    QString writeBootloader(const QString &target)
    {
        const QString path = m_dir.filePath(QStringLiteral("boot%1.bin").arg(++m_count));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(BootloaderFile::build(target)) <= 0)
            return {};
        return path;
    }

    QJsonObject installArgs(const QString &image, const QJsonObject &state, const QString &expected,
                            const QString &dataDir, bool withFile)
    {
        QJsonObject a = args(image, str(state, "scanToken"), dataDir);
        a.insert(QStringLiteral("expectedState"), expected);
        if (withFile) {
            a.insert(QStringLiteral("bootloaderFile"), writeBootloader(str(state, "target")));
            a.insert(QStringLiteral("bootloaderVersion"), QStringLiteral("0.1.0"));
        }
        return a;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    void backupFirmware()
    {
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Dos, &image);
        QVERIFY(pod.ok);
        const QByteArray before = fileHash(image);
        QScopedPointer<HelperClient> client(newClient());
        const QJsonObject state = scan(*client, image);
        QVERIFY(!state.isEmpty());

        const QString dir = dataDir();
        const Call c =
            call(*client, QString(cmd::backupFirmware), args(image, str(state, "scanToken"), dir));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        const QString backup = str(c.data(), "path");
        QVERIFY(backup.startsWith(dir));
        QCOMPARE(QFileInfo(backup).size(), pod.firmwareBytes);
        QCOMPARE(bytesAt(backup, 0, pod.firmwareBytes),
                 bytesAt(image, pod.firmwareStart, pod.firmwareBytes));
        // Reading only: the image is the same.
        QCOMPARE(fileHash(image), before);
        // The stages of the command, in their order.
        QStringList stages;
        for (const QJsonObject &e : c.events) {
            if (str(e, "event") == QLatin1String("progress") && !stages.contains(str(e, "stage")))
                stages.append(str(e, "stage"));
        }
        QCOMPARE(stages, (QStringList{"open", "check", "backup"}));

        // A token that is not the one of the disk is refused.
        const Call bad =
            call(*client, QString(cmd::backupFirmware), args(image, QStringLiteral("0123"), dir));
        QVERIFY(!bad.ok());
        QCOMPARE(bad.error(), QString(err::diskChanged));
    }

    void installUpdateUninstall()
    {
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Dos, &image);
        QVERIFY(pod.ok);
        const QByteArray original = fileHash(image);
        QScopedPointer<HelperClient> client(newClient());
        QJsonObject state = scan(*client, image);
        QCOMPARE(str(state, "state"), QStringLiteral("clean"));

        // The state shown was not the real one: refused, nothing written.
        const QString dir = dataDir();
        Call c = call(*client, QString(cmd::installBootloader),
                      installArgs(image, state, QStringLiteral("installed"), dir, true));
        QVERIFY(!c.ok());
        QCOMPARE(fileHash(image), original);
        // Not the expected state for the command: refused too.
        c = call(*client, QString(cmd::updateBootloader),
                 installArgs(image, state, QStringLiteral("clean"), dir, true));
        QCOMPARE(c.error(), QString(err::badRequest));
        QCOMPARE(fileHash(image), original);

        // Install.
        c = call(*client, QString(cmd::installBootloader),
                 installArgs(image, state, QStringLiteral("clean"), dir, true));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        QVERIFY(QFileInfo::exists(str(c.data(), "backup")));
        QVERIFY(str(c.data(), "backup").startsWith(dir));
        QVERIFY(QFileInfo::exists(str(c.data(), "record")));
        QVERIFY(fileHash(image) != original);
        // The first write is announced, and nothing could be cancelled after it.
        bool sawWrite = false;
        for (const QJsonObject &e : c.events) {
            if (str(e, "event") != QLatin1String("progress"))
                continue;
            if (str(e, "stage") == QLatin1String("write"))
                sawWrite = true;
            QCOMPARE(e.value(QStringLiteral("cancellable")).toBool(), !sawWrite);
        }
        QVERIFY(sawWrite);
        state = scan(*client, image);
        QCOMPARE(str(state, "state"), QStringLiteral("installed"));

        // Update.
        c = call(*client, QString(cmd::updateBootloader),
                 installArgs(image, state, QStringLiteral("installed"), dir, true));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        state = scan(*client, image);
        QCOMPARE(str(state, "state"), QStringLiteral("installed"));

        // Uninstall: the bootloader goes, the firmware partition is the one of the
        // start again (the directory is back as it was).
        c = call(*client, QString(cmd::uninstallBootloader),
                 installArgs(image, state, QStringLiteral("installed"), dir, false));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        state = scan(*client, image);
        QCOMPARE(str(state, "state"), QStringLiteral("clean"));
    }

    void restoreBackup()
    {
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Dos, &image);
        QVERIFY(pod.ok);
        const QByteArray firmware = bytesAt(image, pod.firmwareStart, pod.firmwareBytes);
        QScopedPointer<HelperClient> client(newClient());
        QJsonObject state = scan(*client, image);
        const QString dir = dataDir();
        Call c = call(*client, QString(cmd::installBootloader),
                      installArgs(image, state, QStringLiteral("clean"), dir, true));
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        const QString backup = str(c.data(), "backup");
        QVERIFY(bytesAt(image, pod.firmwareStart, pod.firmwareBytes) != firmware);

        state = scan(*client, image);
        QJsonObject a = args(image, str(state, "scanToken"), dir);
        a.insert(QStringLiteral("backupFile"), backup);
        // A backup outside the data folder is refused.
        QJsonObject outside = a;
        outside.insert(QStringLiteral("backupFile"), writeBootloader(str(state, "target")));
        c = call(*client, QString(cmd::restoreBackup), outside);
        QCOMPARE(c.error(), QString(err::badRequest));

        c = call(*client, QString(cmd::restoreBackup), a);
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        QCOMPARE(bytesAt(image, pod.firmwareStart, pod.firmwareBytes), firmware);
        // The backup is never deleted.
        QVERIFY(QFileInfo::exists(backup));
        QCOMPARE(str(scan(*client, image), "state"), QStringLiteral("clean"));
    }

    void formatMusic()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Dos, &image);
        QVERIFY(pod.ok);
        const QByteArray before = fileHash(image);
        const QByteArray sector0 = bytesAt(image, 0, 512);
        const QByteArray firmware = bytesAt(image, pod.firmwareStart, pod.firmwareBytes);
        QScopedPointer<HelperClient> client(newClient());
        const QJsonObject state = scan(*client, image);

        QJsonObject a;
        a.insert(QStringLiteral("target"), HelperClient::imageTarget(image, 512));
        a.insert(QStringLiteral("scanToken"), str(state, "scanToken"));
        a.insert(QStringLiteral("ipodName"), QStringLiteral("Test iPod"));
        a.insert(QStringLiteral("volumeLabel"), QStringLiteral("TESTPOD"));
        // The typed name does not match: refused before the image is opened.
        QJsonObject wrong = a;
        wrong.insert(QStringLiteral("typedName"), QStringLiteral("test ipod"));
        Call c = call(*client, QString(cmd::formatMusic), wrong);
        QCOMPARE(c.error(), QString(err::confirmationMismatch));
        for (const QJsonObject &e : c.events)
            QVERIFY(str(e, "event") != QLatin1String("accepted"));
        QCOMPARE(fileHash(image), before);

        // Spaces around the typed name do not count.
        a.insert(QStringLiteral("typedName"), QStringLiteral("  Test iPod "));
        c = call(*client, QString(cmd::formatMusic), a);
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        QVERIFY(c.data().value(QStringLiteral("sizeBytes")).toDouble() > 0);
        // A FAT32 file system in the music partition, nothing else touched.
        QCOMPARE(bytesAt(image, pod.musicStart + 0x52, 8), QByteArray("FAT32   "));
        QCOMPARE(bytesAt(image, pod.musicStart + 0x47, 11), QByteArray("TESTPOD    "));
        QCOMPARE(bytesAt(image, 0, 512), sector0);
        QCOMPARE(bytesAt(image, pod.firmwareStart, pod.firmwareBytes), firmware);
    }

    void convertToFat32()
    {
#ifndef Q_OS_LINUX
        QSKIP("the images are large sparse files, which only Linux makes cheaply");
#endif
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Apm, &image);
        QVERIFY(pod.ok);
        const QByteArray firmware = bytesAt(image, pod.firmwareStart, pod.firmwareBytes);
        QScopedPointer<HelperClient> client(newClient());
        const QJsonObject state = scan(*client, image);
        QCOMPARE(str(state, "table"), QStringLiteral("apple"));

        const QString dir = dataDir();
        QJsonObject a = args(image, str(state, "scanToken"), dir);
        a.insert(QStringLiteral("ipodName"), QStringLiteral("Mac iPod"));
        a.insert(QStringLiteral("typedName"), QStringLiteral("Mac iPod"));
        a.insert(QStringLiteral("volumeLabel"), QStringLiteral("IPOD"));
        const Call c = call(*client, QString(cmd::convertToFat32), a);
        QVERIFY2(c.ok(), qPrintable(QJsonDocument(c.result).toJson()));
        // The recognition again: a Windows iPod now, with a token of its own.
        QCOMPARE(str(c.data(), "table"), QStringLiteral("dos"));
        QCOMPARE(str(c.data(), "method"), QStringLiteral("partition"));
        QVERIFY(str(c.data(), "scanToken") != str(state, "scanToken"));
        QVERIFY(QFileInfo::exists(str(c.data(), "backup")));
        QVERIFY(QFileInfo::exists(str(c.data(), "mapBackup")));
        QVERIFY(str(c.data(), "backup").startsWith(dir));
        QCOMPARE(bytesAt(image, 0x1FE, 2), QByteArray("\x55\xAA", 2));
        QCOMPARE(bytesAt(image, pod.firmwareStart, pod.firmwareBytes), firmware);
        QCOMPARE(str(scan(*client, image), "table"), QStringLiteral("dos"));
    }

    void cancelBeforeTheFirstWrite()
    {
        QString image;
        const FakePodResult pod =
            build(QStringLiteral("ipod-video"), false, TableKind::Dos, &image);
        QVERIFY(pod.ok);
        QScopedPointer<HelperClient> client(newClient());
        const QJsonObject state = scan(*client, image);
        const QByteArray before = fileHash(image);
        const QString dir = dataDir();

        // The cancel is in the input before the command reads its first sector: it
        // is heard at the first stopping point, and nothing is written.
        QJsonObject install = installArgs(image, state, QStringLiteral("clean"), dir, true);
        QJsonObject cancelArgs;
        cancelArgs.insert(QStringLiteral("request"), 1);
        QJsonObject r1, r2;
        r1.insert(QStringLiteral("id"), 1);
        r1.insert(QStringLiteral("cmd"), QString(cmd::installBootloader));
        r1.insert(QStringLiteral("args"), install);
        r2.insert(QStringLiteral("id"), 2);
        r2.insert(QStringLiteral("cmd"), QString(cmd::cancel));
        r2.insert(QStringLiteral("args"), cancelArgs);
        QProcess p;
        p.start(QStringLiteral(TACETOS_HELPER_PROGRAM), {});
        p.write(eventLine(r1) + eventLine(r2));
        p.closeWriteChannel();
        QVERIFY(p.waitForFinished(30000));
        QJsonObject first, cancel;
        for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
            const QJsonObject e = QJsonDocument::fromJson(l).object();
            if (str(e, "event") != QLatin1String("result"))
                continue;
            if (e.value(QStringLiteral("id")).toInt() == 1)
                first = e;
            else
                cancel = e;
        }
        QCOMPARE(str(first, "error"), QString(err::cancelled));
        QVERIFY(cancel.value(QStringLiteral("ok")).toBool());
        QCOMPARE(fileHash(image), before);
        QCOMPARE(str(scan(*client, image), "state"), QStringLiteral("clean"));

        // Outside any command, a cancel has nothing to stop.
        const Call late = call(*client, QString(cmd::cancel), cancelArgs);
        QCOMPARE(late.error(), QString(err::noSuchRequest));
    }
};

QTEST_GUILESS_MAIN(TstHelperCommands)
#include "tst_helper_commands.moc"
