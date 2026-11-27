// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The disk verdict of docs/specs/health.md section 5.3, at its limits, and on
// the fixtures of section 10.

#include "blockbuilder.h"
#include "constants.h"
#include "disk.h"
#include "reading.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace tacetos::health;
using namespace testblocks;

Q_DECLARE_METATYPE(tacetos::health::DiskVerdict)

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QDir(QStringLiteral(TACETOS_HEALTH_FIXTURES)).filePath(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return file.readAll();
}

SmartBlock block(const QList<Entry> &entries)
{
    return decodeSmart(hex(smart(entries)));
}

} // namespace

class TestHealthDisk : public QObject
{
    Q_OBJECT

private slots:
    void limits()
    {
        QCOMPARE(tacetos::device::kReallocatedFailing, 100u);
        QCOMPARE(tacetos::device::kPendingFailing, 10u);
        QCOMPARE(tacetos::device::kUncorrectableFailing, 10u);
    }

    void verdict_data()
    {
        QTest::addColumn<int>("id");
        QTest::addColumn<quint64>("raw");
        QTest::addColumn<DiskVerdict>("verdict");
        QTest::newRow("clean") << kPowerOnHoursId << quint64(2140) << DiskVerdict::Good;
        QTest::newRow("reallocated 1") << kReallocatedId << quint64(1) << DiskVerdict::Watch;
        QTest::newRow("reallocated 99") << kReallocatedId << quint64(99) << DiskVerdict::Watch;
        QTest::newRow("reallocated 100") << kReallocatedId << quint64(100) << DiskVerdict::Failing;
        QTest::newRow("pending 1") << kPendingId << quint64(1) << DiskVerdict::Watch;
        QTest::newRow("pending 9") << kPendingId << quint64(9) << DiskVerdict::Watch;
        QTest::newRow("pending 10") << kPendingId << quint64(10) << DiskVerdict::Failing;
        QTest::newRow("uncorrectable 9") << kUncorrectableId << quint64(9) << DiskVerdict::Watch;
        QTest::newRow("uncorrectable 10")
            << kUncorrectableId << quint64(10) << DiskVerdict::Failing;
        QTest::newRow("spin-up retries") << kSpinRetriesId << quint64(1) << DiskVerdict::Watch;
        // Only the low 32 bits count.
        QTest::newRow("high bits only")
            << kReallocatedId << quint64(0x1'0000'0000ull) << DiskVerdict::Good;
        // The other attributes never change the verdict.
        QTest::newRow("hot") << kTemperatureId << quint64(70) << DiskVerdict::Good;
        QTest::newRow("drops") << kDropEventsId << quint64(500) << DiskVerdict::Good;
        QTest::newRow("head loads") << kHeadLoadsId << quint64(900000) << DiskVerdict::Good;
    }

    void verdict()
    {
        QFETCH(int, id);
        QFETCH(quint64, raw);
        QFETCH(DiskVerdict, verdict);
        const SmartBlock smart = block({{1, 100, 100, 0}, {id, 100, 100, raw}});
        QVERIFY(smart.ok);
        QCOMPARE(diskVerdict(DiskKind::Hdd, smart), verdict);
        // Never a verdict on flash.
        QCOMPARE(diskVerdict(DiskKind::Flash, smart), DiskVerdict::Unknown);
    }

    void failingWinsOverWatch()
    {
        const SmartBlock smart = block({{kSpinRetriesId, 100, 100, 4}, {kPendingId, 100, 100, 10}});
        QCOMPARE(diskVerdict(DiskKind::Hdd, smart), DiskVerdict::Failing);
    }

    void noUsableBlock()
    {
        QCOMPARE(diskVerdict(DiskKind::Hdd, SmartBlock()), DiskVerdict::Unknown);
        QCOMPARE(diskVerdict(DiskKind::Hdd, block({{0}})), DiskVerdict::Unknown);

        HealthFile file;
        file.smart = SmartState::Error;
        file.smartError = -3;
        QCOMPARE(readDisk(file, nullptr).verdict, DiskVerdict::Unknown);
        file.smart = SmartState::Ok;
        file.smartData = QStringLiteral("zz");
        QCOMPARE(readDisk(file, nullptr).verdict, DiskVerdict::Unknown);
    }

    void fixtures_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<DiskVerdict>("verdict");
        QTest::newRow("good") << QStringLiteral("health-good.txt") << DiskVerdict::Good;
        QTest::newRow("watch") << QStringLiteral("health-watch.txt") << DiskVerdict::Watch;
        QTest::newRow("failing") << QStringLiteral("health-failing.txt") << DiskVerdict::Failing;
        QTest::newRow("unsupported")
            << QStringLiteral("health-unsupported.txt") << DiskVerdict::Unknown;
        QTest::newRow("flash") << QStringLiteral("health-flash.txt") << DiskVerdict::Unknown;
    }

    void fixtures()
    {
        QFETCH(QString, name);
        QFETCH(DiskVerdict, verdict);
        const HealthReading r = makeReading(fixture(name), std::nullopt, QDateTime());
        QVERIFY(r.ok);
        QVERIFY2(r.log.isEmpty(), qPrintable(r.log.join(QLatin1Char('\n'))));
        QCOMPARE(r.disk.verdict, verdict);
        QVERIFY(r.disk.identify.ok);
        QCOMPARE(r.disk.identify.model, QStringLiteral("TEST HDD 80GB"));
    }

    void shownValues()
    {
        // Section 5.2, on the clean fixture.
        const HealthReading r =
            makeReading(fixture(QStringLiteral("health-good.txt")), std::nullopt, QDateTime());
        const DiskReading &d = r.disk;
        QCOMPARE(d.value(kReallocatedId), std::optional<quint32>(0));
        QCOMPARE(d.value(kPowerOnHoursId), std::optional<quint32>(2140));
        QCOMPARE(d.value(kPowerCyclesId), std::optional<quint32>(1873));
        QCOMPARE(d.value(kHeadLoadsId), std::optional<quint32>(52310));
        QCOMPARE(d.value(kDropEventsId), std::optional<quint32>(41));
        QCOMPARE(d.temperature(), std::optional<int>(31));

        // Flash: still shown, no verdict.
        const HealthReading flash =
            makeReading(fixture(QStringLiteral("health-flash.txt")), std::nullopt, QDateTime());
        QCOMPARE(flash.disk.kind, DiskKind::Flash);
        QCOMPARE(flash.disk.value(kReallocatedId), std::optional<quint32>(3));
        QCOMPARE(flash.disk.verdict, DiskVerdict::Unknown);

        // Without a block nothing is shown.
        const HealthReading older = makeReading(fixture(QStringLiteral("health-unsupported.txt")),
                                                std::nullopt, QDateTime());
        QCOMPARE(older.disk.value(kPowerOnHoursId), std::nullopt);
        QCOMPARE(older.disk.temperature(), std::nullopt);
    }
};

QTEST_GUILESS_MAIN(TestHealthDisk)
#include "tst_health_disk.moc"
