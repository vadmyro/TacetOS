// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The two files of docs/specs/health.md sections 2 and 3, and the tests
// "Parsing" of section 10.

#include "constants.h"
#include "files.h"
#include "reading.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::health;
using tacetos::device::kMarkerMaxBytes;

namespace {

QByteArray fixture(const char *name)
{
    QFile file(QDir(QStringLiteral(TACETOS_HEALTH_FIXTURES)).filePath(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

// A short usable health file, one key per line.
QList<QByteArray> healthLines()
{
    return {"format: 1",
            "event: usb",
            "clock: 2026-11-27 14:03:00",
            "battery_level: 74",
            "battery_mv: 3912",
            "charging: no",
            "runtime_s: 18720",
            "top_runtime_s: 114000",
            "disk_kind: hdd",
            "smart: unsupported",
            "end: ok"};
}

QByteArray joined(const QList<QByteArray> &lines, const QByteArray &end = "\n")
{
    QByteArray data;
    for (const QByteArray &line : lines)
        data += line + end;
    return data;
}

QByteArray without(const QByteArray &key)
{
    QList<QByteArray> lines = healthLines();
    lines.removeIf([&](const QByteArray &line) { return line.startsWith(key + ':'); });
    return joined(lines);
}

QByteArray replaced(const QByteArray &key, const QByteArray &value)
{
    QList<QByteArray> lines = healthLines();
    for (QByteArray &line : lines) {
        if (line.startsWith(key + ':'))
            line = key + ": " + value;
    }
    return joined(lines);
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class TestHealthParsing : public QObject
{
    Q_OBJECT

private slots:
    void fixtureFile()
    {
        const HealthFileReading r = parseHealthFile(fixture("health-good.txt"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.file.event, Event::Usb);
        QCOMPARE(r.file.clock, QStringLiteral("2026-11-27 14:03:00"));
        QCOMPARE(r.file.batteryLevel, std::optional<int>(74));
        QCOMPARE(r.file.batteryMv, 3912);
        QCOMPARE(r.file.charging, Charging::No);
        QCOMPARE(r.file.runtimeS, qint64(18720));
        QCOMPARE(r.file.topRuntimeS, qint64(114000));
        QCOMPARE(r.file.diskKind, DiskKind::Hdd);
        QCOMPARE(r.file.smart, SmartState::Ok);
        QCOMPARE(r.file.identifyData.size(), 1024);
        QCOMPARE(r.file.smartData.size(), 1024);

        const HealthFileReading older = parseHealthFile(fixture("health-unsupported.txt"));
        QVERIFY(older.ok);
        QCOMPARE(older.file.event, Event::Shutdown);
        QCOMPARE(older.file.smart, SmartState::Unsupported);
        QVERIFY(older.file.smartData.isEmpty());
    }

    void chargeFixtures()
    {
        const ChargeFileReading full = parseChargeFile(fixture("charge-100.txt"));
        QVERIFY2(full.ok, qPrintable(full.error));
        QCOMPARE(full.file.batteryLevel, std::optional<int>(100));
        QCOMPARE(full.file.batteryMv, 4105);
        QCOMPARE(full.file.clock, QStringLiteral("2026-11-26 09:12:40"));
        const ChargeFileReading partial = parseChargeFile(fixture("charge-80.txt"));
        QVERIFY(partial.ok);
        QCOMPARE(partial.file.batteryLevel, std::optional<int>(80));
    }

    void missingKey_data()
    {
        QTest::addColumn<QByteArray>("key");
        QTest::addColumn<QString>("error");
        const char *always[] = {"event",         "clock",     "battery_level",
                                "battery_mv",    "charging",  "runtime_s",
                                "top_runtime_s", "disk_kind", "smart"};
        for (const char *key : always)
            QTest::newRow(key) << QByteArray(key)
                               << QStringLiteral("health.txt: no %1 line").arg(QLatin1String(key));
        QTest::newRow("format") << QByteArray("format")
                                << QStringLiteral("health.txt: no format line");
        QTest::newRow("end") << QByteArray("end") << QStringLiteral("health.txt: no end line");
    }

    void missingKey()
    {
        QFETCH(QByteArray, key);
        QFETCH(QString, error);
        const HealthFileReading r = parseHealthFile(without(key));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, error);
    }

    void chargeMissingKey_data()
    {
        QTest::addColumn<QByteArray>("key");
        for (const char *key : {"format", "clock", "battery_level", "battery_mv", "end"})
            QTest::newRow(key) << QByteArray(key);
    }

    void chargeMissingKey()
    {
        QFETCH(QByteArray, key);
        QList<QByteArray> lines = fixture("charge-100.txt").split('\n');
        lines.removeIf([&](const QByteArray &line) { return line.startsWith(key + ':'); });
        const ChargeFileReading r = parseChargeFile(lines.join('\n'));
        QVERIFY(!r.ok);
        QVERIFY(r.error.startsWith(QStringLiteral("charge.txt: ")));
    }

    void cutFile()
    {
        // A file cut while it was written has no end line.
        const QByteArray whole = fixture("health-good.txt");
        const HealthFileReading r = parseHealthFile(whole.left(whole.size() / 2));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("health.txt: no end line"));
        QCOMPARE(parseHealthFile(QByteArray()).error, QStringLiteral("health.txt: empty"));
        QCOMPARE(parseHealthFile(replaced("end", "no")).error,
                 QStringLiteral("health.txt: no end line"));
    }

    void otherFormat()
    {
        const HealthFileReading r = parseHealthFile(replaced("format", "2"));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("health.txt: format 2 not supported"));
        QCOMPARE(parseChargeFile(QByteArray("format: 2\nend: ok\n")).error,
                 QStringLiteral("charge.txt: format 2 not supported"));
        QCOMPARE(parseHealthFile(replaced("format", "one")).error,
                 QStringLiteral("health.txt: bad format 'one'"));
        // The format line comes first.
        QList<QByteArray> lines = healthLines();
        lines.move(0, 1);
        QCOMPARE(parseHealthFile(joined(lines)).error,
                 QStringLiteral("health.txt: no format line"));
    }

    void unknownKey()
    {
        QList<QByteArray> lines = healthLines();
        lines.insert(3, "fan_speed: 12");
        lines.insert(5, "later_key: any value: with a colon");
        const HealthFileReading r = parseHealthFile(joined(lines));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.file.batteryLevel, std::optional<int>(74));
    }

    void wrongForm_data()
    {
        QTest::addColumn<QByteArray>("key");
        QTest::addColumn<QByteArray>("value");
        QTest::newRow("event") << QByteArray("event") << QByteArray("plug");
        QTest::newRow("event case") << QByteArray("event") << QByteArray("USB");
        QTest::newRow("clock words") << QByteArray("clock") << QByteArray("yesterday");
        QTest::newRow("clock month") << QByteArray("clock") << QByteArray("2026-13-01 10:00:00");
        QTest::newRow("clock hour") << QByteArray("clock") << QByteArray("2026-11-27 24:00:00");
        QTest::newRow("clock form") << QByteArray("clock") << QByteArray("2026-11-27T14:03:00");
        QTest::newRow("level high") << QByteArray("battery_level") << QByteArray("101");
        QTest::newRow("level negative") << QByteArray("battery_level") << QByteArray("-1");
        QTest::newRow("level empty") << QByteArray("battery_level") << QByteArray("");
        QTest::newRow("mv point") << QByteArray("battery_mv") << QByteArray("3.91");
        QTest::newRow("mv sign") << QByteArray("battery_mv") << QByteArray("+3912");
        QTest::newRow("mv huge") << QByteArray("battery_mv") << QByteArray("99999999999");
        QTest::newRow("charging") << QByteArray("charging") << QByteArray("maybe");
        QTest::newRow("runtime negative") << QByteArray("runtime_s") << QByteArray("-5");
        QTest::newRow("top runtime") << QByteArray("top_runtime_s") << QByteArray("1h");
        QTest::newRow("disk kind") << QByteArray("disk_kind") << QByteArray("ssd");
        QTest::newRow("smart") << QByteArray("smart") << QByteArray("fine");
        QTest::newRow("smart error alone") << QByteArray("smart") << QByteArray("error");
        QTest::newRow("smart error word") << QByteArray("smart") << QByteArray("error x");
    }

    void wrongForm()
    {
        QFETCH(QByteArray, key);
        QFETCH(QByteArray, value);
        const HealthFileReading r = parseHealthFile(replaced(key, value));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("health.txt: bad %1 '%2'")
                              .arg(QString::fromLatin1(key), QString::fromLatin1(value)));
    }

    void otherValues()
    {
        HealthFileReading r = parseHealthFile(replaced("clock", "unset"));
        QVERIFY(r.ok);
        QVERIFY(r.file.clock.isEmpty());
        r = parseHealthFile(replaced("battery_level", "unknown"));
        QVERIFY(r.ok);
        QVERIFY(!r.file.batteryLevel);
        r = parseHealthFile(replaced("charging", "error"));
        QVERIFY(r.ok);
        QCOMPARE(r.file.charging, Charging::Error);
        r = parseHealthFile(replaced("smart", "error -5"));
        QVERIFY(r.ok);
        QCOMPARE(r.file.smart, SmartState::Error);
        QCOMPARE(r.file.smartError, -5);
        r = parseHealthFile(replaced("disk_kind", "flash"));
        QVERIFY(r.ok);
        QCOMPARE(r.file.diskKind, DiskKind::Flash);
        r = parseHealthFile(replaced("event", "shutdown"));
        QVERIFY(r.ok);
        QCOMPARE(r.file.event, Event::Shutdown);
    }

    void smartDataOnlyWithOk()
    {
        QList<QByteArray> lines = healthLines();
        lines.insert(9, "smart_data: " + QByteArray(1024, '0'));
        const HealthFileReading r = parseHealthFile(joined(lines));
        QVERIFY(r.ok);
        QVERIFY(r.file.smartData.isEmpty());
    }

    void notKeyValueLine()
    {
        QList<QByteArray> lines = healthLines();
        lines.insert(4, "");
        QCOMPARE(parseHealthFile(joined(lines)).error,
                 QStringLiteral("health.txt: line 5 is not a key: value line"));
        lines = healthLines();
        lines.insert(4, "Battery_level: 10");
        QVERIFY(!parseHealthFile(joined(lines)).ok);
        lines = healthLines();
        lines.insert(4, "battery_level:10");
        QVERIFY(!parseHealthFile(joined(lines)).ok);
    }

    void overLimit()
    {
        QList<QByteArray> lines = healthLines();
        lines.insert(1, "padding: " + QByteArray(kMarkerMaxBytes, 'x'));
        const HealthFileReading r = parseHealthFile(joined(lines));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("health.txt: larger than 65536 bytes"));

        // Exactly the limit is still read.
        lines = healthLines();
        const QByteArray base = joined(lines);
        const qsizetype room = kMarkerMaxBytes - base.size() - qsizetype(sizeof("padding: \n")) + 1;
        lines.insert(1, "padding: " + QByteArray(room, 'x'));
        QCOMPARE(joined(lines).size(), kMarkerMaxBytes);
        QVERIFY(parseHealthFile(joined(lines)).ok);
    }

