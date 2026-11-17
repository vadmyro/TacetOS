// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md sections 7.6 and 15.4: the zip reader. The test
// makes its zips itself: stored entries, and deflate data made by qCompress
// with its header and its sum taken off.

#include "crc32.h"
#include "inflate.h"
#include "zipreader.h"

#include <QBuffer>
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::archive;

namespace {

const ZipLimits kLimits{100, qint64(64) << 20};

// One entry of a zip made by the test, and what it gets wrong on purpose.
struct Item
{
    QByteArray name;
    QByteArray data;
    // 0: stored, 8: deflate, anything else as it is.
    int method = 0;
    int level = 9;
    int flags = 0;
    int madeBy = 20;
    quint32 external = 0;
    // When set, the size the headers declare instead of the real one.
    qint64 declaredSize = -1;
    quint32 crcOffBy = 0;
    // When set, the name in the local header instead of `name`.
    QByteArray localName;
};

void put(QByteArray &out, quint64 value, int size)
{
    for (int i = 0; i < size; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

// Raw deflate data: qCompress gives 4 bytes of size, a 2-byte header, the
// data, and a 4-byte sum.
QByteArray deflated(const QByteArray &data, int level)
{
    // qCompress gives only the 4 bytes of size for no data. Deflate tools
    // write one last block with the fixed codes and only its end code.
    if (data.isEmpty())
        return QByteArray("\x03\x00", 2);
    const QByteArray packed = qCompress(data, level);
    return packed.mid(6, packed.size() - 10);
}

QByteArray makeZip(const QList<Item> &items, bool zip64 = false)
{
    QByteArray out;
    QByteArray directory;
    for (const Item &item : items) {
        const QByteArray packed = item.method == 8 ? deflated(item.data, item.level) : item.data;
        const quint32 crc = crc32(item.data) + item.crcOffBy;
        const quint64 size = item.declaredSize >= 0 ? quint64(item.declaredSize) : item.data.size();
        const quint64 offset = quint64(out.size());
        const QByteArray localName = item.localName.isEmpty() ? item.name : item.localName;
        QByteArray extra;
        if (zip64) {
            put(extra, 0x0001, 2);
            put(extra, 16, 2);
            put(extra, size, 8);
            put(extra, quint64(packed.size()), 8);
        }
        put(out, 0x04034b50, 4);
        put(out, zip64 ? 45 : 20, 2);
        put(out, item.flags, 2);
        put(out, item.method, 2);
        put(out, 0, 2);
        put(out, 0x21, 2);
        put(out, crc, 4);
        put(out, zip64 ? 0xFFFFFFFFu : quint64(packed.size()), 4);
        put(out, zip64 ? 0xFFFFFFFFu : size, 4);
        put(out, localName.size(), 2);
        put(out, extra.size(), 2);
        out += localName + extra + packed;

        QByteArray central;
        if (zip64) {
            put(central, 0x0001, 2);
            put(central, 24, 2);
            put(central, size, 8);
            put(central, quint64(packed.size()), 8);
            put(central, offset, 8);
        }
        put(directory, 0x02014b50, 4);
        put(directory, item.madeBy, 2);
        put(directory, zip64 ? 45 : 20, 2);
        put(directory, item.flags, 2);
        put(directory, item.method, 2);
        put(directory, 0, 2);
        put(directory, 0x21, 2);
        put(directory, crc, 4);
        put(directory, zip64 ? 0xFFFFFFFFu : quint64(packed.size()), 4);
        put(directory, zip64 ? 0xFFFFFFFFu : size, 4);
        put(directory, item.name.size(), 2);
        put(directory, central.size(), 2);
        put(directory, 0, 2);
        put(directory, 0, 2);
        put(directory, 0, 2);
        put(directory, item.external, 4);
        put(directory, zip64 ? 0xFFFFFFFFu : offset, 4);
        directory += item.name + central;
    }
    const quint64 directoryOffset = quint64(out.size());
    out += directory;
    if (zip64) {
        const quint64 recordOffset = quint64(out.size());
        put(out, 0x06064b50, 4);
        put(out, 44, 8);
        put(out, 45, 2);
        put(out, 45, 2);
        put(out, 0, 4);
        put(out, 0, 4);
        put(out, quint64(items.size()), 8);
        put(out, quint64(items.size()), 8);
        put(out, quint64(directory.size()), 8);
        put(out, directoryOffset, 8);
        put(out, 0x07064b50, 4);
        put(out, 0, 4);
        put(out, recordOffset, 8);
        put(out, 1, 4);
    }
    put(out, 0x06054b50, 4);
    put(out, 0, 2);
    put(out, 0, 2);
    put(out, zip64 ? 0xFFFF : items.size(), 2);
    put(out, zip64 ? 0xFFFF : items.size(), 2);
    put(out, zip64 ? 0xFFFFFFFFu : quint64(directory.size()), 4);
    put(out, zip64 ? 0xFFFFFFFFu : directoryOffset, 4);
    put(out, 0, 2);
    return out;
}

QByteArray text(int size)
{
    QByteArray out;
    int n = 0;
    while (out.size() < size)
        out += "line " + QByteArray::number(n++ % 97) + " of a text that repeats itself\n";
    return out.left(size);
}

QByteArray noise(int size, quint32 seed)
{
    QRandomGenerator random(seed);
    QByteArray out(size, '\0');
    for (char &c : out)
        c = char(random.bounded(256));
    return out;
}

} // namespace

class TstArchive : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    int m_count = 0;

    QString save(const QByteArray &zip)
    {
        const QString path = m_tmp.filePath(QStringLiteral("a%1.zip").arg(++m_count));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(zip) != zip.size())
            return QString();
        return path;
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // The check value of this CRC, from its public description.
    void crc()
    {
        QCOMPARE(crc32(QByteArray("123456789")), 0xCBF43926u);
        QCOMPARE(crc32(QByteArray()), 0u);
        Crc32 pieces;
        pieces.add(QByteArray("1234"));
        pieces.add(QByteArray("56789"));
        QCOMPARE(pieces.value(), 0xCBF43926u);
    }

    void unpack_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("method");
        QTest::addColumn<int>("level");
        QTest::newRow("stored") << text(5000) << 0 << 0;
        QTest::newRow("stored, empty") << QByteArray() << 0 << 0;
        QTest::newRow("deflate, empty") << QByteArray() << 8 << 9;
        QTest::newRow("deflate 0, stored blocks") << text(200000) << 8 << 0;
        QTest::newRow("deflate 1") << text(300000) << 8 << 1;
        QTest::newRow("deflate 9") << text(300000) << 8 << 9;
        QTest::newRow("deflate 9, short") << QByteArray("TacetOS") << 8 << 9;
        QTest::newRow("deflate 9, noise") << noise(100000, 7) << 8 << 9;
        QTest::newRow("deflate 9, far matches") << noise(40000, 3).repeated(4) << 8 << 9;
        QTest::newRow("deflate 6, one byte") << QByteArray(1 << 20, 'z') << 8 << 6;
    }

