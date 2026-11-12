// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Our Classic DFU images against the reference tool, byte for byte
// (docs/specs/bootloader-classic.md, sections 8 and 9). The tool and the parts of
// the image (two fixed blocks and two programs for the iPod) are made outside the
// tree by build-dfu-parts.sh, from the same programs, so the two sides carry the
// same input. For a few synthetic bootloader files, the installer image built by
// our code is compared whole with the one the tool builds, and so is the uninstaller
// image. No difference is allowed.
//
// The tool is only asked for images. Its option for a single boot is never used,
// by any test (rule 5 of the spec). Without TACETOS_DFU_PARTS_DIR every test is
// skipped.

#include "constants.h"
#include "dfuimage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;

namespace {

QString partsDir()
{
    return QString::fromUtf8(TACETOS_DFU_PARTS_DIR);
}

QString toolPath()
{
    QString path = QDir(partsDir()).filePath(QStringLiteral("dfu-tool"));
#ifdef Q_OS_WIN
    path += QStringLiteral(".exe");
#endif
    return path;
}

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 31 + (i >> 8) * 7 + seed) & 0xFF);
    return b;
}

// The bootloader file of section 9: the checksum (big-endian), the tag, the body.
QByteArray bootFile(const QByteArray &body)
{
    quint32 sum = 71;
    for (char c : body)
        sum += quint8(c);
    QByteArray f;
    for (int shift = 24; shift >= 0; shift -= 8)
        f.append(char((sum >> shift) & 0xFF));
    f.append("ip6g", 4);
    return f + body;
}

struct Run
{
    bool started = false;
    int exitCode = -1;
    QString output;
};

Run run(const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(toolPath(), args);
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

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

// The first offset where two images differ, or -1.
qint64 firstDifference(const QByteArray &a, const QByteArray &b)
{
    const qint64 n = qMin(a.size(), b.size());
    for (qint64 i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return i;
    }
    return a.size() == b.size() ? -1 : n;
}

} // namespace

class TestDfuReference : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void partsMatchTheirManifest();
    void installerImagesAreIdentical_data();
    void installerImagesAreIdentical();
    void largestBodyIsIdentical();
    void oneByteTooMuchIsRefusedByBoth();
    void uninstallerImageIsIdentical();

private:
    DfuParts m_parts;
    qint64 largestBody() const;
    QByteArray referenceInstaller(const QByteArray &file, bool *built);
};

void TestDfuReference::initTestCase()
{
    if (partsDir().isEmpty())
        QSKIP("TACETOS_DFU_PARTS_DIR is not set: the Classic comparison is skipped");
    if (!QFileInfo::exists(toolPath()))
        QSKIP("The reference tool is not in TACETOS_DFU_PARTS_DIR (build-dfu-parts.sh)");
    const DfuPartsLoad load = loadDfuParts(partsDir());
    QVERIFY2(load.ok, qPrintable(load.message));
    m_parts = load.parts;
}

void TestDfuReference::partsMatchTheirManifest()
{
    // The manifest has the name, the size and the sha256 of each of the four files.
    const QByteArray manifest = readAll(QDir(partsDir()).filePath(QStringLiteral("manifest.txt")));
    QVERIFY2(!manifest.isEmpty(), "manifest.txt is missing");
    int seen = 0;
    for (const QByteArray &line : manifest.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const QList<QByteArray> cols = line.trimmed().split('\t');
        QCOMPARE(cols.size(), 3);
        const QByteArray data = readAll(QDir(partsDir()).filePath(QString::fromUtf8(cols[0])));
        QVERIFY2(!data.isEmpty(), cols[0].constData());
        QCOMPARE(data.size(), cols[1].toInt());
        QCOMPARE(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(), cols[2]);
        ++seen;
    }
    QCOMPARE(seen, 4);
}

qint64 TestDfuReference::largestBody() const
{
    // The largest body, a multiple of 16, whose image stays within the limit.
    qint64 body =
        (kDfuMaxImageBytes - installerImageSize(m_parts.installerPayload.size(), 0)) & ~qint64(15);
    while (installerImageSize(m_parts.installerPayload.size(), body) > kDfuMaxImageBytes)
        body -= 16;
    return body;
}

