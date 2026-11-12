// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The DFU image of the iPod Classic (docs/specs/bootloader-classic.md sections 8
// and 9), built from fake blocks and fake programs made here. The real ones are
// generated outside the repository.

#include "dfuimage.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;

namespace {

constexpr int kPayload = 5400;
constexpr int kUninstallPayload = 6000;
constexpr int kMaxImage = 0x20000;

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 7 + seed) & 0xFF);
    return b;
}

DfuParts fakeParts()
{
    DfuParts p;
    p.headerSignature = pattern(16, 0x31);
    p.certificate = pattern(698, 0x55);
    p.installerPayload = pattern(kPayload, 0x11);
    // The last 0x50 bytes are the bootloader header, empty in the program.
    p.installerPayload.replace(kPayload - 0x50, 0x50, QByteArray(0x50, '\0'));
    p.uninstallerPayload = pattern(kUninstallPayload, 0x77);
    return p;
}

// Another way to write the checksum of section 9, big-endian in the file.
QByteArray bootFile(const QByteArray &body, const char *tag = "ip6g", int add = 0)
{
    quint32 sum = 71;
    for (char c : body)
        sum += quint8(c);
    sum += quint32(add);
    QByteArray f;
    for (int shift = 24; shift >= 0; shift -= 8)
        f.append(char((sum >> shift) & 0xFF));
    f.append(tag, 4);
    return f + body;
}

quint32 le32(const QByteArray &b, int at)
{
    return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8 |
           quint32(quint8(b[at + 2])) << 16 | quint32(quint8(b[at + 3])) << 24;
}

quint32 bigEndian32(const QByteArray &f)
{
    return quint32(quint8(f[0])) << 24 | quint32(quint8(f[1])) << 16 | quint32(quint8(f[2])) << 8 |
           quint32(quint8(f[3]));
}

// The largest body that fits, written from the formula of section 8.3:
// 0x30C + payload + 0x7B0 + padded body <= 0x20000.
constexpr int kLargestBody = (kMaxImage - 0x30C - kPayload - 0x7B0) / 16 * 16;

QByteArray built(const QByteArray &body)
{
    const DfuImage r = buildInstallerImage(fakeParts(), bootFile(body));
    return r.image;
}

} // namespace

class TestDfuImage : public QObject
{
    Q_OBJECT

private slots:
    void prefixFields();
    void sizeFormulas_data();
    void sizeFormulas();
    void largestBodyFits();
    void oneByteMoreIsRefused();
    void bootloaderHeader();
    void singleBootByteIsAlwaysZero();
    void patchedByteIsNotReadyToSend();
    void otherDamageIsNotReadyToSend();
    void bootFileChecks();
    void bootFileChecksOrder();
    void bootFileIsReadOnce();
    void uninstallerImage();
    void withoutPartsFolder();
    void partsFolder();
};

void TestDfuImage::prefixFields()
{
    const QByteArray img = built(QByteArray(1, 'x'));
    QCOMPARE(img.left(4), QByteArray("8702"));
    QCOMPARE(img.mid(4, 3), QByteArray("1.0"));
    QCOMPARE(quint8(img[7]), quint8(3));
    QCOMPARE(le32(img, 0x08), quint32(0));
    QCOMPARE(le32(img, 0x0C), quint32(0));
    QCOMPARE(le32(img, 0x10), quint32(0x300));
    QCOMPARE(le32(img, 0x14), quint32(0xFFFFF850));
    QCOMPARE(le32(img, 0x18), quint32(698));
    QCOMPARE(img.mid(0x1C, 36), QByteArray(36, '\0'));
    QCOMPARE(img.mid(0x40, 16), fakeParts().headerSignature);
    // The certificate area, but for its last 4 bytes.
    QCOMPARE(img.mid(0x50, 698 - 4), fakeParts().certificate.left(698 - 4));
    QCOMPARE(le32(img, 0x306), quint32(0x2200030C));
    QCOMPARE(img.mid(0x30A, 2), QByteArray(2, '\0'));
    // The payload starts at 0x30C, and its start is the program's.
    QCOMPARE(img.mid(0x30C, kPayload - 0x50), fakeParts().installerPayload.left(kPayload - 0x50));
}

void TestDfuImage::sizeFormulas_data()
{
    QTest::addColumn<int>("body");
    QTest::addColumn<int>("padded");
    QTest::newRow("1") << 1 << 16;
    QTest::newRow("16") << 16 << 16;
    QTest::newRow("17") << 17 << 32;
    QTest::newRow("33") << 33 << 48;
}