    void unpack()
    {
        QFETCH(QByteArray, data);
        QFETCH(int, method);
        QFETCH(int, level);
        Item folder;
        folder.name = ".tacetos/";
        Item item;
        item.name = ".tacetos/sub/file.bin";
        item.data = data;
        item.method = method;
        item.level = level;
        ZipReader zip(save(makeZip({folder, item})));
        QVERIFY2(zip.open(kLimits), qPrintable(zip.error()));
        QCOMPARE(zip.entries().size(), 2);
        QVERIFY(zip.entries().at(0).folder);
        const ZipEntry entry = zip.entries().at(1);
        QCOMPARE(entry.name, QStringLiteral(".tacetos/sub/file.bin"));
        QCOMPARE(entry.size, data.size());
        QCOMPARE(zip.unpackedSize(), data.size());
        const QByteArray got = zip.read(entry);
        QVERIFY2(zip.error().isEmpty(), qPrintable(zip.error()));
        QCOMPARE(got, data);
        QVERIFY(zip.read(zip.entries().at(0)).isEmpty());
        QVERIFY(zip.error().isEmpty());
    }

    // Archives over 4 GiB hold their sizes and offsets in Zip64 records.
    void zip64()
    {
        Item a;
        a.name = "a.txt";
        a.data = text(70000);
        a.method = 8;
        Item b;
        b.name = QStringLiteral("Été/b.txt").toUtf8();
        b.flags = 0x0800;
        b.data = "b";
        ZipReader zip(save(makeZip({a, b}, true)));
        QVERIFY2(zip.open(kLimits), qPrintable(zip.error()));
        QCOMPARE(zip.entries().size(), 2);
        QCOMPARE(zip.entries().at(1).name, QStringLiteral("Été/b.txt"));
        QCOMPARE(zip.read(zip.entries().at(0)), a.data);
        QCOMPARE(zip.read(zip.entries().at(1)), b.data);
    }

