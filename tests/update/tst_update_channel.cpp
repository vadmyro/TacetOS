// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The address and the reads of docs/specs/update-channel.md section 4, and the
// check of sections 5 and 8: the tests "The reads" and "The address" of section
// 9. Every read goes to a fake HTTP server on 127.0.0.1 started here, or to a
// folder made here: never to the real channel.

#include "address.h"
#include "check.h"
#include "constants.h"
#include "journal.h"
#include "reader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <optional>

using namespace tacetos::update;

namespace {

// What the fake server answers for one path.
struct Route
{
    int status = 200;
    QByteArray body;
    QByteArray location;
    // Sends the headers and the first half of the body, then nothing more.
    bool stall = false;
    // No Content-Length: the body ends when the connection closes.
    bool noLength = false;
};

class FakeServer : public QObject
{
public:
    FakeServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeServer::onConnection);
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString base() const
    {
        return QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort());
    }
    void route(const QByteArray &path, const Route &route) { m_routes.insert(path, route); }
    int requests() const { return m_requests; }

private:
    void onConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            socket->setParent(this);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
        }
    }

    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer.append(socket->readAll());
        if (!buffer.contains("\r\n\r\n"))
            return;
        ++m_requests;
        const QList<QByteArray> words = buffer.left(buffer.indexOf("\r\n")).split(' ');
        buffer.clear();
        const QByteArray path = words.size() > 1 ? words.at(1) : QByteArray();
        const Route route =
            m_routes.value(path, Route{404, QByteArray("not here"), {}, false, false});

        QByteArray head = "HTTP/1.1 " + QByteArray::number(route.status) + " X\r\n";
        if (!route.location.isEmpty())
            head += "Location: " + route.location + "\r\n";
        if (!route.noLength) {
            const qsizetype length = route.stall ? route.body.size() + 1000 : route.body.size();
            head += "Content-Length: " + QByteArray::number(length) + "\r\n";
        }
        head += "Connection: close\r\n\r\n";
        socket->write(head);
        if (route.stall) {
            socket->write(route.body.left(route.body.size() / 2));
            return;
        }
        socket->write(route.body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QByteArray, Route> m_routes;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    int m_requests = 0;
};

Route redirect(const QString &to)
{
    Route route;
    route.status = 302;
    route.location = to.toUtf8();
    return route;
}

ChannelAddress localAddress(const FakeServer &server)
{
    ChannelAddress address;
    address.kind = ChannelAddress::Kind::Local;
    address.url = QUrl(server.base());
    return address;
}

ChannelAddress folderAddress(const QString &folder)
{
    ChannelAddress address;
    address.kind = ChannelAddress::Kind::Folder;
    address.folder = folder;
    return address;
}

std::optional<ChannelRead> readOnce(ChannelReader &reader, const ChannelAddress &address,
                                    const QString &name, qint64 limit)
{
    std::optional<ChannelRead> got;
    int count = 0;
    const QMetaObject::Connection connection =
        QObject::connect(&reader, &ChannelReader::finished, [&](const ChannelRead &read) {
            got = read;
            ++count;
        });
    reader.read(address, name, limit);
    // finished() never comes before read() has returned.
    if (got)
        return std::nullopt;
    if (!QTest::qWaitFor([&] { return got.has_value(); }, 10000))
        return std::nullopt;
    // And it comes once.
    QTest::qWait(50);
    QObject::disconnect(connection);
    return count == 1 ? got : std::nullopt;
}

std::optional<UpdateCheckResult> checkOnce(UpdateCheck &check, const ChannelAddress &address,
                                           const QString &target, const QString &ipodVersion)
{
    std::optional<UpdateCheckResult> got;
    const QMetaObject::Connection connection = QObject::connect(
        &check, &UpdateCheck::finished, [&](const UpdateCheckResult &result) { got = result; });
    check.check(address, target, ipodVersion);
    const bool came = QTest::qWaitFor([&] { return got.has_value(); }, 10000);
    QObject::disconnect(connection);
    return came ? got : std::nullopt;
}