QByteArray TestDfuReference::referenceInstaller(const QByteArray &file, bool *built)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        *built = false;
        return QByteArray();
    }
    const QString in = dir.filePath(QStringLiteral("boot.bin"));
    const QString out = dir.filePath(QStringLiteral("installer.dfu"));
    if (!writeAll(in, file)) {
        *built = false;
        return QByteArray();
    }
    // Only the install command, with no option: the double boot, as ours.
    const Run r = run({QStringLiteral("--mkdfu-inst"), in, out});
    *built = r.started && r.exitCode == 0 && QFileInfo::exists(out);
    return *built ? readAll(out) : QByteArray();
}

void TestDfuReference::installerImagesAreIdentical_data()
{
    QTest::addColumn<int>("bodySize");
    QTest::newRow("1 byte") << 1;
    QTest::newRow("16 bytes") << 16;
    QTest::newRow("17 bytes") << 17;
    QTest::newRow("2047 bytes") << 2047;
    QTest::newRow("a few KiB") << 4096 + 5;
    QTest::newRow("50000 bytes") << 50000;
}

void TestDfuReference::installerImagesAreIdentical()
{
    QFETCH(int, bodySize);
    const QByteArray file = bootFile(pattern(bodySize, bodySize));
    bool built = false;
    const QByteArray expected = referenceInstaller(file, &built);
    QVERIFY2(built, "the reference tool did not build the image");

    const DfuImage ours = buildInstallerImage(m_parts, file);
    QVERIFY2(ours.ok, qPrintable(ours.message));
    QCOMPARE(ours.image.size(), expected.size());
    QVERIFY2(ours.image == expected,
             qPrintable(QStringLiteral("first difference at offset 0x%1")
                            .arg(firstDifference(ours.image, expected), 0, 16)));
}

void TestDfuReference::largestBodyIsIdentical()
{
    const qint64 body = largestBody();
    const QByteArray file = bootFile(pattern(int(body), 3));
    bool built = false;
    const QByteArray expected = referenceInstaller(file, &built);
    QVERIFY2(built, "the reference tool did not build the largest image");
    QCOMPARE(qint64(expected.size()), installerImageSize(m_parts.installerPayload.size(), body));
    QVERIFY(expected.size() <= kDfuMaxImageBytes);

    const DfuImage ours = buildInstallerImage(m_parts, file);
    QVERIFY2(ours.ok, qPrintable(ours.message));
    QVERIFY2(ours.image == expected,
             qPrintable(QStringLiteral("first difference at offset 0x%1")
                            .arg(firstDifference(ours.image, expected), 0, 16)));
}

void TestDfuReference::oneByteTooMuchIsRefusedByBoth()
{
    // One byte more than the largest body pads to 16 more, which is over the limit.
    const QByteArray file = bootFile(pattern(int(largestBody()) + 1, 4));
    bool built = true;
    const QByteArray expected = referenceInstaller(file, &built);
    QVERIFY2(!built, "the reference tool built an image over the limit");
    QVERIFY(expected.isEmpty());
    QVERIFY(!buildInstallerImage(m_parts, file).ok);
}

void TestDfuReference::uninstallerImageIsIdentical()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("uninstaller.dfu"));
    const Run r = run({QStringLiteral("--mkdfu-uninst"), QStringLiteral("ipod6g"), out});
    QVERIFY2(r.started && r.exitCode == 0, qPrintable(r.output));
    const QByteArray expected = readAll(out);
    QVERIFY(!expected.isEmpty());

    const DfuImage ours = buildUninstallerImage(m_parts);
    QVERIFY2(ours.ok, qPrintable(ours.message));
    QCOMPARE(ours.image.size(), expected.size());
    QVERIFY2(ours.image == expected,
             qPrintable(QStringLiteral("first difference at offset 0x%1")
                            .arg(firstDifference(ours.image, expected), 0, 16)));
}

QTEST_GUILESS_MAIN(TestDfuReference)
#include "tst_dfu_reference.moc"