    // Unpacking goes by pieces, and stops between two of them.
    void stopAndWrite()
    {
        Item item;
        item.name = "big.bin";
        item.data = noise(3 << 20, 11);
        item.method = 8;
        ZipReader zip(save(makeZip({item})));
        QVERIFY(zip.open(kLimits));
        QBuffer out;
        out.open(QIODevice::WriteOnly);
        int pieces = 0;
        QVERIFY(!zip.extractTo(zip.entries().first(), out, [&pieces] { return ++pieces > 2; }));
        QCOMPARE(zip.error(), QStringLiteral("stopped"));
        QVERIFY(out.size() < item.data.size());

        QBuffer all;
        all.open(QIODevice::WriteOnly);
        QVERIFY2(zip.extractTo(zip.entries().first(), all), qPrintable(zip.error()));
        QCOMPARE(all.data(), item.data);
    }

    // Section 7.6: refused with the whole archive, when it is opened.
    void refusedAtOpen_data()
    {
        QTest::addColumn<QString>("problem");
        QTest::addColumn<QString>("error");
        QTest::newRow("encrypted") << "encrypted" << "an encrypted entry";
        QTest::newRow("method") << "method" << "an entry packed with method 12";
        QTest::newRow("dot dot") << "dotdot" << "a name with ..";
        QTest::newRow("slash") << "slash" << "a name that starts with /";
        QTest::newRow("drive") << "drive" << "a name that starts with a drive letter";
        QTest::newRow("backslash") << "backslash" << "a name with a backslash";
        QTest::newRow("case") << "case" << "two names that differ only by case";
        QTest::newRow("same name") << "same" << "two names that differ only by case";
        QTest::newRow("link") << "link" << "a link";
        QTest::newRow("too many") << "many" << "more entries than allowed";
        QTest::newRow("too large") << "large" << "more unpacked bytes than allowed";
        QTest::newRow("local header") << "local"
                                      << "the central directory does not match the "
                                         "local headers";
        QTest::newRow("not a zip") << "noise" << "no end record: not a zip archive, or a cut one";
        QTest::newRow("cut") << "cut" << "no end record: not a zip archive, or a cut one";
    }

