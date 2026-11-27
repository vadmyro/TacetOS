// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The identify and SMART blocks of docs/specs/health.md section 5.1, and the
// tests "Blocks" of section 10.

#include "blockbuilder.h"
#include "blocks.h"
#include "disk.h"

#include <QTest>

using namespace tacetos::health;
using namespace testblocks;

namespace {

// A usable health file around a SMART block.
HealthFile withSmart(const QString &hex)
{
    HealthFile file;
    file.smart = SmartState::Ok;
    file.smartData = hex;
    return file;
}

} // namespace

class TestHealthBlocks : public QObject
{
    Q_OBJECT

private slots:
    void wrongLength_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("error");
        const QString good = hex(smart({{5}}));
        QTest::newRow("short") << good.left(1022) << QStringLiteral("1022 digits instead of 1024");
        QTest::newRow("long") << good + QStringLiteral("00")
                              << QStringLiteral("1026 digits instead of 1024");
        QTest::newRow("empty") << QString() << QStringLiteral("0 digits instead of 1024");
        QTest::newRow("not hex") << QString(good).replace(100, 1, QLatin1Char('g'))
                                 << QStringLiteral("a character that is not a hexadecimal digit");
        QTest::newRow("space") << QString(good).replace(0, 1, QLatin1Char(' '))
                               << QStringLiteral("a character that is not a hexadecimal digit");
    }

    void wrongLength()
    {
        QFETCH(QString, text);
        QFETCH(QString, error);
        const SmartBlock s = decodeSmart(text);
        QVERIFY(!s.ok);
        QCOMPARE(s.error, error);
        const IdentifyBlock i = decodeIdentify(text);
        QVERIFY(!i.ok);
        QCOMPARE(i.error, error);
    }

    void upperCaseDigits()
    {
        QVERIFY(decodeSmart(hex(smart({{5, 100, 100, 0xab}})).toUpper()).ok);
        QCOMPARE(blockBytes(QString(1024, QLatin1Char('F'))), QByteArray(512, char(0xff)));
    }

    void identifyStrings()
    {
        const IdentifyBlock block =
            decodeIdentify(hex(identify("TEST HDD 80GB", "TST0000001", "TF01")));
        QVERIFY2(block.ok, qPrintable(block.error));
        QCOMPARE(block.model, QStringLiteral("TEST HDD 80GB"));
        QCOMPARE(block.serial, QStringLiteral("TST0000001"));
        QCOMPARE(block.revision, QStringLiteral("TF01"));
        QVERIFY(block.checksummed);
    }

    void identifyPadding()
    {
        // Spaces in front, NUL characters behind, spaces inside kept.
        QByteArray block(512, '\0');
        putString(block, 10, 10, "      AB 12", '\0');
        putString(block, 23, 4, QByteArray("\0\0R1  ", 6), ' ');
        putString(block, 27, 20, "  MODEL  X", '\0');
        const IdentifyBlock i = decodeIdentify(hex(block));
        QVERIFY(i.ok);
        QCOMPARE(i.serial, QStringLiteral("AB 12"));
        QCOMPARE(i.revision, QStringLiteral("R1"));
        QCOMPARE(i.model, QStringLiteral("MODEL  X"));
        // Every word blank.
        const IdentifyBlock empty = decodeIdentify(hex(QByteArray(512, '\0')));
        QVERIFY(empty.ok);
        QVERIFY(empty.serial.isEmpty());
    }

    void identifyChecksum()
    {
        const IdentifyBlock right = decodeIdentify(hex(identify("M", "S", "R", Checksum::Right)));
        QVERIFY(right.ok);
        QVERIFY(right.checksummed);

        const IdentifyBlock wrong = decodeIdentify(hex(identify("M", "S", "R", Checksum::Wrong)));
        QVERIFY(!wrong.ok);
        QCOMPARE(wrong.error, QStringLiteral("the checksum is wrong"));

        // No 0xA5: nothing to test, even when the bytes do not add up to 0.
        QByteArray absent = identify("M", "S", "R", Checksum::Absent);
        absent[511] = char(0x17);
        QVERIFY(byteSum(absent) != 0);
        const IdentifyBlock none = decodeIdentify(hex(absent));
        QVERIFY(none.ok);
        QVERIFY(!none.checksummed);
        QCOMPARE(none.model, QStringLiteral("M"));
    }

    void smartEntries()
    {
        // Empty slots between the entries are skipped.
        const SmartBlock s = decodeSmart(
            hex(smart({{5, 100, 99, 3}, {0}, {0}, {197, 98, 97, 1}, {0}, {9, 90, 90, 1200}})));
        QVERIFY2(s.ok, qPrintable(s.error));
        QVERIFY(s.checksumOk);
        QCOMPARE(s.revision, 16);
        QCOMPARE(s.attributes.size(), 3);
        QCOMPARE(s.attributes.at(0).id, 5);
        QCOMPARE(s.attributes.at(0).current, 100);
        QCOMPARE(s.attributes.at(0).worst, 99);
        QCOMPARE(s.attributes.at(0).flags, quint16(0x32));
        QCOMPARE(s.attributes.at(1).id, 197);
        QCOMPARE(s.attributes.at(2).id, 9);
        QCOMPARE(s.raw(9), std::optional<quint32>(1200));
        QCOMPARE(s.raw(198), std::nullopt);

        // The last of the 30 slots is read.
        QList<Entry> thirty(29);
        thirty.append({12, 100, 100, 77});
        QCOMPARE(decodeSmart(hex(smart(thirty))).raw(12), std::optional<quint32>(77));
    }

    void noAttribute()
    {
        const SmartBlock s = decodeSmart(hex(smart({{0}, {0}})));
        QVERIFY(!s.ok);
        QCOMPARE(s.error, QStringLiteral("no attribute"));
    }

    void rawOver32Bits()
    {
        const SmartBlock s = decodeSmart(hex(smart({{5, 100, 100, 0x1234'0000'0007ull}})));
        QVERIFY(s.ok);
        QCOMPARE(s.attribute(5)->raw, 0x1234'0000'0007ull);
        QCOMPARE(s.raw(5), std::optional<quint32>(7));
        QCOMPARE(decodeSmart(hex(smart({{9, 100, 100, 0xffff'ffff'ffffull}}))).raw(9),
                 std::optional<quint32>(0xffffffffu));
    }

    void temperature()
    {
        // The low byte only: the other bytes hold the lowest and highest seen.
        QCOMPARE(decodeSmart(hex(smart({{194, 100, 100, 0x0000'2d12'001full}}))).temperature(),
                 std::optional<int>(31));
        QCOMPARE(decodeSmart(hex(smart({{194, 100, 100, 1}}))).temperature(),
                 std::optional<int>(1));
        QCOMPARE(decodeSmart(hex(smart({{194, 100, 100, 99}}))).temperature(),
                 std::optional<int>(99));
        QCOMPARE(decodeSmart(hex(smart({{194, 100, 100, 0x2d00}}))).temperature(), std::nullopt);
        QCOMPARE(decodeSmart(hex(smart({{194, 100, 100, 100}}))).temperature(), std::nullopt);
        QCOMPARE(decodeSmart(hex(smart({{5}}))).temperature(), std::nullopt);
    }

    void wrongSmartChecksum()
    {
        const QString text = hex(smart({{5, 100, 100, 2}}, false));
        const SmartBlock s = decodeSmart(text);
        QVERIFY(s.ok);
        QVERIFY(!s.checksumOk);

        // Used, with one log line.
        QStringList log;
        const DiskReading disk = readDisk(withSmart(text), &log);
        QVERIFY(disk.smart.ok);
        QCOMPARE(disk.value(kReallocatedId), std::optional<quint32>(2));
        QCOMPARE(log, QStringList{QStringLiteral(
                          "health.txt: the SMART checksum is wrong, the block is used")});
    }

    void unusableBlocksLogged()
    {
        HealthFile file = withSmart(QStringLiteral("00"));
        file.identifyData = hex(identify("M", "S", "R", Checksum::Wrong));
        QStringList log;
        const DiskReading disk = readDisk(file, &log);
        QVERIFY(!disk.identify.ok);
        QVERIFY(!disk.smart.ok);
        QCOMPARE(disk.value(kReallocatedId), std::nullopt);
        QCOMPARE(
            log,
            QStringList(
                {QStringLiteral("health.txt: identify block ignored: the checksum is wrong"),
                 QStringLiteral("health.txt: SMART block ignored: 2 digits instead of 1024")}));

        // `smart: ok` with no block.
        log.clear();
        QVERIFY(!readDisk(withSmart(QString()), &log).smart.ok);
        QCOMPARE(log, QStringList{
                          QStringLiteral("health.txt: SMART block ignored: no smart_data line")});

        // No identify line and `smart: unsupported`: nothing to log.
        log.clear();
        readDisk(HealthFile(), &log);
        QVERIFY(log.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHealthBlocks)
#include "tst_health_blocks.moc"
