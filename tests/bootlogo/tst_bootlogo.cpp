// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The boot logo library (docs/specs/boot-screen.md sections 3, 8.3, 8.4 and 12).

#include "bootlogo.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace tacetos::bootlogo;

namespace {

const Screen kClassic{320, 240, true};
const Screen kNano{176, 132, true};
const Screen kMini{138, 110, false};

QImage filled(int w, int h, QColor colour)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(colour);
    return image;
}

quint16 colourAt(const Logo &logo, int x, int y)
{
    return qFromLittleEndian<quint16>(logo.pixels.constData() + 2 * (y * logo.width + x));
}

int levelAt(const Logo &logo, int x, int y)
{
    return logo.pixels.at(y * logo.width + x);
}

// A valid file for `screen`, made by hand from the table of section 3.2.
QByteArray handMade(const Screen &screen)
{
    const int bytes = screen.colour ? 2 : 1;
    QByteArray pixels(screen.width * screen.height * bytes, '\x01');
    QByteArray file("TLGO", 4);
    file.append(char(1));
    file.append(char(screen.colour ? 1 : 2));
    char word[4];
    qToLittleEndian<quint16>(screen.width, word);
    file.append(word, 2);
    qToLittleEndian<quint16>(screen.height, word);
    file.append(word, 2);
    file.append(2, '\0');
    qToLittleEndian<quint32>(crc32(pixels), word);
    file.append(word, 4);
    return file + pixels;
}

} // namespace

class TestBootLogo : public QObject
{
    Q_OBJECT

private slots:
    void crcOfKnownData();
    void handMadeFileIsRead_data();
    void handMadeFileIsRead();
    void writtenThenReadBack_data();
    void writtenThenReadBack();
    void refused_data();
    void refused();
    void colourRounding();
    void greyLevelsOfFlatPictures();
    void floydSteinbergByHand();
    void floydSteinbergKeepsTheMean();
    void fitAndFill_data();
    void fitAndFill();
    void transparentIsBlack();
    void previewOfTheReadPixels();
    void newFileLeftByACut();
    void removeWithAndWithoutFile();
    void writeWithoutFolder();
    void pictureRefused();
};

void TestBootLogo::crcOfKnownData()
{
    // The check value of the zip CRC-32.
    QCOMPARE(crc32(QByteArrayLiteral("123456789")), 0xCBF43926U);
    QCOMPARE(crc32(QByteArray()), 0U);
    // A 4 x 1 red picture: 0xF800 four times, little-endian.
    const Logo logo = fromImage(filled(4, 1, Qt::red), Screen{4, 1, true}, false);
    QCOMPARE(logo.pixels, QByteArray("\x00\xf8\x00\xf8\x00\xf8\x00\xf8", 8));
    QCOMPARE(crc32(logo.pixels), 0x4246E80DU);
    QCOMPARE(qFromLittleEndian<quint32>(encode(logo).constData() + 12), 0x4246E80DU);
}

void TestBootLogo::handMadeFileIsRead_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("colour");
    QTest::newRow("320x240 colour") << 320 << 240 << true;
    QTest::newRow("176x132 colour") << 176 << 132 << true;
    QTest::newRow("138x110 grey") << 138 << 110 << false;
}

void TestBootLogo::handMadeFileIsRead()
{
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(bool, colour);
    const Screen screen{width, height, colour};
    const QByteArray file = handMade(screen);
    QCOMPARE(file.size(), 16 + width * height * (colour ? 2 : 1));
    const auto logo = decode(file, screen);
    QVERIFY(logo);
    QCOMPARE(logo->kind, colour ? Kind::Colour : Kind::Grey);
    QCOMPARE(logo->width, width);
    QCOMPARE(logo->height, height);
    // Our encoder gives the same bytes.
    QCOMPARE(encode(*logo), file);
}

void TestBootLogo::writtenThenReadBack_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("colour");
    QTest::addColumn<bool>("black");
    for (const Screen &s : {kClassic, kNano, kMini}) {
        const QByteArray name = QByteArray::number(s.width) + 'x' + QByteArray::number(s.height);
        QTest::newRow(name + " picture") << s.width << s.height << s.colour << false;
        QTest::newRow(name + " black") << s.width << s.height << s.colour << true;
    }
}

