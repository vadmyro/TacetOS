// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The reference partition tool opens our fake iPod images, read only, and
// recognises the right model. The tool is built by build-reference-tools.sh and
// is never changed. Without TACETOS_REFERENCE_TOOLS_DIR every test is skipped.
//
// The 2nd generation nano has sectors of 2048 bytes, which the tool cannot read
// from a plain file (it takes 512). On Linux, with TACETOS_REFERENCE_LOOP=1 and
// a sudo that asks for no password (the CI), the image goes through a loop disk
// made with `losetup --sector-size 2048`, so the tool sees the real size.

#include "fakepod.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::fakepod;

namespace {

QString toolPath()
{
    const QString dir = QString::fromUtf8(TACETOS_REFERENCE_TOOLS_DIR);
    if (dir.isEmpty())
        return QString();
    QString path = QDir(dir).filePath(QStringLiteral("partition-tool"));
#ifdef Q_OS_WIN
    path += QStringLiteral(".exe");
#endif
    return path;
}

QByteArray sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result();
}

struct Run
{
    bool started = false;
    int exitCode = -1;
    QString output;
};

Run run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    Run r;
    r.started = p.waitForStarted(10000);
    if (!r.started)
        return r;
    p.closeWriteChannel();
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished();
    }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.output = QString::fromUtf8(p.readAll());
    return r;
}

} // namespace

class TstReferencePartition : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void listsImageFile_data();
    void listsImageFile();
    void listsNano2gThroughLoopDisk();
    void refusesAMissingFile();

private:
    QString m_tool;
};

void TstReferencePartition::init()
{
    m_tool = toolPath();
    if (m_tool.isEmpty())
        QSKIP("TACETOS_REFERENCE_TOOLS_DIR is not set: no reference tool to compare with");
    if (!QFileInfo(m_tool).isExecutable())
        QSKIP(qPrintable(
            QStringLiteral("no reference tool at %1 (run tests/reference/build-reference-tools.sh)")
                .arg(m_tool)));
}

void TstReferencePartition::listsImageFile_data()
{
    QTest::addColumn<QString>("modelId");
    QTest::addColumn<QString>("modelName");

    // The names the tool prints for these models.
    QTest::newRow("4g") << "ipod-4g" << "4th Generation";
    QTest::newRow("video") << "ipod-video" << "Video (aka 5th Generation)";
    QTest::newRow("nano-1g") << "nano-1g" << "1st Generation Nano";
}

void TstReferencePartition::listsImageFile()
{
    QFETCH(QString, modelId);
    QFETCH(QString, modelName);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakePodSpec spec;
    spec.modelId = modelId;
    spec.path = dir.filePath(QStringLiteral("image.bin"));
    const FakePodResult built = FakePod::build(spec);
    QVERIFY2(built.ok, qPrintable(built.error));
    const QByteArray before = sha256Of(spec.path);
    QVERIFY(!before.isEmpty());

    // -l only reads: the image is opened without write access.
    const Run r = run(m_tool, {spec.path, QStringLiteral("-l")});
    QVERIFY2(r.started, "the reference tool did not start");
    QVERIFY2(r.exitCode == 0, qPrintable(r.output));
    QVERIFY2(r.output.contains(QStringLiteral("Ipod model: %1 (\"winpod\")").arg(modelName)),
             qPrintable(r.output));
    QVERIFY2(r.output.contains(QStringLiteral("Main firmware")), qPrintable(r.output));
    QCOMPARE(sha256Of(spec.path), before);
}

void TstReferencePartition::listsNano2gThroughLoopDisk()
{
#ifndef Q_OS_LINUX
    QSKIP("a loop disk with sectors of 2048 bytes only exists on Linux");
#else
    if (qEnvironmentVariable("TACETOS_REFERENCE_LOOP") != QLatin1String("1"))
        QSKIP(
            "no loop disk: set TACETOS_REFERENCE_LOOP=1 where sudo asks for no password (the CI)");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakePodSpec spec;
    spec.modelId = QStringLiteral("nano-2g");
    spec.path = dir.filePath(QStringLiteral("image.bin"));
    const FakePodResult built = FakePod::build(spec);
    QVERIFY2(built.ok, qPrintable(built.error));
    QCOMPARE(built.sectorSize, 2048);
    const QByteArray before = sha256Of(spec.path);

    const Run attach =
        run(QStringLiteral("sudo"),
            {QStringLiteral("-n"), QStringLiteral("losetup"), QStringLiteral("--find"),
             QStringLiteral("--show"), QStringLiteral("--read-only"),
             QStringLiteral("--sector-size"), QStringLiteral("2048"), spec.path});
    QVERIFY2(attach.started && attach.exitCode == 0, qPrintable(attach.output));
    const QString device = attach.output.trimmed();
    QVERIFY2(device.startsWith(QStringLiteral("/dev/loop")), qPrintable(device));

    const Run r =
        run(QStringLiteral("sudo"), {QStringLiteral("-n"), m_tool, device, QStringLiteral("-l")});
    const Run detach = run(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("losetup"),
                                                    QStringLiteral("-d"), device});
    QVERIFY2(detach.exitCode == 0, qPrintable(detach.output));

    QVERIFY2(r.started, "the reference tool did not start");
    QVERIFY2(r.exitCode == 0, qPrintable(r.output));
    QVERIFY2(r.output.contains(QStringLiteral("Sector size is 2048 bytes")), qPrintable(r.output));
    QVERIFY2(r.output.contains(QStringLiteral("Ipod model: 2nd Generation Nano (\"winpod\")")),
             qPrintable(r.output));
    QCOMPARE(sha256Of(spec.path), before);
#endif
}

void TstReferencePartition::refusesAMissingFile()
{
    // A control: the tool does not invent a model when there is no image.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Run r = run(m_tool, {dir.filePath(QStringLiteral("absent.bin")), QStringLiteral("-l")});
    QVERIFY2(r.started, "the reference tool did not start");
    QVERIFY(r.exitCode != 0);
    QVERIFY(!r.output.contains(QStringLiteral("Ipod model:")));
}

QTEST_GUILESS_MAIN(TstReferencePartition)
#include "tst_reference_partition.moc"