    void crlfLines()
    {
        const HealthFileReading lf = parseHealthFile(joined(healthLines()));
        const HealthFileReading crlf = parseHealthFile(joined(healthLines(), "\r\n"));
        QVERIFY(crlf.ok);
        QCOMPARE(crlf.file.clock, lf.file.clock);
        QCOMPARE(crlf.file.smart, lf.file.smart);
        // No line end after the last line.
        QByteArray noFinal = joined(healthLines());
        noFinal.chop(1);
        QVERIFY(parseHealthFile(noFinal).ok);
        QVERIFY(parseChargeFile(fixture("charge-80.txt").replace("\n", "\r\n")).ok);
    }

    void readFromModelFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray health = fixture("health-good.txt");
        const QByteArray charge = fixture("charge-100.txt");
        // The model's folder, with names in another case, as FAT32 allows.
        QVERIFY(writeFile(root.filePath(QStringLiteral(".TacetOS/HEALTH.TXT")), health));
        QVERIFY(writeFile(root.filePath(QStringLiteral(".TacetOS/Charge.txt")), charge));
        // A default folder left by an older install comes second.
        QVERIFY(writeFile(root.filePath(QString(tacetos::device::kFirmwareFolder) +
                                        QStringLiteral("/health.txt")),
                          fixture("health-watch.txt")));

