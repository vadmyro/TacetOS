// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The history of docs/specs/health.md section 7, and the tests "History" of
// section 10, in a temporary folder given as the data folder.

#include "constants.h"
#include "healthbuilder.h"
#include "history.h"
#include "reading.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::health;
using namespace testblocks;
using namespace testfiles;

namespace {

const QDateTime kReadAt(QDate(2026, 11, 27), QTime(13, 3, 12), QTimeZone::UTC);

HealthReading reading(const Health &h, const std::optional<QByteArray> &charge = std::nullopt,
                      const QDateTime &at = kReadAt)
{
    return makeReading(healthText(h), charge, at);
}

// A different health file for each `i`.
Health numbered(int i)
{
    Health h;
    h.runtimeS = 100 + i;
    return h;
}

QString keyOf(const QByteArray &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex().left(16));
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class TestHealthHistory : public QObject
{
    Q_OBJECT

private slots:
    void constants()
    {
        QCOMPARE(tacetos::device::kHistoryMax, 500);
        QCOMPARE(QString(tacetos::device::kHealthFolder), QStringLiteral("health"));
    }

    void key()
    {
        Health withDisk;
        withDisk.identify = identify("APPLE HDD", "5RX0ABCD", "1.0");
        const HealthReading disk = reading(withDisk);
        QVERIFY(disk.disk.identify.ok);
        QCOMPARE(historyKey(disk, QString()), keyOf("disk:5RX0ABCD"));
        // The disk serial comes first.
        QCOMPARE(historyKey(disk, QStringLiteral("000A27001234")), keyOf("disk:5RX0ABCD"));

        const HealthReading noDisk = reading(Health());
        QCOMPARE(historyKey(noDisk, QStringLiteral("000A27001234")), keyOf("usb:000A27001234"));
        QCOMPARE(historyKey(noDisk, QStringLiteral("000A27001234")).size(), 16);

        // Neither serial: no history.
        QVERIFY(historyKey(noDisk, QString()).isEmpty());
        Health noSerial;
        noSerial.identify = identify("APPLE HDD", "", "1.0");
        QVERIFY(historyKey(reading(noSerial), QString()).isEmpty());
    }

    void addedOnce()
    {
        History history;
        QVERIFY(addReading(&history, reading(Health())));
        // The same file at the next connection, or at a Refresh.
        QVERIFY(!addReading(&history, reading(Health(), std::nullopt, kReadAt.addSecs(60))));
        QCOMPARE(history.readings.size(), 1);

        QVERIFY(addReading(&history, reading(numbered(1))));
        QCOMPARE(history.readings.size(), 2);
        // Only the last reading counts: a file seen before comes back.
        QVERIFY(addReading(&history, reading(Health())));
        QCOMPARE(history.readings.size(), 3);
    }

    void unusableNotAdded()
    {
        History history;
        QByteArray bad = healthText(Health());
        bad.replace("end: ok\n", "");
        QVERIFY(!addReading(&history, makeReading(bad, chargeText(), kReadAt)));
        QVERIFY(!addReading(&history, makeReading(std::nullopt, chargeText(), kReadAt)));
        QVERIFY(history.readings.isEmpty());
    }

    void values()
    {
        Health h;
        h.charging = "error";
        h.smart = smart({{5, 100, 100, 3}, {9, 100, 100, 2140}, {197, 100, 100, 0}});
        History history;
        QVERIFY(addReading(&history, reading(h, chargeText())));
        const HistoryReading &r = history.readings.first();
        QCOMPARE(r.readAt, kReadAt);
        QCOMPARE(r.event, Event::Usb);
        QCOMPARE(r.clock, QStringLiteral("2026-11-27 14:03:00"));
        QCOMPARE(r.batteryLevel, std::optional<int>(50));
        QCOMPARE(r.batteryMv, 3800);
        QCOMPARE(r.runtimeS, qint64(36000));
        QCOMPARE(r.charging, Charging::Error);
        // Only the ids of the disk verdict, those the block has.
        QVERIFY(r.smart);
        QCOMPARE(r.smart->keys(), (QList<int>{5, 197}));
        QCOMPARE(r.smart->value(5), 3u);
        QCOMPARE(r.estimateH, std::optional<double>(20.0));
        QCOMPARE(r.healthSha256.size(), 64);
        QCOMPARE(r.chargeSha256.size(), 64);

        // No usable block, no charge file.
        QVERIFY(addReading(&history, reading(numbered(1))));
        QVERIFY(!history.readings.last().smart);
        QVERIFY(history.readings.last().chargeSha256.isEmpty());
        QVERIFY(!history.readings.last().estimateH);
    }

    void oneEstimatePerChargeFile()
    {
        History history;
        // Too short to give one: the charge file can still give one later.
        Health shortRun;
        shortRun.runtimeS = 600;
        QVERIFY(addReading(&history, reading(shortRun, chargeText())));
        QVERIFY(!history.readings.last().estimateH);

        QVERIFY(addReading(&history, reading(Health(), chargeText())));
        QCOMPARE(history.readings.last().estimateH, std::optional<double>(20.0));

        // A later health file, the same charge file: no second estimate.
        Health later;
        later.level = "40";
        later.runtimeS = 43200;
        QVERIFY(addReading(&history, reading(later, chargeText())));
        QVERIFY(!history.readings.last().estimateH);

        // The next charge gives one again.
        QVERIFY(
            addReading(&history, reading(numbered(1), chargeText("100", "2026-11-27 10:00:00"))));
        Health next;
        next.runtimeS = 36000;
        next.clock = "2026-11-28 10:00:00";
        QVERIFY(addReading(&history, reading(next, chargeText("100", "2026-11-27 10:00:00"))));
        QCOMPARE(history.readings.last().estimateH, std::optional<double>(20.0));
        QCOMPARE(history.estimates(), (QList<double>{20.0, 20.0}));
    }

    void oldestGoFirst()
    {
        History history;
        for (int i = 0; i < 501; ++i)
            QVERIFY(addReading(&history, reading(numbered(i))));
        QCOMPARE(history.readings.size(), 500);
        QCOMPARE(history.readings.first().runtimeS, qint64(101));
        QCOMPARE(history.readings.last().runtimeS, qint64(600));
    }

    void jsonRoundTrip()
    {
        Health h;
        h.smart = smart({{5, 100, 100, 3}, {10, 100, 100, 0}});
        History history;
        QVERIFY(addReading(&history, reading(h, chargeText())));
        Health unset;
        unset.clock = "unset";
        unset.level = "unknown";
        unset.runtimeS = 5;
        QVERIFY(addReading(&history, reading(unset)));

        const HistoryParse parse = historyFromJson(historyToJson(history));
        QVERIFY2(parse.ok, qPrintable(parse.error));
        QCOMPARE(parse.history.readings.size(), 2);
        const HistoryReading &a = parse.history.readings.at(0);
        const HistoryReading &b = history.readings.at(0);
        QCOMPARE(a.readAt, b.readAt);
        QCOMPARE(a.clock, b.clock);
        QCOMPARE(a.batteryLevel, b.batteryLevel);
        QCOMPARE(a.runtimeS, b.runtimeS);
        QCOMPARE(a.charging, b.charging);
        QCOMPARE(a.smart, b.smart);
        QCOMPARE(a.estimateH, b.estimateH);
        QCOMPARE(a.healthSha256, b.healthSha256);
        QCOMPARE(a.chargeSha256, b.chargeSha256);
        const HistoryReading &c = parse.history.readings.at(1);
        QVERIFY(c.clock.isEmpty());
        QVERIFY(!c.batteryLevel);
        QVERIFY(!c.smart);
        QVERIFY(!c.estimateH);
        QVERIFY(c.chargeSha256.isEmpty());

        const QByteArray json = historyToJson(history);
        QVERIFY(json.contains("\"read_at\": \"2026-11-27T13:03:12Z\""));
        QVERIFY(json.contains("\"format\": 1"));
    }

    void notAHistory_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("error");
        QTest::newRow("not JSON") << QByteArray("{nope") << "not a JSON object";
        QTest::newRow("array") << QByteArray("[]") << "not a JSON object";
        QTest::newRow("format 2") << QByteArray(R"({"format": 2, "readings": []})")
                                  << "format 2 not supported";
        QTest::newRow("no format")
            << QByteArray(R"({"readings": []})") << "format missing not supported";
        QTest::newRow("no readings") << QByteArray(R"({"format": 1})") << "no readings";
        QTest::newRow("bad reading")
            << QByteArray(R"({"format": 1, "readings": [{"event": "usb"}]})") << "bad reading 1";
    }

