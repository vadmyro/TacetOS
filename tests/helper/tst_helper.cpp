// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "checks.h"
#include "commands.h"
#include "fakepod.h"
#include "gate.h"
#include "helperclient.h"
#include "helperconstants.h"
#include "lock.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::helper;
using tacetos::fakepod::FakePod;
using tacetos::fakepod::FakePodSpec;

namespace {

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

QByteArray requestLine(qint64 id, const QString &cmd, const QJsonObject &args)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), double(id));
    o.insert(QStringLiteral("cmd"), cmd);
    o.insert(QStringLiteral("args"), args);
    return eventLine(o);
}

QJsonObject imageArgs(const QString &path, int sectorSize = 512)
{
    QJsonObject a;
    a.insert(QStringLiteral("target"), HelperClient::imageTarget(path, sectorSize));
    return a;
}

// A fake disk that says it is a real one, for the gate.
class RealLooking : public tacetos::installer::SectorDevice
{
public:
    int sectorSize() const override { return 512; }
    qint64 sectorCount() const override { return 1; }
    bool isRealDisk() const override { return true; }
    tacetos::installer::IoResult readSectors(qint64, qint64, QByteArray &) override { return {}; }
    tacetos::installer::IoResult writeSectors(qint64, const QByteArray &) override { return {}; }
    tacetos::installer::IoResult flush() override { return {}; }
};

} // namespace