void TestBootLogo::writtenThenReadBack()
{
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(bool, colour);
    QFETCH(bool, black);
    const Screen screen{width, height, colour};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage picture(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            picture.setPixel(x, y, qRgb(x * 255 / width, y * 255 / height, 128));
    const Logo logo = black ? Logo{} : fromImage(picture, screen, false);
    const QByteArray file = encode(logo);
    QCOMPARE(file.size(), black ? 16 : 16 + width * height * (colour ? 2 : 1));

    QString error;
    QVERIFY2(writeLogo(dir.path(), file, &error), qPrintable(error));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("boot-logo.bin")});
    const OnIpod found = readLogo(dir.path(), screen);
    QCOMPARE(found.found, black ? Found::Black : Found::Picture);
    if (!black)
        QCOMPARE(found.logo->pixels, logo.pixels);

    // Written again over the first one.
    QVERIFY2(writeLogo(dir.path(), encode(Logo{}), &error), qPrintable(error));
    QCOMPARE(readLogo(dir.path(), screen).found, Found::Black);
}

void TestBootLogo::refused_data()
{
    QTest::addColumn<QByteArray>("file");
    QTest::addColumn<bool>("colourScreen");

    const QByteArray colour = handMade(kNano);
    const QByteArray grey = handMade(kMini);
    const auto changed = [](QByteArray file, int at, char value) {
        file[at] = value;
        return file;
    };
    QTest::newRow("too short") << colour.left(15) << true;
    QTest::newRow("magic") << changed(colour, 0, 'X') << true;
    QTest::newRow("version 2") << changed(colour, 4, 2) << true;
    QTest::newRow("reserved") << changed(colour, 10, 1) << true;
    QTest::newRow("kind 3") << changed(colour, 5, 3) << true;
    QTest::newRow("colour on a grey screen") << colour << false;
    QTest::newRow("grey on a colour screen") << grey << true;
    QTest::newRow("other width") << changed(colour, 6, char(175)) << true;
    QTest::newRow("other height") << changed(colour, 8, char(131)) << true;
    QTest::newRow("one byte short") << colour.chopped(1) << true;
    QTest::newRow("one byte more") << colour + QByteArray(1, '\x01') << true;
    QTest::newRow("crc") << changed(colour, 12, colour.at(12) ^ 1) << true;
    QTest::newRow("pixel changed") << changed(colour, 100, 7) << true;
    QTest::newRow("black with pixels") << encode(Logo{}) + QByteArray(2, '\0') << true;
}

void TestBootLogo::refused()
{
    QFETCH(QByteArray, file);
    QFETCH(bool, colourScreen);
    const Screen screen = colourScreen ? kNano : kMini;
    QVERIFY(!decode(file, screen));

    QTemporaryDir dir;
    QFile out(logoPath(dir.path()));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(file);
    out.close();
    QCOMPARE(readLogo(dir.path(), screen).found, Found::Unusable);
}

void TestBootLogo::colourRounding()
{
    const struct
    {
        QRgb in;
        quint16 out;
    } cases[] = {
        {qRgb(255, 255, 255), 0xFFFF}, {qRgb(0, 0, 0), 0x0000},   {qRgb(255, 0, 0), 0xF800},
        {qRgb(0, 255, 0), 0x07E0},     {qRgb(0, 0, 255), 0x001F}, {qRgb(128, 128, 128), 0x8410},
        {qRgb(8, 4, 8), 0x0821},       {qRgb(4, 2, 4), 0x0000},
    };
    QImage picture(8, 1, QImage::Format_RGB32);
    for (int i = 0; i < 8; ++i)
        picture.setPixel(i, 0, cases[i].in);
    const Logo logo = fromImage(picture, Screen{8, 1, true}, false);
    QCOMPARE(logo.kind, Kind::Colour);
    for (int i = 0; i < 8; ++i)
        QCOMPARE(colourAt(logo, i, 0), cases[i].out);
}

