// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The step `download` of an update (docs/specs/update-channel.md sections 6 and
// 9, "The download"): a local channel made by the test, a fake HTTP server on
// 127.0.0.1, never the real network.

#include "constants.h"
#include "download.h"
#include "package.h"
#include "runner.h"
#include "testpackage.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos;
using namespace tacetos::wizard;
using tacetos::wizard::testing::makeTestPackage;
using tacetos::wizard::testing::TestPackage;
using tacetos::wizard::testing::writeTestIndex;

namespace {

const QString kTarget = QStringLiteral("ipod6g");
const QString kVersion = QStringLiteral("0.3.0");

class Sources : public DownloadSources
{
public:
    QString target() const override { return kTarget; }
    QString ipodVersion() const override { return ipod; }
    update::ChannelAddress channel() const override { return address; }
    QString updatesFolder() const override { return updates; }
    qint64 freeBytes(const QString &) const override { return free; }
    qint64 channelTimeoutMs() const override { return 2000; }

    QString ipod = QStringLiteral("0.2.0");
    update::ChannelAddress address;
    QString updates;
    qint64 free = -1;
};

// What a step is given, without a runner.
class Context : public StepContext
{
public:
    QString serial() const override { return QStringLiteral("SER"); }
    QString model() const override { return QStringLiteral("ipod-classic"); }
    FlowShape shape() const override
    {
        FlowShape shape;
        shape.family = Family::Dfu;
        shape.kind = FlowKind::Update;
        return shape;
    }
    bool isResume() const override { return false; }
    bool stopRequested() const override { return stop; }
    void progress(const QString &, qint64 done, qint64 total) override
    {
        if (total > 0)
            last = {done, total};
    }
    UserAnswer waitForUser(WaitKind, const QJsonObject &) override { return {}; }
    QJsonValue fact(const QString &, const QString &) const override { return {}; }
    void setFact(const QString &, const QJsonValue &) override {}
    QString backupFolder() const override { return QString(); }
    void setBackupFolder(const QString &) override {}
    QString packageManifest() const override { return manifest; }
    QString packageSha256() const override { return sha256; }
    QString updateVersion() const override { return version; }
    void setPackage(const QString &m, const QString &s, const QString &v) override
    {
        manifest = m;
        sha256 = s;
        version = v;
    }

    bool stop = false;
    QPair<qint64, qint64> last{0, 0};
    QString manifest;
    QString sha256;
    QString version;
};

// Serves the files of a folder: 404 for any other path.
class FolderServer : public QObject
{
public:
    explicit FolderServer(const QString &folder) : m_folder(folder)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString base() const
    {
        return QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort());
    }
    int requests = 0;

private:
    void onData(QTcpSocket *socket)
    {
        m_buffer.append(socket->readAll());
        if (!m_buffer.contains("\r\n\r\n"))
            return;
        ++requests;
        const QList<QByteArray> words = m_buffer.left(m_buffer.indexOf("\r\n")).split(' ');
        m_buffer.clear();
        const QString name = QString::fromLatin1(words.value(1)).mid(1);
        QFile file(QDir(m_folder).filePath(name));
        QByteArray answer;
        if (!name.contains(QLatin1Char('/')) && file.open(QIODevice::ReadOnly)) {
            const QByteArray body = file.readAll();
            answer = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                     "\r\nConnection: close\r\n\r\n" + body;
        } else {
            answer = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        }
        socket->write(answer);
        socket->disconnectFromHost();
    }

    QString m_folder;
    QTcpServer m_server;
    QByteArray m_buffer;
};

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList partsUnder(const QString &folder)
{
    QStringList found;
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QString(kPartSuffix)))
            found << path;
    }
    return found;
}

} // namespace