QByteArray index(const QString &version)
{
    const QString text = QStringLiteral(
        "{\"format\": 1, \"version\": \"%1\", \"built\": \"2026-11-26T10:00:00Z\", "
        "\"packages\": [{\"target\": \"ipod6g\", \"manifest\": \"tacetos-ipod6g-%1.json\", "
        "\"size\": 812, \"sha256\": \"%2\"}]}");
    return text.arg(version, QString(64, QLatin1Char('a'))).toUtf8();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class TestUpdateChannel : public QObject
{
    Q_OBJECT

private slots:
    // The address.

    void keepsTheDefaultWithNoValue()
    {
        const ChannelChoice choice = chooseChannel(QString(), false);
        QCOMPARE(choice.address.kind, ChannelAddress::Kind::Remote);
        QCOMPARE(choice.address.url.toString(), QString(tacetos::device::kUpdateChannel));
        QCOMPARE(choice.log, QStringList{QStringLiteral("Update channel: %1")
                                             .arg(QString(tacetos::device::kUpdateChannel))});
        QCOMPARE(defaultChannel().url, choice.address.url);
    }

    void takesEachKindOfValue_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("url");
        const int remote = int(ChannelAddress::Kind::Remote);
        const int local = int(ChannelAddress::Kind::Local);
        QTest::newRow("https") << QStringLiteral("https://example.org/tacetos/") << remote
                               << QStringLiteral("https://example.org/tacetos/");
        QTest::newRow("https with no slash")
            << QStringLiteral("https://example.org/tacetos") << remote
            << QStringLiteral("https://example.org/tacetos/");
        QTest::newRow("https upper case") << QStringLiteral("HTTPS://example.org/") << remote
                                          << QStringLiteral("https://example.org/");
        QTest::newRow("http on 127.0.0.1")
            << QStringLiteral("http://127.0.0.1:8080/channel/") << local
            << QStringLiteral("http://127.0.0.1:8080/channel/");
        QTest::newRow("http on ::1")
            << QStringLiteral("http://[::1]:8080") << local << QStringLiteral("http://[::1]:8080/");
        QTest::newRow("http on localhost") << QStringLiteral(" http://localhost:9/ ") << local
                                           << QStringLiteral("http://localhost:9/");
    }

    void takesEachKindOfValue()
    {
        QFETCH(QString, value);
        QFETCH(int, kind);
        QFETCH(QString, url);
        const ChannelChoice choice = chooseChannel(value, false);
        QCOMPARE(int(choice.address.kind), kind);
        QCOMPARE(choice.address.url.toString(), url);
        QCOMPARE(choice.log, QStringList{QStringLiteral("Update channel: %1").arg(url)});
    }

    void takesAFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const ChannelChoice byPath = chooseChannel(dir.path(), false);
        QCOMPARE(byPath.address.kind, ChannelAddress::Kind::Folder);
        QCOMPARE(QFileInfo(byPath.address.folder).canonicalFilePath(),
                 QFileInfo(dir.path()).canonicalFilePath());
        QCOMPARE(byPath.log.size(), 1);

        const ChannelChoice byUrl =
            chooseChannel(QUrl::fromLocalFile(dir.path()).toString(), false);
        QCOMPARE(byUrl.address.kind, ChannelAddress::Kind::Folder);
        QCOMPARE(QFileInfo(byUrl.address.folder).canonicalFilePath(),
                 QFileInfo(dir.path()).canonicalFilePath());
    }

    void refusesOtherValues_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("http elsewhere") << QStringLiteral("http://example.org/tacetos/");
        QTest::newRow("http on another address") << QStringLiteral("http://192.168.1.2/");
        QTest::newRow("ftp") << QStringLiteral("ftp://127.0.0.1/");
        QTest::newRow("https with no host") << QStringLiteral("https:///tacetos/");
        QTest::newRow("https with a query") << QStringLiteral("https://example.org/?a=1");
        QTest::newRow("relative path") << QStringLiteral("updates");
        QTest::newRow("missing folder")
            << QDir::temp().filePath(QStringLiteral("tacetos-no-such-folder"));
        QTest::newRow("words") << QStringLiteral("not an address");
    }

    void refusesOtherValues()
    {
        QFETCH(QString, value);
        const ChannelChoice choice = chooseChannel(value, false);
        QCOMPARE(choice.address.kind, ChannelAddress::Kind::Remote);
        QCOMPARE(choice.address.url, defaultChannel().url);
        QCOMPARE(choice.log.size(), 2);
        QVERIFY(choice.log.at(0).startsWith(QLatin1String("TACETOS_UPDATE_CHANNEL is ignored")));
        QVERIFY(choice.log.at(1).startsWith(QLatin1String("Update channel: https://github.com/")));
    }

    void refusesAFileAsFolder()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("index"));
        QVERIFY(writeFile(path, "{}"));
        QCOMPARE(chooseChannel(path, false).address.kind, ChannelAddress::Kind::Remote);
    }

    void aPackageBuildKeepsTheDefault()
    {
        QTemporaryDir dir;
        for (const QString &value : {QStringLiteral("https://example.org/"),
                                     QStringLiteral("http://127.0.0.1:8080/"), dir.path()}) {
            const ChannelChoice choice = chooseChannel(value, true);
            QCOMPARE(choice.address.url, defaultChannel().url);
            QCOMPARE(choice.log.size(), 2);
            QVERIFY(choice.log.at(0).contains(QLatin1String("packaged build")));
        }
    }

    void thisBuildReadsTheVariable()
    {
        QTemporaryDir dir;
        qputenv(tacetos::device::kUpdateChannelVariable.data(), dir.path().toLocal8Bit());
        const ChannelChoice choice = channelOfThisBuild();
        qunsetenv(tacetos::device::kUpdateChannelVariable.data());
        QCOMPARE(choice.address.kind, ChannelAddress::Kind::Folder);
        QCOMPARE(channelOfThisBuild().address.url, defaultChannel().url);
    }

    // The reads, on the fake server.

    void readsAnAnswer()
    {
        FakeServer server;
        QVERIFY(server.start());
        server.route("/tacetos-index.json", Route{200, QByteArray("hello"), {}, false, false});
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("tacetos-index.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Read);
        QCOMPARE(read->data, QByteArray("hello"));
    }

    void readsAFileOfExactlyItsLimit()
    {
        FakeServer server;
        QVERIFY(server.start());
        server.route("/a.json", Route{200, QByteArray(100, 'x'), {}, false, true});
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Read);
        QCOMPARE(read->data.size(), 100);
    }

    void a404IsMissing()
    {
        FakeServer server;
        QVERIFY(server.start());
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("tacetos-index.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Missing);
        QVERIFY(read->data.isEmpty());
    }

    void anotherErrorFails()
    {
        FakeServer server;
        QVERIFY(server.start());
        server.route("/a.json", Route{500, QByteArray("broken"), {}, false, false});
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Failed);
        QVERIFY(read->data.isEmpty());
    }

    void followsARedirectOnTheComputer()
    {
        FakeServer server;
        QVERIFY(server.start());
        server.route("/a.json", redirect(server.base() + QStringLiteral("files/a.json")));
        server.route("/files/a.json", Route{200, QByteArray("moved"), {}, false, false});
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Read);
        QCOMPARE(read->data, QByteArray("moved"));
    }

    void refusesARedirectToHttp()
    {
        FakeServer server;
        QVERIFY(server.start());
        // Refused before any connection: nothing goes to that address.
        server.route("/a.json", redirect(QStringLiteral("http://example.invalid/a.json")));
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Failed);
        QVERIFY2(read->reason.contains(QLatin1String("not https")), qPrintable(read->reason));
        QCOMPARE(server.requests(), 1);
    }

    void followsFiveRedirectsButNotSix()
    {
        FakeServer server;
        QVERIFY(server.start());
        for (int i = 1; i <= 6; ++i) {
            server.route("/r" + QByteArray::number(i),
                         redirect(server.base() + QStringLiteral("r%1").arg(i + 1)));
        }
        server.route("/r7", Route{200, QByteArray("end"), {}, false, false});
        QNetworkAccessManager network;
        ChannelReader reader(&network);

        const std::optional<ChannelRead> five =
            readOnce(reader, localAddress(server), QStringLiteral("r2"), 100);
        QVERIFY(five);
        QCOMPARE(five->outcome, ChannelRead::Outcome::Read);
        QCOMPARE(five->data, QByteArray("end"));

        const std::optional<ChannelRead> six =
            readOnce(reader, localAddress(server), QStringLiteral("r1"), 100);
        QVERIFY(six);
        QCOMPARE(six->outcome, ChannelRead::Outcome::Failed);
        QVERIFY2(six->reason.contains(QLatin1String("redirects")), qPrintable(six->reason));
    }

    void failsWhenTheServerStopsSending()
    {
        FakeServer server;
        QVERIFY(server.start());
        Route route;
        route.body = QByteArray(40, 'x');
        route.stall = true;
        server.route("/a.json", route);
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        reader.setTimeout(300);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 2000);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Failed);
        QVERIFY2(read->reason.contains(QLatin1String("no byte")), qPrintable(read->reason));
        QVERIFY(read->data.isEmpty());
    }

    void failsOnAFileLargerThanItsLimit_data()
    {
        QTest::addColumn<bool>("noLength");
        QTest::newRow("with its length") << false;
        QTest::newRow("with no length") << true;
    }

    void failsOnAFileLargerThanItsLimit()
    {
        QFETCH(bool, noLength);
        FakeServer server;
        QVERIFY(server.start());
        server.route("/a.json", Route{200, QByteArray(101, 'x'), {}, false, noLength});
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, localAddress(server), QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Failed);
        QVERIFY2(read->reason.contains(QLatin1String("larger")), qPrintable(read->reason));
        QVERIFY(read->data.isEmpty());
    }

    void failsWithNoServer()
    {
        // A port of the computer that nobody listens to.
        quint16 port = 0;
        {
            QTcpServer closed;
            QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
            port = closed.serverPort();
        }
        ChannelAddress address;
        address.kind = ChannelAddress::Kind::Local;
        address.url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port));
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const std::optional<ChannelRead> read =
            readOnce(reader, address, QStringLiteral("a.json"), 100);
        QVERIFY(read);
        QCOMPARE(read->outcome, ChannelRead::Outcome::Failed);
    }

    void aCancelledReadSaysNothing()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("a.json")), "a"));
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        int count = 0;
        connect(&reader, &ChannelReader::finished, this, [&] { ++count; });
        reader.read(folderAddress(dir.path()), QStringLiteral("a.json"), 100);
        reader.cancel();
        QTest::qWait(50);
        QCOMPARE(count, 0);
    }

    // The reads, in a folder.

    void readsAFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("a.json")), QByteArray(100, 'y')));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("big.json")), QByteArray(101, 'y')));
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder.json")));
        QNetworkAccessManager network;
        ChannelReader reader(&network);
        const ChannelAddress address = folderAddress(dir.path());

        const std::optional<ChannelRead> good =
            readOnce(reader, address, QStringLiteral("a.json"), 100);
        QVERIFY(good);
        QCOMPARE(good->outcome, ChannelRead::Outcome::Read);
        QCOMPARE(good->data, QByteArray(100, 'y'));

        const std::optional<ChannelRead> missing =
            readOnce(reader, address, QStringLiteral("none.json"), 100);
        QVERIFY(missing);
        QCOMPARE(missing->outcome, ChannelRead::Outcome::Missing);

        const std::optional<ChannelRead> big =
            readOnce(reader, address, QStringLiteral("big.json"), 100);
        QVERIFY(big);
        QCOMPARE(big->outcome, ChannelRead::Outcome::Failed);
        QVERIFY(big->data.isEmpty());

        const std::optional<ChannelRead> folder =
            readOnce(reader, address, QStringLiteral("folder.json"), 100);
        QVERIFY(folder);
        QCOMPARE(folder->outcome, ChannelRead::Outcome::Failed);
    }

    // The check.

    void findsAnUpdateForTheModel()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QString(tacetos::device::kUpdateIndexFile)),
                          index(QStringLiteral("0.3.0"))));
        QNetworkAccessManager network;
        tacetos::journal::Journal journal;
        UpdateCheck check(&network, &journal);
        const std::optional<UpdateCheckResult> result = checkOnce(
            check, folderAddress(dir.path()), QStringLiteral("ipod6g"), QStringLiteral("0.2.0"));
        QVERIFY(result);
        QVERIFY(result->update);
        QCOMPARE(result->version, QStringLiteral("0.3.0"));
        QCOMPARE(result->package.manifest, QStringLiteral("tacetos-ipod6g-0.3.0.json"));
        QCOMPARE(journal.entries().size(), 1);
        QCOMPARE(journal.entries().at(0).text, result->logLine);
        QVERIFY(result->logLine.contains(QLatin1String("0.3.0")));

        // A development build of the same three numbers is older than the release.
        const std::optional<UpdateCheckResult> dev =
            checkOnce(check, folderAddress(dir.path()), QStringLiteral("ipod6g"),
                      QStringLiteral("0.3.0-dev+g5a1b2c3d4e5f"));
        QVERIFY(dev);
        QVERIFY(dev->update);
    }

    void findsNoUpdate_data()
    {
        QTest::addColumn<QByteArray>("indexData");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("ipodVersion");
        QTest::addColumn<QString>("reason");
        QTest::newRow("no index") << QByteArray() << QStringLiteral("ipod6g")
                                  << QStringLiteral("0.2.0") << QStringLiteral("has no index");
        QTest::newRow("bad format") << QByteArray("{\"format\": 2}") << QStringLiteral("ipod6g")
                                    << QStringLiteral("0.2.0") << QStringLiteral("refused");
        QTest::newRow("model not in the index")
            << index(QStringLiteral("0.3.0")) << QStringLiteral("ipodvideo")
            << QStringLiteral("0.2.0") << QStringLiteral("no package for ipodvideo");
        QTest::newRow("same version") << index(QStringLiteral("0.3.0")) << QStringLiteral("ipod6g")
                                      << QStringLiteral("0.3.0") << QStringLiteral("not newer");
        QTest::newRow("older version") << index(QStringLiteral("0.3.0")) << QStringLiteral("ipod6g")
                                       << QStringLiteral("0.4.0") << QStringLiteral("not newer");
        QTest::newRow("same three numbers in development")
            << index(QStringLiteral("0.3.0-dev+g000000000000")) << QStringLiteral("ipod6g")
            << QStringLiteral("0.3.0-dev+g5a1b2c3d4e5f") << QStringLiteral("not newer");
        QTest::newRow("version of the iPod not comparable")
            << index(QStringLiteral("0.3.0")) << QStringLiteral("ipod6g") << QStringLiteral("0.2")
            << QStringLiteral("cannot be compared");
        QTest::newRow("version of the channel not comparable")
            << index(QStringLiteral("0.03.0")) << QStringLiteral("ipod6g")
            << QStringLiteral("0.2.0") << QStringLiteral("cannot be compared");
    }

    void findsNoUpdate()
    {
        QFETCH(QByteArray, indexData);
        QFETCH(QString, target);
        QFETCH(QString, ipodVersion);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        if (!indexData.isEmpty())
            QVERIFY(writeFile(dir.filePath(QString(tacetos::device::kUpdateIndexFile)), indexData));
        QNetworkAccessManager network;
        tacetos::journal::Journal journal;
        UpdateCheck check(&network, &journal);
        const std::optional<UpdateCheckResult> result =
            checkOnce(check, folderAddress(dir.path()), target, ipodVersion);
        QVERIFY(result);
        QVERIFY(!result->update);
        QVERIFY(result->version.isEmpty());
        QVERIFY2(result->logLine.startsWith(QLatin1String("No update: ")),
                 qPrintable(result->logLine));
        QVERIFY2(result->logLine.contains(reason), qPrintable(result->logLine));
        QCOMPARE(journal.entries().size(), 1);
    }

    void checksOnTheFakeServer()
    {
        FakeServer server;
        QVERIFY(server.start());
        QNetworkAccessManager network;
        UpdateCheck check(&network, nullptr);

        const std::optional<UpdateCheckResult> none = checkOnce(
            check, localAddress(server), QStringLiteral("ipod6g"), QStringLiteral("0.2.0"));
        QVERIFY(none);
        QVERIFY(!none->update);
        QVERIFY(none->logLine.contains(QLatin1String("HTTP 404")));

        server.route("/tacetos-index.json",
                     Route{200, index(QStringLiteral("0.3.0")), {}, false, false});
        const std::optional<UpdateCheckResult> found = checkOnce(
            check, localAddress(server), QStringLiteral("ipod6g"), QStringLiteral("0.2.0"));
        QVERIFY(found);
        QVERIFY(found->update);

        // An index over its limit is cut off, never kept.
        server.route(
            "/tacetos-index.json",
            Route{200, QByteArray(tacetos::device::kIndexMaxBytes + 1, ' '), {}, false, true});
        const std::optional<UpdateCheckResult> big = checkOnce(
            check, localAddress(server), QStringLiteral("ipod6g"), QStringLiteral("0.2.0"));
        QVERIFY(big);
        QVERIFY(!big->update);
        QVERIFY(big->logLine.contains(QLatin1String("larger")));
    }
};

QTEST_GUILESS_MAIN(TestUpdateChannel)
#include "tst_update_channel.moc"
