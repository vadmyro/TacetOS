// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The battery of docs/specs/health.md section 6: the tests "Estimate" of
// section 10, the medians and the reference, and each row of the verdict of
// section 6.3 at its limits.

#include "battery.h"
#include "constants.h"
#include "healthbuilder.h"
#include "reading.h"

#include <QTest>

using namespace tacetos::health;
using namespace testfiles;

Q_DECLARE_METATYPE(tacetos::health::BatteryVerdict)

namespace {

// A full charge, then 10 hours that took half the battery: 20 hours a charge.
ChargeFile fullCharge()
{
    ChargeFile charge;
    charge.clock = QStringLiteral("2026-11-26 09:00:00");
    charge.batteryLevel = 100;
    charge.batteryMv = 4150;
    return charge;
}

HealthFile halfDrained()
{
    HealthFile health;
    health.clock = QStringLiteral("2026-11-27 14:03:00");
    health.batteryLevel = 50;
    health.batteryMv = 3800;
    health.runtimeS = 36000;
    return health;
}

QList<double> repeated(double value, int count)
{
    return QList<double>(count, value);
}

} // namespace

class TestHealthBattery : public QObject
{
    Q_OBJECT

private slots:
    void limits()
    {
        QCOMPARE(tacetos::device::kFullLevel, 95);
        QCOMPARE(tacetos::device::kMinDrop, 30);
        QCOMPARE(tacetos::device::kMinRuntimeS, qint64(3600));
        QCOMPARE(tacetos::device::kMinEstimates, 6);
        QCOMPARE(tacetos::device::kWornRatio, 0.5);
    }

    void estimateValue()
    {
        QCOMPARE(estimateHours(fullCharge(), halfDrained()), std::optional<double>(20.0));

        // 18720 s for 60 points: 8.67 hours, kept as 8.7.
        HealthFile health = halfDrained();
        health.batteryLevel = 40;
        health.runtimeS = 18720;
        QCOMPARE(estimateHours(fullCharge(), health), std::optional<double>(8.7));
    }

    void estimateConditions_data()
    {
        QTest::addColumn<QString>("chargeLevel");
        QTest::addColumn<QString>("healthLevel");
        QTest::addColumn<qint64>("runtime");
        QTest::addColumn<QString>("chargeClock");
        QTest::addColumn<QString>("healthClock");
        QTest::addColumn<bool>("estimate");

        const QString start = QStringLiteral("2026-11-26 09:00:00");
        const QString later = QStringLiteral("2026-11-27 14:03:00");
        const QString unset;
        QTest::newRow("all hold") << "100" << "50" << qint64(36000) << start << later << true;
        // Condition 2.
        QTest::newRow("charge 95") << "95" << "50" << qint64(36000) << start << later << true;
        QTest::newRow("charge 94") << "94" << "50" << qint64(36000) << start << later << false;
        QTest::newRow("charge unknown") << "" << "50" << qint64(36000) << start << later << false;
        // Condition 3.
        QTest::newRow("drop 30") << "100" << "70" << qint64(36000) << start << later << true;
        QTest::newRow("drop 29") << "100" << "71" << qint64(36000) << start << later << false;
        QTest::newRow("level unknown") << "100" << "" << qint64(36000) << start << later << false;
        // Condition 4.
        QTest::newRow("runtime 1 h") << "100" << "50" << qint64(3600) << start << later << true;
        QTest::newRow("runtime short") << "100" << "50" << qint64(3599) << start << later << false;
        // Condition 5.
        QTest::newRow("same clock") << "100" << "50" << qint64(36000) << start << start << false;
        QTest::newRow("earlier clock") << "100" << "50" << qint64(36000) << later << start << false;
        QTest::newRow("charge clock unset")
            << "100" << "50" << qint64(36000) << unset << later << true;
        QTest::newRow("health clock unset")
            << "100" << "50" << qint64(36000) << start << unset << true;
    }