void TestDfuImage::sizeFormulas()
{
    QFETCH(int, body);
    QFETCH(int, padded);
    const QByteArray img = built(pattern(body, 3));
    // 0x30C + payload + 0x7B0 + padded body, with the numbers written out.
    QCOMPARE(img.size(), 780 + kPayload + 1968 + padded);
    QCOMPARE(installerImageSize(kPayload, body), qint64(img.size()));
    // The body, then zeros up to the padding, and nothing after.
    const int bodyAt = 0x30C + kPayload - 0x50 + 0x800;
    QCOMPARE(img.mid(bodyAt, body), pattern(body, 3));
    QCOMPARE(img.mid(bodyAt + body), QByteArray(padded - body, '\0'));
    // Zeros from the end of the payload to the body.
    QCOMPARE(img.mid(0x30C + kPayload, 0x7B0), QByteArray(0x7B0, '\0'));
}

void TestDfuImage::largestBodyFits()
{
    QCOMPARE(kLargestBody % 16, 0);
    const DfuImage r = buildInstallerImage(fakeParts(), bootFile(pattern(kLargestBody, 9)));
    QVERIFY2(r.ok, qPrintable(r.message));
    // The padding makes the largest image a few bytes under the limit; 16 more would pass it.
    QCOMPARE(r.image.size(), 780 + kPayload + 1968 + kLargestBody);
    QVERIFY(r.image.size() <= kMaxImage);
    QVERIFY(r.image.size() + 16 > kMaxImage);
    // A body of 15 bytes less pads to the same size.
    const DfuImage s = buildInstallerImage(fakeParts(), bootFile(pattern(kLargestBody - 15, 9)));
    QVERIFY(s.ok);
    QCOMPARE(s.image.size(), r.image.size());
}

void TestDfuImage::oneByteMoreIsRefused()
{
    const DfuImage r = buildInstallerImage(fakeParts(), bootFile(pattern(kLargestBody + 1, 9)));
    QVERIFY(!r.ok);
    QVERIFY(r.image.isEmpty());
    QCOMPARE(r.message, QStringLiteral("The bootloader is too big for the iPod."));
}

void TestDfuImage::bootloaderHeader()
{
    for (int body : {1, 16, 17, 1000}) {
        const QByteArray img = built(pattern(body, 5));
        const int h = 0x30C + kPayload - 0x50;
        const int paddedBody = (body + 15) / 16 * 16;
        QCOMPARE(img.mid(h, 4), QByteArray("8702"));
        QCOMPARE(img.mid(h + 4, 3), QByteArray("1.0"));
        QCOMPARE(quint8(img[h + 7]), quint8(2));
        QCOMPARE(le32(img, h + 0x0C), quint32(paddedBody));
        // Every other byte of the header is still the program's, here zero.
        QByteArray rest = img.mid(h, 0x50);
        rest.replace(0, 7, QByteArray(7, '\0'));
        rest[7] = 0;
        rest.replace(0x0C, 4, QByteArray(4, '\0'));
        QCOMPARE(rest, QByteArray(0x50, '\0'));
        // The body is exactly 0x800 bytes after the start of the header.
        QCOMPARE(img.mid(h + 0x800, 1), pattern(body, 5).left(1));
        QCOMPARE(quint8(img[h + 0x40]), quint8(0));
    }
}

void TestDfuImage::singleBootByteIsAlwaysZero()
{
    // A program whose header already holds a non-zero byte there: the builder
    // still writes 0, and keeps the other bytes it does not fill.
    DfuParts p = fakeParts();
    const int h = kPayload - 0x50;
    p.installerPayload[h + 0x40] = char(1);
    p.installerPayload[h + 0x45] = char(0x66);
    const DfuImage r = buildInstallerImage(p, bootFile(pattern(40, 2)));
    QVERIFY(r.ok);
    QCOMPARE(quint8(r.image[0x30C + h + 0x40]), quint8(0));
    QCOMPARE(quint8(r.image[0x30C + h + 0x45]), quint8(0x66));
    QVERIFY(checkInstallerImageReadyToSend(r.image, kPayload).ok);
}

void TestDfuImage::patchedByteIsNotReadyToSend()
{
    QByteArray img = built(pattern(300, 4));
    const DfuSendCheck good = checkInstallerImageReadyToSend(img, kPayload);
    QVERIFY2(good.ok, qPrintable(good.message));
    const int at = 0x30C + kPayload - 0x50 + 0x40;
    QCOMPARE(quint8(img[at]), quint8(0));
    for (int value : {1, 2, 0x80, 0xFF}) {
        QByteArray patched = img;
        patched[at] = char(value);
        const DfuSendCheck r = checkInstallerImageReadyToSend(patched, kPayload);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains(QLatin1String("double boot")));
    }
    // The original copy is untouched.
    QVERIFY(checkInstallerImageReadyToSend(img, kPayload).ok);
}