void TestBootLogo::greyLevelsOfFlatPictures()
{
    const int expected[] = {0, 1, 2, 3};
    const int greys[] = {0, 85, 170, 255};
    for (int i = 0; i < 4; ++i) {
        const Logo logo = fromImage(
            filled(kMini.width, kMini.height, QColor(greys[i], greys[i], greys[i])), kMini, false);
        QCOMPARE(logo.kind, Kind::Grey);
        QCOMPARE(logo.pixels, QByteArray(kMini.width * kMini.height, char(expected[i])));
    }
}

void TestBootLogo::floydSteinbergByHand()
{
    // 2 x 2 of grey 64. Errors in sixteenths, rounded to the nearest:
    // (0,0) 64 > 1, error -21; (1,0) 64 - 9 = 55 > 1, error -30;
    // (0,1) 64 - 12 = 52 > 1, error -33; (1,1) 64 - 25 = 39 > 0.
    const Logo logo = fromImage(filled(2, 2, QColor(64, 64, 64)), Screen{2, 2, false}, false);
    QCOMPARE(logo.pixels, QByteArray("\x01\x01\x01\x00", 4));
}

void TestBootLogo::floydSteinbergKeepsTheMean()
{
    // A fixed ramp, black on the left to white on the right: each column keeps its grey on
    // average, and the levels never go down from one column block to the next.
    const Screen screen{128, 64, false};
    QImage ramp(128, 64, QImage::Format_RGB32);
    for (int x = 0; x < 128; ++x)
        for (int y = 0; y < 64; ++y)
            ramp.setPixel(x, y, qRgb(x * 2, x * 2, x * 2));
    const Logo logo = fromImage(ramp, screen, false);
    double previous = -1;
    for (int block = 0; block < 128; block += 16) {
        double sum = 0;
        double grey = 0;
        for (int x = block; x < block + 16; ++x) {
            grey += x * 2;
            for (int y = 0; y < 64; ++y)
                sum += levelAt(logo, x, y) * 85;
        }
        const double mean = sum / (16 * 64);
        QVERIFY2(qAbs(mean - grey / 16) < 6, qPrintable(QString::number(mean)));
        QVERIFY(mean >= previous);
        previous = mean;
    }
}

void TestBootLogo::fitAndFill_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("colour");
    QTest::addColumn<bool>("wide");
    for (const Screen &s : {kClassic, kNano, kMini}) {
        const QByteArray name = QByteArray::number(s.width) + 'x' + QByteArray::number(s.height);
        QTest::newRow(name + " wide") << s.width << s.height << s.colour << true;
        QTest::newRow(name + " tall") << s.width << s.height << s.colour << false;
    }
}

void TestBootLogo::fitAndFill()
{
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(bool, colour);
    QFETCH(bool, wide);
    const Screen screen{width, height, colour};
    // Three times wider than high, or the other way round.
    const QImage picture = wide ? filled(900, 300, Qt::white) : filled(300, 900, Qt::white);
    const auto white = [&](const Logo &logo, int x, int y) {
        return colour ? colourAt(logo, x, y) == 0xFFFF : levelAt(logo, x, y) == 3;
    };
    const auto black = [&](const Logo &logo, int x, int y) {
        return colour ? colourAt(logo, x, y) == 0 : levelAt(logo, x, y) == 0;
    };
    // Only the size, the bars and the kind: smooth scaling can differ a little between systems.
    const Logo fit = fromImage(picture, screen, false);
    QCOMPARE(fit.kind, colour ? Kind::Colour : Kind::Grey);
    QCOMPARE(fit.width, width);
    QCOMPARE(fit.height, height);
    QCOMPARE(fit.pixels.size(), width * height * (colour ? 2 : 1));
    QVERIFY(white(fit, width / 2, height / 2));
    if (wide) {
        QVERIFY(black(fit, width / 2, 1));
        QVERIFY(black(fit, width / 2, height - 2));
    } else {
        QVERIFY(black(fit, 1, height / 2));
        QVERIFY(black(fit, width - 2, height / 2));
    }

    const Logo fill = fromImage(picture, screen, true);
    QCOMPARE(fill.pixels.size(), fit.pixels.size());
    for (int y = 0; y < height; y += 7)
        for (int x = 0; x < width; x += 7)
            QVERIFY(white(fill, x, y));
}