        const HealthFiles files = readHealthFiles(root.path(), QStringLiteral(".tacetos"));
        QVERIFY(!files.volumeGone);
        QCOMPARE(files.folder, QStringLiteral(".tacetos"));
        QVERIFY(files.health);
        QCOMPARE(*files.health, health);
        QVERIFY(files.charge);
        QCOMPARE(*files.charge, charge);
        QVERIFY(files.log.isEmpty());
    }

    void readFromDefaultFolder()
    {
        QTemporaryDir root;
        const QString folder = QString(tacetos::device::kFirmwareFolder);
        QVERIFY(writeFile(root.filePath(folder + QStringLiteral("/health.txt")),
                          fixture("health-unsupported.txt")));
        const HealthFiles files = readHealthFiles(root.path(), QStringLiteral(".tacetos"));
        QCOMPARE(files.folder, folder);
        QVERIFY(files.health);
        QVERIFY(!files.charge);
        QVERIFY(files.log.isEmpty());

        const HealthFiles none = readHealthFiles(QTemporaryDir().path(), folder);
        QVERIFY(none.folder.isEmpty());
        QVERIFY(!none.health);
        QVERIFY(!none.charge);
    }

    void readTooLarge()
    {
        QTemporaryDir root;
        const QString folder = QString(tacetos::device::kFirmwareFolder);
        QVERIFY(writeFile(root.filePath(folder + QStringLiteral("/health.txt")),
                          QByteArray(kMarkerMaxBytes + 1, 'x')));
        QDir(root.path()).mkpath(folder + QStringLiteral("/charge.txt"));
        const HealthFiles files = readHealthFiles(root.path(), QString());
        QVERIFY(!files.health);
        QVERIFY(!files.charge);
        QCOMPARE(files.log, QStringList({QStringLiteral("health.txt: larger than 65536 bytes"),
                                         QStringLiteral("charge.txt: not a file")}));
    }

    void readGoneVolume()
    {
        const HealthFiles files =
            readHealthFiles(QStringLiteral("/no/such/volume/at/all"), QStringLiteral(".tacetos"));
        QVERIFY(files.volumeGone);
        QVERIFY(!files.health);
    }

    void reading()
    {
        const QDateTime at(QDate(2026, 11, 27), QTime(13, 3, 12), QTimeZone::UTC);
        const HealthReading r =
            makeReading(fixture("health-good.txt"), fixture("charge-80.txt"), at);
        QVERIFY(r.ok);
        QVERIFY(r.chargeOk);
        QCOMPARE(r.readAt, at);
        QCOMPARE(r.charge.batteryLevel, std::optional<int>(80));
        QCOMPARE(r.disk.identify.serial, QStringLiteral("TST0000001"));
        QCOMPARE(r.healthBytes, fixture("health-good.txt"));
        QVERIFY(r.log.isEmpty());

        const HealthReading bad = makeReading(replaced("format", "2"), std::nullopt, at);
        QVERIFY(!bad.ok);
        QVERIFY(!bad.chargeOk);
        QCOMPARE(bad.log, QStringList{QStringLiteral("health.txt: format 2 not supported")});

        const HealthReading nothing = makeReading(std::nullopt, std::nullopt, at);
        QVERIFY(!nothing.ok);
        QVERIFY(nothing.log.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHealthParsing)
#include "tst_health_parsing.moc"