    void refusedAtOpen()
    {
        QFETCH(QString, problem);
        QFETCH(QString, error);
        QList<Item> items(3);
        items[0].name = ".tacetos/a.txt";
        items[0].data = "a";
        items[1].name = ".tacetos/b.txt";
        items[1].data = text(3000);
        items[1].method = 8;
        items[2].name = ".tacetos/c.txt";
        items[2].data = "c";
        Item &bad = items[1];
        ZipLimits limits = kLimits;
        if (problem == QLatin1String("encrypted"))
            bad.flags = 0x0001;
        else if (problem == QLatin1String("method"))
            bad.method = 12;
        else if (problem == QLatin1String("dotdot"))
            bad.name = ".tacetos/../../b.txt";
        else if (problem == QLatin1String("slash"))
            bad.name = "/etc/b.txt";
        else if (problem == QLatin1String("drive"))
            bad.name = "C:/b.txt";
        else if (problem == QLatin1String("backslash"))
            bad.name = ".tacetos\\b.txt";
        else if (problem == QLatin1String("case"))
            bad.name = ".TacetOS/A.TXT";
        else if (problem == QLatin1String("same"))
            bad.name = ".tacetos/a.txt";
        else if (problem == QLatin1String("link")) {
            bad.madeBy = 3 << 8;
            bad.external = 0xA1FFu << 16;
        } else if (problem == QLatin1String("many"))
            limits.entries = 2;
        else if (problem == QLatin1String("large"))
            limits.unpackedBytes = 2000;
        else if (problem == QLatin1String("local"))
            bad.localName = ".tacetos/x.txt";
        QByteArray zip = makeZip(items);
        if (problem == QLatin1String("noise"))
            zip = noise(5000, 5);
        else if (problem == QLatin1String("cut"))
            zip.chop(30);
        ZipReader reader(save(zip));
        QVERIFY(!reader.open(limits));
        QCOMPARE(reader.error(), error);
        QVERIFY(reader.entries().isEmpty());
    }

    // Found while unpacking: the CRC-32, and data that does not match its size.
    void refusedAtUnpack_data()
    {
        QTest::addColumn<int>("method");
        QTest::addColumn<QString>("problem");
        QTest::addColumn<QString>("error");
        QTest::newRow("stored, CRC") << 0 << "crc" << "the CRC-32 of an entry does not match";
        QTest::newRow("deflate, CRC") << 8 << "crc" << "the CRC-32 of an entry does not match";
        QTest::newRow("deflate, larger")
            << 8 << "larger" << "the data unpacks to more than its declared size";
        QTest::newRow("deflate, smaller")
            << 8 << "smaller" << "the data unpacks to less than its declared size";
    }

    void refusedAtUnpack()
    {
        QFETCH(int, method);
        QFETCH(QString, problem);
        QFETCH(QString, error);
        Item item;
        item.name = "x.bin";
        item.data = text(10000);
        item.method = method;
        if (problem == QLatin1String("crc"))
            item.crcOffBy = 1;
        else if (problem == QLatin1String("larger"))
            item.declaredSize = 9000;
        else
            item.declaredSize = 11000;
        ZipReader zip(save(makeZip({item})));
        QVERIFY2(zip.open(kLimits), qPrintable(zip.error()));
        QVERIFY(zip.read(zip.entries().first()).isEmpty());
        QCOMPARE(zip.error(), error);
    }

    // Damaged deflate data gives an error, never a crash or a hang: every
    // byte of the data changed in turn, then random changes.
    void damagedDeflate()
    {
        const QByteArray data = text(20000) + noise(3000, 9);
        const QByteArray packed = deflated(data, 9);
        int errors = 0;
        QRandomGenerator random(42);
        for (int round = 0; round < 600; ++round) {
            QByteArray broken = packed;
            if (round < 300) {
                const int at = round * (packed.size() - 1) / 299;
                broken[at] = char(broken.at(at) ^ (1 << (round % 8)));
            } else {
                for (int k = 0; k < 4; ++k)
                    broken[random.bounded(int(broken.size()))] = char(random.bounded(256));
            }
            QBuffer in(&broken);
            in.open(QIODevice::ReadOnly);
            QByteArray out;
            QString why;
            const bool ok = inflate(
                in, broken.size(), data.size(),
                [&out](const char *d, qint64 n) {
                    out.append(d, qsizetype(n));
                    return true;
                },
                &why);
            if (!ok) {
                QVERIFY(!why.isEmpty());
                ++errors;
            }
            QVERIFY(out.size() <= data.size());
        }
        QVERIFY(errors > 0);

        // Cut short: the data ends in the middle.
        QByteArray cut = packed.left(packed.size() / 2);
        QBuffer in(&cut);
        in.open(QIODevice::ReadOnly);
        QString why;
        QVERIFY(!inflate(in, cut.size(), data.size(), {}, &why));
        QCOMPARE(why, QStringLiteral("the data ends in the middle"));
    }
};

QTEST_GUILESS_MAIN(TstArchive)
#include "tst_archive.moc"