void TestBootLogo::transparentIsBlack()
{
    const Logo logo = fromImage(filled(16, 16, Qt::transparent), Screen{16, 16, true}, false);
    QCOMPARE(logo.pixels, QByteArray(16 * 16 * 2, '\0'));
}

void TestBootLogo::previewOfTheReadPixels()
{
    const Logo colour = fromImage(filled(4, 4, QColor(255, 0, 0)), Screen{4, 4, true}, false);
    QCOMPARE(toImage(colour, Screen{4, 4, true}).pixel(1, 1), qRgb(255, 0, 0));
    const Logo grey = fromImage(filled(4, 4, QColor(170, 170, 170)), Screen{4, 4, false}, false);
    QCOMPARE(toImage(grey, Screen{4, 4, false}).pixel(2, 3), qRgb(170, 170, 170));
    const QImage black = toImage(Logo{}, kMini);
    QCOMPARE(black.size(), QSize(138, 110));
    QCOMPARE(black.pixel(0, 0), qRgb(0, 0, 0));
}

void TestBootLogo::newFileLeftByACut()
{
    QTemporaryDir dir;
    QFile cut(logoPath(dir.path()) + QStringLiteral(".new"));
    QVERIFY(cut.open(QIODevice::WriteOnly));
    cut.write("half a file");
    cut.close();
    QString error;
    QVERIFY(writeLogo(dir.path(), encode(Logo{}), &error));
    QVERIFY(!cut.exists());
    QCOMPARE(readLogo(dir.path(), kClassic).found, Found::Black);
}

void TestBootLogo::removeWithAndWithoutFile()
{
    QTemporaryDir dir;
    QString error;
    // Nothing to remove is not a failure.
    QVERIFY(removeLogo(dir.path(), &error));
    QVERIFY(writeLogo(dir.path(), encode(Logo{}), &error));
    QFile cut(logoPath(dir.path()) + QStringLiteral(".new"));
    QVERIFY(cut.open(QIODevice::WriteOnly));
    cut.close();
    QVERIFY(removeLogo(dir.path(), &error));
    QVERIFY(QDir(dir.path()).isEmpty());
    QCOMPARE(readLogo(dir.path(), kClassic).found, Found::None);
}

void TestBootLogo::writeWithoutFolder()
{
    QTemporaryDir dir;
    const QString gone = dir.filePath(QStringLiteral("gone"));
    QString error;
    QVERIFY(!writeLogo(gone, encode(Logo{}), &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!removeLogo(gone, &error));
    QVERIFY(!error.isEmpty());
}

void TestBootLogo::pictureRefused()
{
    QTemporaryDir dir;
    const QString text = dir.filePath(QStringLiteral("notes.png"));
    QFile file(text);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a picture");
    file.close();
    QCOMPARE(readPicture(text).error,
             QStringLiteral("This file is not a picture the app can read"));
    QCOMPARE(readPicture(dir.filePath(QStringLiteral("none.png"))).error,
             QStringLiteral("This file is not a picture the app can read"));

    const QString wide = dir.filePath(QStringLiteral("wide.png"));
    QVERIFY(QImage(16385, 1, QImage::Format_Mono).save(wide));
    QCOMPARE(readPicture(wide).error, QStringLiteral("This picture is too large"));

    const QString big = dir.filePath(QStringLiteral("big.png"));
    QFile large(big);
    QVERIFY(large.open(QIODevice::WriteOnly));
    QVERIFY(large.resize(64LL * 1024 * 1024 + 1));
    large.close();
    QCOMPARE(readPicture(big).error, QStringLiteral("This picture is too large"));

    const QString good = dir.filePath(QStringLiteral("good.png"));
    QVERIFY(filled(40, 30, Qt::red).save(good));
    const Picture picture = readPicture(good);
    QVERIFY(picture.error.isEmpty());
    QCOMPARE(picture.image.size(), QSize(40, 30));
}

QTEST_GUILESS_MAIN(TestBootLogo)
#include "tst_bootlogo.moc"