class TstWizardDownload : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    int m_count = 0;

    struct Channel
    {
        QString folder;
        TestPackage package;
        QString zip;
        QString bootloader;
    };

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    // A local channel with the package of 0.3.0 for the Classic and its index.
    Channel channel()
    {
        Channel c;
        c.folder = newFolder(QStringLiteral("channel"));
        c.package = makeTestPackage(c.folder, kTarget, QByteArray(4096, 'b'),
                                    {{QString(device::kFirmwareInfoFile), "Target: ipod6g\n"},
                                     {QStringLiteral("a.txt"), "new"}},
                                    QString(device::kFirmwareFolder), kVersion);
        c.zip = QDir(c.folder).filePath(QStringLiteral("tacetos-ipod6g-0.3.0.zip"));
        c.bootloader =
            QDir(c.folder).filePath(QStringLiteral("tacetos-bootloader-ipod6g-0.3.0.bin"));
        writeTestIndex(c.folder, kVersion, {{kTarget, c.package}});
        return c;
    }

    Sources sourcesOf(const Channel &c)
    {
        Sources s;
        s.address.kind = update::ChannelAddress::Kind::Folder;
        s.address.folder = c.folder;
        s.updates = newFolder(QStringLiteral("updates"));
        return s;
    }

    static QString folderOf(const Sources &s)
    {
        return downloadFolder(s.updates, kVersion, kTarget);
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // The manifest and its two files, in `updates/<version>/<target>/`, checked, and
    // the manifest is the package of the flow.
    void rightFiles()
    {
        const Channel c = channel();
        Sources s = sourcesOf(c);
        Context context;
        DownloadStep step(s);
        const StepOutcome outcome = step.run(context);
        QCOMPARE(outcome.code, QString());
        QCOMPARE(int(outcome.kind), int(StepOutcome::Kind::Done));

        const QString folder = folderOf(s);
        QCOMPARE(context.manifest,
                 QDir(folder).filePath(QStringLiteral("tacetos-ipod6g-0.3.0.json")));
        QCOMPARE(context.sha256, c.package.sha256);
        QCOMPARE(context.version, kVersion);
        QCOMPARE(readAll(QDir(folder).filePath(QStringLiteral("tacetos-ipod6g-0.3.0.zip"))),
                 readAll(c.zip));
        QCOMPARE(QDir(folder).entryList(QDir::Files).size(), 3);
        QVERIFY(partsUnder(s.updates).isEmpty());
        QVERIFY(context.last.second > 0);
        QCOMPARE(context.last.first, context.last.second);
        // Checked as a manifest chosen by hand (install-wizard section 7.1).
        QCOMPARE(checkPackage(context.manifest, kTarget, QString(device::kFirmwareFolder)).code,
                 QString());
    }

    // The index gives a sha256 that the manifest does not have.
    void manifestShaWrong()
    {
        const Channel c = channel();
        const QString index = QDir(c.folder).filePath(QStringLiteral("tacetos-index.json"));
        QByteArray text = readAll(index);
        QVERIFY(text.contains(c.package.sha256.toLatin1()));
        text.replace(c.package.sha256.toLatin1(), QByteArray(64, '0'));
        QVERIFY(write(index, text));

        Sources s = sourcesOf(c);
        Context context;
        const StepOutcome outcome = DownloadStep(s).run(context);
        QCOMPARE(outcome.code, QString(error::downloadDamaged));
        QVERIFY(QDir(folderOf(s)).entryList(QDir::Files).isEmpty());
        QVERIFY(context.manifest.isEmpty());
    }

    // The manifest gives a sha256 that the zip does not have: the file is deleted.
    void fileShaWrong()
    {
        const Channel c = channel();
        QByteArray zip = readAll(c.zip);
        zip[zip.size() - 1] = char(zip.at(zip.size() - 1) ^ 0x55);
        QVERIFY(write(c.zip, zip));

        Sources s = sourcesOf(c);
        Context context;
        const StepOutcome outcome = DownloadStep(s).run(context);
        QCOMPARE(outcome.code, QString(error::downloadDamaged));
        QVERIFY(!QFile::exists(QDir(folderOf(s)).filePath(QFileInfo(c.zip).fileName())));
        QVERIFY(partsUnder(s.updates).isEmpty());
        QVERIFY(context.manifest.isEmpty());
    }

    // A file larger than the size of its manifest: the read stops at the limit.
    void fileLarger()
    {
        const Channel c = channel();
        QVERIFY(write(c.bootloader, QByteArray(5000, 'b')));
        Sources s = sourcesOf(c);
        Context context;
        QCOMPARE(DownloadStep(s).run(context).code, QString(error::downloadDamaged));
        QVERIFY(!QFile::exists(QDir(folderOf(s)).filePath(QFileInfo(c.bootloader).fileName())));
    }

    // A `.part` left by a cut is deleted, and the file starts again from zero.
    void partLeftByACut()
    {
        const Channel c = channel();
        Sources s = sourcesOf(c);
        const QString folder = folderOf(s);
        QVERIFY(QDir().mkpath(folder));
        const QString zip = QDir(folder).filePath(QFileInfo(c.zip).fileName());
        QVERIFY(write(zip + QString(kPartSuffix), "half of it"));
        Context context;
        QCOMPARE(DownloadStep(s).run(context).code, QString());
        QCOMPARE(readAll(zip), readAll(c.zip));
        QVERIFY(partsUnder(s.updates).isEmpty());
    }

    // A file already there with its size and sha256 is not read again: the channel keeps only its
    // index.
    void fileAlreadyThere()
    {
        const Channel c = channel();
        Sources s = sourcesOf(c);
        Context first;
        QCOMPARE(DownloadStep(s).run(first).code, QString());
        QVERIFY(QFile::remove(c.zip));
        QVERIFY(QFile::remove(c.bootloader));
        QVERIFY(QFile::remove(c.package.manifest));
        Context second;
        QCOMPARE(DownloadStep(s).run(second).code, QString());
        QCOMPARE(second.manifest, first.manifest);
        QCOMPARE(second.sha256, c.package.sha256);

        // A file there but wrong is read again.
        const QString zip = QDir(folderOf(s)).filePath(QFileInfo(c.zip).fileName());
        QVERIFY(write(zip, "wrong"));
        Context third;
        QCOMPARE(DownloadStep(s).run(third).code, QString(error::updateGone));
    }

    // The manifest, its two files and the margin do not fit.
    void notEnoughSpace()
    {
        const Channel c = channel();
        Sources s = sourcesOf(c);
        s.free = 40 * 1000 * 1000;
        Context context;
        const StepOutcome outcome = DownloadStep(s).run(context);
        QCOMPARE(outcome.code, QString(error::downloadSpace));
        QVERIFY2(outcome.detail.contains(QStringLiteral("needed, 40 MB free")),
                 qPrintable(outcome.detail));
        QVERIFY(QDir(folderOf(s)).entryList(QDir::Files).isEmpty());

        s.free = device::kDownloadMarginBytes + 100 * 1000;
        Context enough;
        QCOMPARE(DownloadStep(s).run(enough).code, QString());
    }

    void updateGone_data()
    {
        QTest::addColumn<QString>("why");
        QTest::newRow("no index") << "no index";
        QTest::newRow("no entry") << "no entry";
        QTest::newRow("not newer") << "not newer";
        QTest::newRow("bad index") << "bad index";
    }

    // Step 1: the index read again no longer has an update for this iPod.
    void updateGone()
    {
        QFETCH(QString, why);
        const Channel c = channel();
        Sources s = sourcesOf(c);
        const QString index = QDir(c.folder).filePath(QStringLiteral("tacetos-index.json"));
        if (why == QLatin1String("no index"))
            QVERIFY(QFile::remove(index));
        else if (why == QLatin1String("no entry"))
            writeTestIndex(c.folder, kVersion, {});
        else if (why == QLatin1String("not newer"))
            s.ipod = QStringLiteral("0.3.0");
        else
            QVERIFY(write(index, "{\"format\": 2}"));
        Context context;
        QCOMPARE(DownloadStep(s).run(context).code, QString(error::updateGone));
        QVERIFY(context.manifest.isEmpty());
    }

    // A read that fails: here the name of the zip is a folder of the channel.
    void readFails()
    {
        const Channel c = channel();
        QVERIFY(QFile::remove(c.zip));
        QVERIFY(QDir().mkpath(c.zip));
        Sources s = sourcesOf(c);
        Context context;
        QCOMPARE(DownloadStep(s).run(context).code, QString(error::downloadFailed));
    }

    void stopped()
    {
        const Channel c = channel();
        Sources s = sourcesOf(c);
        Context context;
        context.stop = true;
        QCOMPARE(int(DownloadStep(s).run(context).kind), int(StepOutcome::Kind::Cancelled));
    }

    // The step in the thread of a runner, over HTTP: the package ends in the progress file.
    void overHttpInTheRunner()
    {
        const Channel c = channel();
        FolderServer server(c.folder);
        QVERIFY(server.start());
        Sources s = sourcesOf(c);
        s.address.kind = update::ChannelAddress::Kind::Local;
        s.address.url = QUrl(server.base());
        const QString data = newFolder(QStringLiteral("data"));
        // The flow stops after `download`: the next step is not known to this factory.
        WizardRunner runner(data, [&s](const QString &name) -> std::unique_ptr<Step> {
            if (name == step::download)
                return std::make_unique<DownloadStep>(s);
            return nullptr;
        });
        QSignalSpy done(&runner, &WizardRunner::stepDone);
        QSignalSpy stopped(&runner, &WizardRunner::flowStopped);
        WizardRunner::Start start;
        start.serial = QStringLiteral("SERHTTP");
        start.model = QStringLiteral("ipod-classic");
        start.shape.family = Family::Dfu;
        start.shape.kind = FlowKind::Update;
        start.version = kVersion;
        QCOMPARE(runner.start(start), QString());
        QVERIFY(QTest::qWaitFor([&] { return !stopped.isEmpty(); }, 20000));
        runner.waitFinished(5000);
        QCOMPARE(done.size(), 1);
        QCOMPARE(server.requests, 4);

        const ProgressRead read = ProgressStore(data).load(QStringLiteral("SERHTTP"));
        QCOMPARE(int(read.outcome), int(ProgressRead::Outcome::Ok));
        QCOMPARE(read.progress.version, kVersion);
        QCOMPARE(read.progress.packageSha256, c.package.sha256);
        QCOMPARE(read.progress.packageManifest,
                 QDir(folderOf(s)).filePath(QFileInfo(c.package.manifest).fileName()));
        QCOMPARE(read.progress.current, 1);
    }

    // After `finish`: the older versions go, the one installed stays.
    void olderUpdatesRemoved()
    {
        const QString updates = newFolder(QStringLiteral("updates"));
        for (const char *version : {"0.2.0", "0.3.0", "0.3.0-dev+g0123456789ab"}) {
            const QString folder = downloadFolder(updates, QString::fromLatin1(version), kTarget);
            QVERIFY(QDir().mkpath(folder));
            QVERIFY(write(QDir(folder).filePath(QStringLiteral("x")), "x"));
        }
        removeOlderUpdates(updates, kVersion);
        QCOMPARE(QDir(updates).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{kVersion});
    }
};

QTEST_GUILESS_MAIN(TstWizardDownload)
#include "tst_wizard_download.moc"