class TstHelper : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_runtime;
    QString m_image;
    QString m_installedImage;
    QString m_classicImage;

    QString build(const QString &name, const QString &model, bool boot)
    {
        FakePodSpec spec;
        spec.modelId = model;
        spec.path = m_dir.filePath(name);
        spec.bootloaderPresent = boot;
        const auto r = FakePod::build(spec);
        return r.ok ? spec.path : QString();
    }

    // Starts the real program with this input, and gives its lines and exit code.
    QList<QJsonObject> runHelper(const QByteArray &input, int *exitCode = nullptr)
    {
        QProcess p;
        p.start(QStringLiteral(TACETOS_HELPER_PROGRAM), {});
        p.write(input);
        p.closeWriteChannel();
        p.waitForFinished(20000);
        if (exitCode)
            *exitCode = p.exitCode();
        QList<QJsonObject> out;
        for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
            if (!l.isEmpty())
                out.append(QJsonDocument::fromJson(l).object());
        }
        return out;
    }

    static QJsonObject resultOf(const QList<QJsonObject> &events, qint64 id)
    {
        for (const QJsonObject &e : events) {
            if (e.value(QStringLiteral("event")).toString() == QLatin1String("result") &&
                qint64(e.value(QStringLiteral("id")).toDouble()) == id)
                return e;
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
        m_image = build(QStringLiteral("video.img"), QStringLiteral("ipod-video"), false);
        m_installedImage =
            build(QStringLiteral("video-boot.img"), QStringLiteral("ipod-video"), true);
        m_classicImage = build(QStringLiteral("classic.img"), QStringLiteral("classic-6g"), false);
        QVERIFY(!m_image.isEmpty());
        QVERIFY(!m_installedImage.isEmpty());
        QVERIFY(!m_classicImage.isEmpty());
    }

    void lineReaderCutsLongLines()
    {
        QByteArray input = QByteArray(kMaxRequestLine + 10, 'a') + "\n{}\nlast";
        int at = 0;
        LineReader r([&] { return at < input.size() ? uchar(input.at(at++)) : -1; });
        QCOMPARE(r.next().kind, LineReader::Kind::TooLong);
        const auto second = r.next();
        QCOMPARE(second.kind, LineReader::Kind::Line);
        QCOMPARE(second.line, QByteArray("{}"));
        QCOMPARE(r.next().line, QByteArray("last"));
        QCOMPARE(r.next().kind, LineReader::Kind::End);
    }

    void parseRefusals_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QString>("code");
        const QString img = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/x.img"));
        const auto good = [&](const QString &cmd, const QString &args) {
            return QByteArray("{\"id\":5,\"cmd\":\"" + cmd.toUtf8() +
                              "\",\"args\":" + args.toUtf8() + "}");
        };
        const QString t =
            QStringLiteral("{\"target\":{\"kind\":\"image\",\"path\":\"%1\",\"sectorSize\":512}}")
                .arg(img);
        QTest::newRow("unknown command")
            << good(QStringLiteral("format-everything"), QStringLiteral("{}"))
            << QString(err::unknownCommand);
        QTest::newRow("listed in the spec but not wired")
            << good(QStringLiteral("format-music"), QStringLiteral("{}"))
            << QString(err::unknownCommand);
        QTest::newRow("not json") << QByteArray("hello") << QString(err::badRequest);
        QTest::newRow("not an object") << QByteArray("[1]") << QString(err::badRequest);
        QTest::newRow("extra field")
            << QByteArray("{\"id\":5,\"cmd\":\"read-state\",\"args\":{},\"x\":1}")
            << QString(err::badRequest);
        QTest::newRow("missing field")
            << QByteArray("{\"id\":5,\"cmd\":\"read-state\"}") << QString(err::badRequest);
        QTest::newRow("id zero") << QByteArray("{\"id\":0,\"cmd\":\"read-state\",\"args\":{}}")
                                 << QString(err::badRequest);
        QTest::newRow("id not growing")
            << QByteArray("{\"id\":3,\"cmd\":\"read-state\",\"args\":{}}")
            << QString(err::badRequest);
        QTest::newRow("argument missing")
            << good(QStringLiteral("read-state"), QStringLiteral("{}")) << QString(err::badRequest);
        QTest::newRow("argument too many")
            << good(QStringLiteral("read-state"), t.chopped(1) + QStringLiteral(",\"force\":true}"))
            << QString(err::badRequest);
        QTest::newRow("field too many in target")
            << good(QStringLiteral("read-state"),
                    QStringLiteral("{\"target\":{\"kind\":\"image\",\"path\":\"%1\",\"sectorSize\":"
                                   "512,\"x\":1}}")
                        .arg(img))
            << QString(err::badRequest);
        QTest::newRow("wrong type")
            << good(QStringLiteral("read-state"),
                    QStringLiteral(
                        "{\"target\":{\"kind\":\"image\",\"path\":\"%1\",\"sectorSize\":\"512\"}}")
                        .arg(img))
            << QString(err::badRequest);
        QTest::newRow("relative path")
            << good(
                   QStringLiteral("read-state"),
                   QStringLiteral(
                       "{\"target\":{\"kind\":\"image\",\"path\":\"a/b.img\",\"sectorSize\":512}}"))
            << QString(err::badRequest);
        QTest::newRow("fraction")
            << good(QStringLiteral("read-state"),
                    QStringLiteral(
                        "{\"target\":{\"kind\":\"image\",\"path\":\"%1\",\"sectorSize\":512.5}}")
                        .arg(img))
            << QString(err::badRequest);
    }

    void parseRefusals()
    {
        QFETCH(QByteArray, line);
        QFETCH(QString, code);
        const ParseResult r = parseRequest(line, 4);
        QVERIFY(!r.ok);
        QCOMPARE(r.refusal.code, code);
    }

    void parseAcceptsRequest()
    {
        const ParseResult r =
            parseRequest(requestLine(5, QStringLiteral("read-state"), imageArgs(m_image)), 4);
        QVERIFY2(r.ok, qPrintable(r.refusal.detail));
        QCOMPARE(r.request.id, qint64(5));
    }

    void lockRefusesSecondHolder()
    {
        const QString folder = m_runtime.filePath(QStringLiteral("locks-a"));
        QString why;
        bool held = false;
        auto first = DiskLock::acquire(folder, QStringLiteral("image:/a"), &why, &held);
        QVERIFY2(first, qPrintable(why));
        auto second = DiskLock::acquire(folder, QStringLiteral("image:/a"), &why, &held);
        QVERIFY(!second);
        QVERIFY(held);
        // Another disk is not affected.
        QVERIFY(DiskLock::acquire(folder, QStringLiteral("image:/b"), &why, &held));
        first.reset();
        QVERIFY(!QFile::exists(DiskLock::lockFilePath(folder, QStringLiteral("image:/a"))));
        QVERIFY(DiskLock::acquire(folder, QStringLiteral("image:/a"), &why, &held));
    }

    void gateRefusesRealDiskOnDevBuild()
    {
        RealLooking real;
        QVERIFY(!writeAllowed(real, false));
        QVERIFY(writeAllowed(real, true));
        tacetos::installer::FileSectorDevice image(m_image, 512);
        QVERIFY(writeAllowed(image, false));
    }

    void programRefusesArguments()
    {
        QProcess p;
        p.start(QStringLiteral(TACETOS_HELPER_PROGRAM), {QStringLiteral("--help")});
        QVERIFY(p.waitForFinished(20000));
        QCOMPARE(p.exitCode(), kExitCommandLine);
        QVERIFY(!p.readAllStandardError().isEmpty());
    }

    void programSaysHelloAndEndsOnClosedInput()
    {
        int code = -1;
        const auto events = runHelper({}, &code);
        QCOMPARE(code, kExitNormal);
        QCOMPARE(events.size(), 1);
        const QJsonObject hello = events.first();
        QCOMPARE(hello.value(QStringLiteral("event")).toString(), QStringLiteral("hello"));
        QCOMPARE(hello.value(QStringLiteral("protocol")).toInt(), kProtocol);
        QCOMPARE(hello.value(QStringLiteral("version")).toString(), helperVersion());
        QVERIFY(!hello.value(QStringLiteral("realWrites")).toBool() ||
                tacetos::installer::realWritesAllowed());
    }

    void programRefusesAndWritesNothing()
    {
        const QByteArray before = fileHash(m_image);
        QByteArray input;
        input += requestLine(1, QStringLiteral("erase-all"), {});
        QJsonObject extra = imageArgs(m_image);
        extra.insert(QStringLiteral("force"), true);
        input += requestLine(2, QStringLiteral("read-state"), extra);
        input += requestLine(2, QStringLiteral("read-state"), imageArgs(m_image));
        input += QByteArray(kMaxRequestLine + 5, 'x') + "\n";
        input += requestLine(3, QStringLiteral("read-state"), imageArgs(QDir::tempPath()));
        input += requestLine(4, QStringLiteral("read-state"), imageArgs(m_image, 1000));
        const auto events = runHelper(input);
        QCOMPARE(resultOf(events, 1).value(QStringLiteral("error")).toString(),
                 QString(err::unknownCommand));
        // The refused id 2 counts: the same id again is refused.
        const QJsonObject second = resultOf(events, 2);
        QCOMPARE(second.value(QStringLiteral("error")).toString(), QString(err::badRequest));
        QCOMPARE(resultOf(events, 0).value(QStringLiteral("error")).toString(),
                 QString(err::badRequest));
        QCOMPARE(resultOf(events, 3).value(QStringLiteral("error")).toString(),
                 QString(err::badTarget));
        QCOMPARE(resultOf(events, 4).value(QStringLiteral("error")).toString(),
                 QString(err::badTarget));
        for (const QJsonObject &e : events)
            QVERIFY(e.value(QStringLiteral("event")).toString() != QLatin1String("accepted"));
        QCOMPARE(fileHash(m_image), before);
    }

    void clientReadsAnImage()
    {
        const QByteArray before = fileHash(m_image);
        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QSignalSpy spy(&client, &HelperClient::finished);
        QSignalSpy events(&client, &HelperClient::eventReceived);
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        const QJsonObject result = spy.first().at(1).toJsonObject();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QJsonDocument(result).toJson()));
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("method")).toString(), QStringLiteral("partition"));
        QCOMPARE(data.value(QStringLiteral("table")).toString(), QStringLiteral("dos"));
        QCOMPARE(data.value(QStringLiteral("state")).toString(), QStringLiteral("clean"));
        QCOMPARE(data.value(QStringLiteral("sectorSize")).toInt(), 512);
        QCOMPARE(qint64(data.value(QStringLiteral("sizeBytes")).toDouble()),
                 QFileInfo(m_image).size());
        QVERIFY(!data.value(QStringLiteral("scanToken")).toString().isEmpty());
        QVERIFY(!data.value(QStringLiteral("target")).toString().isEmpty());
        QCOMPARE(data.value(QStringLiteral("slots")).toArray().size(), 4);
        QVERIFY(data.value(QStringLiteral("entries")).toArray().size() >= 1);
        // An `accepted` came before the result, the result is the last event.
        QCOMPARE(events.first().at(1).toJsonObject().value(QStringLiteral("event")).toString(),
                 QStringLiteral("accepted"));
        QCOMPARE(events.last().at(1).toJsonObject().value(QStringLiteral("event")).toString(),
                 QStringLiteral("result"));
        QCOMPARE(fileHash(m_image), before);

        // The same iPod, read again, gives the same token; another one does not.
        spy.clear();
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QCOMPARE(spy.first()
                     .at(1)
                     .toJsonObject()
                     .value(QStringLiteral("data"))
                     .toObject()
                     .value(QStringLiteral("scanToken")),
                 data.value(QStringLiteral("scanToken")));
        spy.clear();
        client.readImageState(m_installedImage, 512);
        QVERIFY(spy.wait(20000));
        const QJsonObject other =
            spy.first().at(1).toJsonObject().value(QStringLiteral("data")).toObject();
        QCOMPARE(other.value(QStringLiteral("state")).toString(), QStringLiteral("installed"));
        // The token holds sector 0 and the first firmware sector only (section 5.3):
        // the installed state is told apart by `expectedState`, not by the token.
        spy.clear();
        client.readImageState(m_classicImage, 4096);
        QVERIFY(spy.wait(20000));
        QVERIFY(spy.first()
                    .at(1)
                    .toJsonObject()
                    .value(QStringLiteral("data"))
                    .toObject()
                    .value(QStringLiteral("scanToken")) != data.value(QStringLiteral("scanToken")));
    }

    void clientReadsAClassicAsDfu()
    {
        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.readImageState(m_classicImage, 4096);
        QVERIFY(spy.wait(20000));
        const QJsonObject result = spy.first().at(1).toJsonObject();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QJsonDocument(result).toJson()));
        QCOMPARE(result.value(QStringLiteral("data"))
                     .toObject()
                     .value(QStringLiteral("method"))
                     .toString(),
                 QStringLiteral("dfu"));
    }

    void clientGetsRefusals()
    {
        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.request(QStringLiteral("not-a-command"), {});
        QJsonObject extra = imageArgs(m_image);
        extra.insert(QStringLiteral("more"), 1);
        client.request(QString(cmd::readState), extra);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 20000);
        QCOMPARE(spy.at(0).at(1).toJsonObject().value(QStringLiteral("error")).toString(),
                 QString(err::unknownCommand));
        QCOMPARE(spy.at(1).at(1).toJsonObject().value(QStringLiteral("error")).toString(),
                 QString(err::badRequest));
    }

    void secondOperationOnTheSameImageIsRefused()
    {
        // Another process holds the lock of the image: this very test process.
        const QString key = QStringLiteral("image:") + QFileInfo(m_image).canonicalFilePath();
        QString why;
        auto lock = DiskLock::acquire(DiskLock::defaultFolder(), key, &why);
        QVERIFY2(lock, qPrintable(why));
        const QByteArray before = fileHash(m_image);

        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QCOMPARE(spy.first().at(1).toJsonObject().value(QStringLiteral("error")).toString(),
                 QString(err::diskLocked));
        QCOMPARE(fileHash(m_image), before);

        // Released: the same request now works, and the lock is gone after it.
        lock.reset();
        spy.clear();
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QVERIFY(spy.first().at(1).toJsonObject().value(QStringLiteral("ok")).toBool());
        QVERIFY(!QFile::exists(DiskLock::lockFilePath(DiskLock::defaultFolder(), key)));
    }

    void staleLockIsTakenOver()
    {
        // The id of a process that has ended.
        QProcess done;
        done.start(QStringLiteral(TACETOS_HELPER_PROGRAM), {QStringLiteral("-x")});
        QVERIFY(done.waitForStarted());
        const qint64 pid = done.processId();
        QVERIFY(done.waitForFinished(20000));
        QVERIFY(!DiskLock::processRuns(pid));

        const QString key = QStringLiteral("image:") + QFileInfo(m_image).canonicalFilePath();
        const QString path = DiskLock::lockFilePath(DiskLock::defaultFolder(), key);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::number(pid) + '\n');
        f.close();

        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QVERIFY(spy.first().at(1).toJsonObject().value(QStringLiteral("ok")).toBool());
        QVERIFY(!QFile::exists(path));
    }

    void clientSaysWhenTheHelperIsMissing()
    {
        HelperClient client;
        client.setProgram(m_dir.filePath(QStringLiteral("no-such-helper")));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QCOMPARE(spy.first().at(1).toJsonObject().value(QStringLiteral("error")).toString(),
                 QString(err::helperMissing));
    }

    void clientRefusesAHelperOfAnotherVersion()
    {
        HelperClient client;
        client.setProgram(QStringLiteral(TACETOS_HELPER_PROGRAM));
        client.setExpectedVersion(QStringLiteral("9.9.9"));
        QSignalSpy spy(&client, &HelperClient::finished);
        client.readImageState(m_image, 512);
        QVERIFY(spy.wait(20000));
        QCOMPARE(spy.first().at(1).toJsonObject().value(QStringLiteral("error")).toString(),
                 QString(err::helperMismatch));
    }
};

QTEST_GUILESS_MAIN(TstHelper)
#include "tst_helper.moc"