    void estimateConditions()
    {
        QFETCH(QString, chargeLevel);
        QFETCH(QString, healthLevel);
        QFETCH(qint64, runtime);
        QFETCH(QString, chargeClock);
        QFETCH(QString, healthClock);
        QFETCH(bool, estimate);

        ChargeFile charge = fullCharge();
        charge.batteryLevel =
            chargeLevel.isEmpty() ? std::nullopt : std::optional<int>(chargeLevel.toInt());
        charge.clock = chargeClock;
        HealthFile health = halfDrained();
        health.batteryLevel =
            healthLevel.isEmpty() ? std::nullopt : std::optional<int>(healthLevel.toInt());
        health.runtimeS = runtime;
        health.clock = healthClock;
        QCOMPARE(estimateHours(charge, health).has_value(), estimate);
    }

    // Condition 1, through the files: each must be usable.
    void estimateNeedsBothFiles()
    {
        const QDateTime at = QDateTime::currentDateTimeUtc();
        const QByteArray health = healthText(Health());
        const QByteArray charge = chargeText();
        QCOMPARE(makeReading(health, charge, at).estimateH, std::optional<double>(20.0));

        QByteArray noEnd = charge;
        noEnd.replace("end: ok\n", "");
        QVERIFY(!makeReading(health, noEnd, at).estimateH);
        QVERIFY(!makeReading(health, std::nullopt, at).estimateH);

        QByteArray badHealth = health;
        badHealth.replace("format: 1", "format: 2");
        QVERIFY(!makeReading(badHealth, charge, at).estimateH);
        QVERIFY(!makeReading(std::nullopt, charge, at).estimateH);
    }

    void medians()
    {
        QCOMPARE(median3(1, 2, 3), 2.0);
        QCOMPARE(median3(3, 1, 2), 2.0);
        QCOMPARE(median3(2, 3, 1), 2.0);
        QCOMPARE(median3(5, 5, 1), 5.0);

        QVERIFY(!currentEstimate({20, 21}));
        QVERIFY(!referenceEstimate({20, 21}));

        // One long idle charge (50) does not make the reference alone.
        const QList<double> estimates{10, 40, 30, 1, 2, 3, 50};
        QCOMPARE(currentEstimate(estimates), std::optional<double>(3));
        QCOMPARE(referenceEstimate(estimates), std::optional<double>(30));
    }

    void verdict_data()
    {
        QTest::addColumn<bool>("chargingError");
        QTest::addColumn<QList<double>>("estimates");
        QTest::addColumn<BatteryVerdict>("verdict");

        const QList<double> worn{20, 20, 20, 10, 10, 10};
        QTest::newRow("charging error") << true << repeated(20, 6) << BatteryVerdict::Watch;
        QTest::newRow("charging error, no estimate")
            << true << QList<double>() << BatteryVerdict::Watch;
        QTest::newRow("charging error, worn") << true << worn << BatteryVerdict::Watch;
        QTest::newRow("no estimate") << false << QList<double>() << BatteryVerdict::Unknown;
        QTest::newRow("5 estimates") << false << repeated(20, 5) << BatteryVerdict::Unknown;
        QTest::newRow("6 estimates") << false << repeated(20, 6) << BatteryVerdict::Good;
        QTest::newRow("half") << false << worn << BatteryVerdict::Worn;
        QTest::newRow("just over half")
            << false << QList<double>{20, 20, 20, 10.1, 10.1, 10.1} << BatteryVerdict::Good;
        // One short charge at the end does not move the median.
        QTest::newRow("one heavy day")
            << false << QList<double>{20, 20, 20, 20, 20, 2} << BatteryVerdict::Good;
        QTest::newRow("better than ever")
            << false << QList<double>{10, 10, 10, 20, 20, 20} << BatteryVerdict::Good;
    }

    void verdict()
    {
        QFETCH(bool, chargingError);
        QFETCH(QList<double>, estimates);
        QFETCH(BatteryVerdict, verdict);
        QCOMPARE(batteryVerdict(chargingError, estimates), verdict);
    }
};

QTEST_GUILESS_MAIN(TestHealthBattery)
#include "tst_health_battery.moc"