    void notAHistory()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, error);
        const HistoryParse parse = historyFromJson(data);
        QVERIFY(!parse.ok);
        QCOMPARE(parse.error, error);
    }

    void recordAndLoad()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString key = QStringLiteral("0123456789abcdef");
        QStringList log;

        History history = recordReading(data.path(), key, reading(Health(), chargeText()), &log);
        QCOMPARE(history.readings.size(), 1);
        const QString path = data.filePath(QStringLiteral("health/0123456789abcdef.json"));
        QCOMPARE(historyPath(data.path(), key), path);
        QVERIFY(QFile::exists(path));

        // A Refresh of the same file writes nothing new.
        const QByteArray before = readAll(path);
        history = recordReading(data.path(), key,
                                reading(Health(), chargeText(), kReadAt.addSecs(30)), &log);
        QCOMPARE(history.readings.size(), 1);
        QCOMPARE(readAll(path), before);

        history = recordReading(data.path(), key, reading(numbered(1)), &log);
        QCOMPARE(history.readings.size(), 2);
        const std::optional<History> loaded = loadHistory(data.path(), key, &log);
        QVERIFY(loaded);
        QCOMPARE(loaded->readings.size(), 2);
        QCOMPARE(loaded->estimates(), (QList<double>{20.0}));
        QVERIFY(log.isEmpty());
        // Only the history file in the folder: QSaveFile leaves nothing.
        QCOMPARE(QDir(data.filePath(QStringLiteral("health"))).entryList(QDir::Files),
                 QStringList{QStringLiteral("0123456789abcdef.json")});
    }

    void noFileYet()
    {
        QTemporaryDir data;
        QStringList log;
        const std::optional<History> loaded =
            loadHistory(data.path(), QStringLiteral("0123456789abcdef"), &log);
        QVERIFY(loaded);
        QVERIFY(loaded->readings.isEmpty());
        QVERIFY(log.isEmpty());
    }

    void noKeyNoFile()
    {
        QTemporaryDir data;
        QStringList log;
        const History history = recordReading(data.path(), QString(), reading(Health()), &log);
        QCOMPARE(history.readings.size(), 1);
        QVERIFY(QDir(data.path()).isEmpty());
    }

    void badFileSetAside()
    {
        QTemporaryDir data;
        const QString key = QStringLiteral("0123456789abcdef");
        const QString path = historyPath(data.path(), key);
        QVERIFY(writeFile(path + QStringLiteral(".bad"), "older"));
        QVERIFY(writeFile(path, R"({"format": 2, "readings": []})"));

        QStringList log;
        const History history = recordReading(data.path(), key, reading(Health()), &log);
        QCOMPARE(history.readings.size(), 1);
        QCOMPARE(log, QStringList{QStringLiteral("health/0123456789abcdef.json: format 2 not "
                                                 "supported, set aside as "
                                                 "0123456789abcdef.json.bad")});
        // The .bad replaces the older one, and a new history starts.
        QCOMPARE(readAll(path + QStringLiteral(".bad")),
                 QByteArray(R"({"format": 2, "readings": []})"));
        const std::optional<History> loaded = loadHistory(data.path(), key, &log);
        QVERIFY(loaded);
        QCOMPARE(loaded->readings.size(), 1);
        QCOMPARE(log.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestHealthHistory)
#include "tst_health_history.moc"