void TestDfuImage::otherDamageIsNotReadyToSend()
{
    const QByteArray img = built(pattern(300, 4));
    QVERIFY(!checkInstallerImageReadyToSend(QByteArray(), kPayload).ok);
    QVERIFY(!checkInstallerImageReadyToSend(img.left(0x30C), kPayload).ok);
    QVERIFY(!checkInstallerImageReadyToSend(img + QByteArray(kMaxImage, '\0'), kPayload).ok);
    // The uninstaller image is not an installer image.
    QVERIFY(!checkInstallerImageReadyToSend(buildUninstallerImage(fakeParts()).image, kPayload).ok);
    QByteArray tag = img;
    tag[0] = 'X';
    QVERIFY(!checkInstallerImageReadyToSend(tag, kPayload).ok);
    QByteArray enc = img;
    enc[0x30C + kPayload - 0x50 + 7] = char(3);
    QVERIFY(!checkInstallerImageReadyToSend(enc, kPayload).ok);
    QByteArray size = img;
    size[0x30C + kPayload - 0x50 + 0x0C] = char(size[0x30C + kPayload - 0x50 + 0x0C] + 16);
    QVERIFY(!checkInstallerImageReadyToSend(size, kPayload).ok);
}

void TestDfuImage::bootFileChecks()
{
    const QByteArray body = pattern(100, 8);
    const ClassicBootCheck good = checkClassicBootloaderFile(bootFile(body), kPayload);
    QVERIFY(good.ok());
    QCOMPARE(good.body, body);

    const QString another =
        QStringLiteral("This file is a bootloader for another model, or not a bootloader.");
    const ClassicBootCheck tag = checkClassicBootloaderFile(bootFile(body, "ip5g"), kPayload);
    QCOMPARE(tag.status, ClassicBootStatus::WrongModel);
    QCOMPARE(tag.message, another);

    for (int add : {1, -1}) {
        const ClassicBootCheck sum =
            checkClassicBootloaderFile(bootFile(body, "ip6g", add), kPayload);
        QCOMPARE(sum.status, ClassicBootStatus::BadChecksum);
        QCOMPARE(sum.message, QStringLiteral("The bootloader file is damaged."));
        QVERIFY(sum.body.isEmpty());
    }

    // 8 bytes: a prefix and no body. 9 bytes: the smallest file.
    QCOMPARE(checkClassicBootloaderFile(bootFile(QByteArray()), kPayload).status,
             ClassicBootStatus::TooShort);
    QCOMPARE(checkClassicBootloaderFile(QByteArray(), kPayload).status,
             ClassicBootStatus::TooShort);
    QVERIFY(checkClassicBootloaderFile(bootFile(QByteArray(1, 'x')), kPayload).ok());

    // A body whose byte sum wraps around 32 bits: 17 000 000 bytes of 0xFF add up to
    // 4 335 000 000, above 2^32. The checksum of the file is the wrapped value, and
    // the file passes check 3 (it is then refused by check 4, for its size).
    const QByteArray wrap = bootFile(QByteArray(17000000, char(0xFF)));
    const quint64 exact = 71 + quint64(17000000) * 255;
    QVERIFY(exact > 0xFFFFFFFFull);
    QCOMPARE(bigEndian32(wrap), quint32(exact & 0xFFFFFFFFull));
    QCOMPARE(checkClassicBootloaderFile(wrap, kPayload).status, ClassicBootStatus::TooBig);
    // The wrapped value plus one is a bad checksum.
    QCOMPARE(
        checkClassicBootloaderFile(bootFile(QByteArray(17000000, char(0xFF)), "ip6g", 1), kPayload)
            .status,
        ClassicBootStatus::BadChecksum);
}

void TestDfuImage::bootFileChecksOrder()
{
    // Too big, but with a wrong tag: the tag is reported first (check 2 before 4).
    QByteArray big = bootFile(pattern(kLargestBody + 100, 1), "ip5g");
    QCOMPARE(checkClassicBootloaderFile(big, kPayload).status, ClassicBootStatus::WrongModel);
    // Too big, wrong checksum: the checksum first (check 3 before 4).
    big = bootFile(pattern(kLargestBody + 100, 1), "ip6g", 1);
    QCOMPARE(checkClassicBootloaderFile(big, kPayload).status, ClassicBootStatus::BadChecksum);
    big = bootFile(pattern(kLargestBody + 100, 1));
    const ClassicBootCheck c = checkClassicBootloaderFile(big, kPayload);
    QCOMPARE(c.status, ClassicBootStatus::TooBig);
    QCOMPARE(c.message, QStringLiteral("The bootloader is too big for the iPod."));
    // Too short and wrong tag at once: check 1.
    QCOMPARE(checkClassicBootloaderFile(QByteArray("abc"), kPayload).status,
             ClassicBootStatus::TooShort);
}

