// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The versions of docs/specs/update-channel.md section 5 and the tests
// "Versions" of section 9. The line of the card is tested with the detection
// (tst_device_firmware).

#include "version.h"

#include <QTest>

using namespace tacetos::update;

class TestUpdateVersion : public QObject
{
    Q_OBJECT

private slots:
    void readsEachPart()
    {
        const std::optional<Version> release = parseVersion(QStringLiteral("0.3.0"));
        QVERIFY(release);
        QCOMPARE(release->major, 0);
        QCOMPARE(release->minor, 3);
        QCOMPARE(release->patch, 0);
        QVERIFY(!release->development);
        QVERIFY(release->commit.isEmpty());

        const std::optional<Version> dev =
            parseVersion(QStringLiteral("12.65535.7-dev+g5a1b2c3d4e5f"));
        QVERIFY(dev);
        QCOMPARE(dev->major, 12);
        QCOMPARE(dev->minor, 65535);
        QCOMPARE(dev->patch, 7);
        QVERIFY(dev->development);
        QCOMPARE(dev->commit, QStringLiteral("5a1b2c3d4e5f"));

        const std::optional<Version> devOnly = parseVersion(QStringLiteral("0.3.0-dev"));
        QVERIFY(devOnly);
        QVERIFY(devOnly->development);
        const std::optional<Version> commitOnly =
            parseVersion(QStringLiteral("0.3.0+g000000000000"));
        QVERIFY(commitOnly);
        QVERIFY(!commitOnly->development);
    }

    void refusesOtherShapes_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("leading zero") << QStringLiteral("0.03.0");
        QTest::newRow("leading zero first") << QStringLiteral("01.3.0");
        QTest::newRow("over 65535") << QStringLiteral("0.65536.0");
        QTest::newRow("far over") << QStringLiteral("0.3.999999");
        QTest::newRow("two dots") << QStringLiteral("0..3");
        QTest::newRow("two parts") << QStringLiteral("0.3");
        QTest::newRow("four parts") << QStringLiteral("0.3.0.1");
        QTest::newRow("short commit") << QStringLiteral("0.3.0-dev+g5a1b2c3");
        QTest::newRow("long commit") << QStringLiteral("0.3.0-dev+g5a1b2c3d4e5f6");
        QTest::newRow("upper case commit") << QStringLiteral("0.3.0-dev+g5A1B2C3D4E5F");
        QTest::newRow("no g") << QStringLiteral("0.3.0-dev+5a1b2c3d4e5f");
        QTest::newRow("dev after commit") << QStringLiteral("0.3.0+g5a1b2c3d4e5f-dev");
        QTest::newRow("other suffix") << QStringLiteral("0.3.0-beta");
        QTest::newRow("v in front") << QStringLiteral("v0.3.0");
        QTest::newRow("name in front") << QStringLiteral("TacetOS 0.3.0");
        QTest::newRow("space") << QStringLiteral("0.3.0 ");
        QTest::newRow("new line") << QStringLiteral("0.3.0\n");
        QTest::newRow("minus") << QStringLiteral("-1.3.0");
    }

    void refusesOtherShapes()
    {
        QFETCH(QString, text);
        QVERIFY(!parseVersion(text));
    }

    void ordersVersions_data()
    {
        QTest::addColumn<QString>("older");
        QTest::addColumn<QString>("newer");
        QTest::newRow("patch") << QStringLiteral("0.3.0") << QStringLiteral("0.3.1");
        QTest::newRow("minor") << QStringLiteral("0.3.9") << QStringLiteral("0.4.0");
        QTest::newRow("major") << QStringLiteral("0.65535.65535") << QStringLiteral("1.0.0");
        QTest::newRow("as numbers") << QStringLiteral("0.9.0") << QStringLiteral("0.10.0");
        QTest::newRow("dev before release")
            << QStringLiteral("0.3.0-dev+g5a1b2c3d4e5f") << QStringLiteral("0.3.0");
        QTest::newRow("release before next dev")
            << QStringLiteral("0.3.0") << QStringLiteral("0.4.0-dev+g000000000000");
    }

    void ordersVersions()
    {
        QFETCH(QString, older);
        QFETCH(QString, newer);
        QVERIFY(compareVersions(*parseVersion(older), *parseVersion(newer)) < 0);
        QVERIFY(compareVersions(*parseVersion(newer), *parseVersion(older)) > 0);
        QVERIFY(isNewer(newer, older));
        QVERIFY(!isNewer(older, newer));
    }

    void ignoresTheCommit()
    {
        const QString a = QStringLiteral("0.3.0-dev+g5a1b2c3d4e5f");
        const QString b = QStringLiteral("0.3.0-dev+gffffffffffff");
        QCOMPARE(compareVersions(*parseVersion(a), *parseVersion(b)), 0);
        QVERIFY(!isNewer(a, b));
        QVERIFY(!isNewer(b, a));
        QVERIFY(!isNewer(QStringLiteral("0.3.0"), QStringLiteral("0.3.0")));
        QCOMPARE(compareVersions(*parseVersion(QStringLiteral("0.3.0-dev")), *parseVersion(a)), 0);
    }

    void neverNewerWhenOneSideCannotBeCompared()
    {
        QString why;
        QVERIFY(!isNewer(QStringLiteral("0.04.0"), QStringLiteral("0.3.0"), &why));
        QVERIFY(why.contains(QLatin1String("channel")));
        why.clear();
        QVERIFY(!isNewer(QStringLiteral("9.0.0"), QStringLiteral("0.3"), &why));
        QVERIFY(why.contains(QLatin1String("iPod")));
        why.clear();
        // Two versions that compare leave it empty, newer or not.
        QVERIFY(isNewer(QStringLiteral("0.3.0"), QStringLiteral("0.2.0"), &why));
        QVERIFY(!isNewer(QStringLiteral("0.2.0"), QStringLiteral("0.3.0"), &why));
        QVERIFY(why.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUpdateVersion)
#include "tst_update_version.moc"