void TestDfuImage::bootFileIsReadOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("boot.bin"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bootFile(pattern(64, 6)));
    }
    const ClassicBootCheck good = readClassicBootloaderFile(path, kPayload);
    QVERIFY(good.ok());
    QCOMPARE(good.body, pattern(64, 6));

    // A file longer than the image can be is refused from its size alone: here a
    // sparse file of 1 GiB whose content, never read, would be a valid file.
    const QString huge = dir.filePath(QStringLiteral("huge.bin"));
    {
        QFile f(huge);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize(qint64(1) << 30));
    }
    const ClassicBootCheck h = readClassicBootloaderFile(huge, kPayload);
    QCOMPARE(h.status, ClassicBootStatus::TooBig);
    QCOMPARE(h.message, QStringLiteral("The bootloader is too big for the iPod."));
    QCOMPARE(readClassicBootloaderFile(dir.filePath(QStringLiteral("none.bin")), kPayload).status,
             ClassicBootStatus::TooShort);
}

void TestDfuImage::uninstallerImage()
{
    const DfuParts p = fakeParts();
    const DfuImage r = buildUninstallerImage(p);
    QVERIFY(r.ok);
    QCOMPARE(r.image.size(), 0x30C + kUninstallPayload);
    QCOMPARE(r.image.left(0x30C), built(QByteArray(1, 'x')).left(0x30C));
    QCOMPARE(r.image.mid(0x30C), p.uninstallerPayload);
    QCOMPARE(le32(r.image, 0x306), quint32(0x2200030C));
    QCOMPARE(r.image.left(4), QByteArray("8702"));
    // Nothing after the program: no bootloader header, no padding.
    QCOMPARE(r.image.right(64), p.uninstallerPayload.right(64));

    DfuParts bad = p;
    bad.uninstallerPayload = QByteArray(16 * 1024 + 1, 'x');
    QVERIFY(!buildUninstallerImage(bad).ok);
    bad.uninstallerPayload = QByteArray(16 * 1024, 'x');
    QVERIFY(buildUninstallerImage(bad).ok);
}

void TestDfuImage::withoutPartsFolder()
{
    const QString text = QStringLiteral("Classic install is not available in this build");
    const DfuPartsLoad p = loadDfuParts(QString());
    QVERIFY(!p.ok);
    QCOMPARE(p.message, text);
    const DfuImage r = buildClassicInstallerImage(bootFile(pattern(10, 1)), QString());
    QVERIFY(!r.ok);
    QCOMPARE(r.message, text);
    QVERIFY(r.image.isEmpty());
    QCOMPARE(buildClassicUninstallerImage(QString()).message, text);
    // This test build was configured without the folder.
    QVERIFY(builtInDfuPartsDir().isEmpty() || QDir(builtInDfuPartsDir()).exists() ||
            !buildClassicInstallerImage(bootFile(pattern(10, 1))).ok);
}

void TestDfuImage::partsFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const DfuParts p = fakeParts();
    auto write = [&](const char *name, const QByteArray &data) {
        QFile f(dir.filePath(QLatin1String(name)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    };
    write("header-signature.bin", p.headerSignature);
    write("certificate.bin", p.certificate);
    write("installer-payload.bin", p.installerPayload);
    QVERIFY(!loadDfuParts(dir.path()).ok); // the 4th is missing
    write("uninstaller-payload.bin", p.uninstallerPayload);
    const DfuPartsLoad l = loadDfuParts(dir.path());
    QVERIFY2(l.ok, qPrintable(l.message));
    QCOMPARE(l.parts.installerPayload, p.installerPayload);

    const QByteArray file = bootFile(pattern(200, 2));
    QCOMPARE(buildClassicInstallerImage(file, dir.path()).image,
             buildInstallerImage(p, file).image);
    QCOMPARE(buildClassicUninstallerImage(dir.path()).image, buildUninstallerImage(p).image);

    write("certificate.bin", QByteArray(697, 'x'));
    const DfuPartsLoad bad = loadDfuParts(dir.path());
    QVERIFY(!bad.ok);
    QVERIFY(bad.message.contains(QLatin1String("wrong size")));
}

QTEST_GUILESS_MAIN(TestDfuImage)
#include "tst_installer_dfuimage.moc"
