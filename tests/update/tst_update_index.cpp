// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The index of docs/specs/update-channel.md section 3 and the tests "The index"
// of section 9.

#include "constants.h"
#include "index.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace tacetos::update;

namespace {

QJsonObject package(const QString &target, char digit = 'a')
{
    QJsonObject object;
    object.insert(QStringLiteral("target"), target);
    object.insert(QStringLiteral("manifest"), QStringLiteral("tacetos-%1-0.3.0.json").arg(target));
    object.insert(QStringLiteral("size"), 812);
    object.insert(QStringLiteral("sha256"), QString(64, QLatin1Char(digit)));
    return object;
}

QJsonObject goodIndex()
{
    QJsonObject index;
    index.insert(QStringLiteral("format"), 1);
    index.insert(QStringLiteral("version"), QStringLiteral("0.3.0"));
    index.insert(QStringLiteral("built"), QStringLiteral("2026-11-26T10:00:00Z"));
    index.insert(QStringLiteral("packages"), QJsonArray{package(QStringLiteral("ipod6g")),
                                                        package(QStringLiteral("ipodvideo"), 'b')});
    return index;
}

QByteArray bytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson();
}

QJsonObject withPackage(QJsonObject index, int i, const QString &key, const QJsonValue &value)
{
    QJsonArray packages = index.value(QStringLiteral("packages")).toArray();
    QJsonObject entry = packages.at(i).toObject();
    if (value.isUndefined())
        entry.remove(key);
    else
        entry.insert(key, value);
    packages.replace(i, entry);
    index.insert(QStringLiteral("packages"), packages);
    return index;
}

} // namespace

class TestUpdateIndex : public QObject
{
    Q_OBJECT

private slots:
    void readsAGoodIndex()
    {
        const IndexReading reading = parseIndex(bytes(goodIndex()));
        QVERIFY2(reading.ok, qPrintable(reading.error));
        QCOMPARE(reading.index.version, QStringLiteral("0.3.0"));
        QCOMPARE(reading.index.built, QStringLiteral("2026-11-26T10:00:00Z"));
        QCOMPARE(reading.index.packages.size(), 2);
        const IndexPackage *classic = reading.index.package(QStringLiteral("ipod6g"));
        QVERIFY(classic);
        QCOMPARE(classic->manifest, QStringLiteral("tacetos-ipod6g-0.3.0.json"));
        QCOMPARE(classic->size, qint64(812));
        QCOMPARE(classic->sha256, QString(64, QLatin1Char('a')));
        QCOMPARE(reading.index.package(QStringLiteral("ipodvideo"))->sha256,
                 QString(64, QLatin1Char('b')));
    }

    void keepsAKeyItDoesNotKnow()
    {
        QJsonObject index = goodIndex();
        index.insert(QStringLiteral("notes"), QStringLiteral("anything"));
        QVERIFY(parseIndex(bytes(withPackage(index, 0, QStringLiteral("extra"), 1))).ok);
    }

    void aModelMissingHasNoPackageOnlyForItself()
    {
        const IndexReading reading = parseIndex(bytes(goodIndex()));
        QVERIFY(reading.ok);
        QVERIFY(!reading.index.package(QStringLiteral("ipodnano2g")));
        QVERIFY(reading.index.package(QStringLiteral("ipod6g")));
    }

    void refusesABadRoot_data()
    {
        QTest::addColumn<QByteArray>("data");
        auto without = [](const char *key) {
            QJsonObject index = goodIndex();
            index.remove(QLatin1String(key));
            return bytes(index);
        };
        auto with = [](const char *key, const QJsonValue &value) {
            QJsonObject index = goodIndex();
            index.insert(QLatin1String(key), value);
            return bytes(index);
        };
        QTest::newRow("not JSON") << QByteArray("{\"format\": 1,");
        QTest::newRow("a list") << QByteArray("[1, 2]");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("no format") << without("format");
        QTest::newRow("format as text") << with("format", QStringLiteral("1"));
        QTest::newRow("format 2") << with("format", 2);
        QTest::newRow("format 1.5") << with("format", 1.5);
        QTest::newRow("no version") << without("version");
        QTest::newRow("version as number") << with("version", 3);
        QTest::newRow("version empty") << with("version", QString());
        QTest::newRow("no built") << without("built");
        QTest::newRow("built as number") << with("built", 2026);
        QTest::newRow("no packages") << without("packages");
        QTest::newRow("packages as object") << with("packages", QJsonObject());
        QTest::newRow("a package as text")
            << with("packages", QJsonArray{package(QStringLiteral("ipod6g")), QStringLiteral("x")});
    }

    void refusesABadRoot()
    {
        QFETCH(QByteArray, data);
        const IndexReading reading = parseIndex(data);
        QVERIFY(!reading.ok);
        QVERIFY(!reading.error.isEmpty());
    }

    void refusesABadPackage_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        const QJsonValue missing(QJsonValue::Undefined);
        QTest::newRow("no target") << QStringLiteral("target") << missing;
        QTest::newRow("target as number") << QStringLiteral("target") << QJsonValue(6);
        QTest::newRow("target empty") << QStringLiteral("target") << QJsonValue(QString());
        QTest::newRow("no manifest") << QStringLiteral("manifest") << missing;
        QTest::newRow("manifest as number") << QStringLiteral("manifest") << QJsonValue(1);
        QTest::newRow("manifest in a folder")
            << QStringLiteral("manifest") << QJsonValue(QStringLiteral("a/b.json"));
        QTest::newRow("manifest with a backslash")
            << QStringLiteral("manifest") << QJsonValue(QStringLiteral("a\\b.json"));
        QTest::newRow("manifest up")
            << QStringLiteral("manifest") << QJsonValue(QStringLiteral(".."));
        QTest::newRow("no size") << QStringLiteral("size") << missing;
        QTest::newRow("size as text")
            << QStringLiteral("size") << QJsonValue(QStringLiteral("812"));
        QTest::newRow("size zero") << QStringLiteral("size") << QJsonValue(0);
        QTest::newRow("size below zero") << QStringLiteral("size") << QJsonValue(-1);
        QTest::newRow("size not whole") << QStringLiteral("size") << QJsonValue(8.5);
        QTest::newRow("no sha256") << QStringLiteral("sha256") << missing;
        QTest::newRow("sha256 as number") << QStringLiteral("sha256") << QJsonValue(1);
        QTest::newRow("sha256 short")
            << QStringLiteral("sha256") << QJsonValue(QString(63, QLatin1Char('a')));
        QTest::newRow("sha256 upper case")
            << QStringLiteral("sha256") << QJsonValue(QString(64, QLatin1Char('A')));
    }

    void refusesABadPackage()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        const IndexReading reading = parseIndex(bytes(withPackage(goodIndex(), 1, key, value)));
        QVERIFY(!reading.ok);
        QVERIFY(!reading.error.isEmpty());
    }

    void refusesATargetTwice()
    {
        QJsonObject index = goodIndex();
        index.insert(
            QStringLiteral("packages"),
            QJsonArray{package(QStringLiteral("ipod6g")), package(QStringLiteral("ipod6g"), 'c')});
        const IndexReading reading = parseIndex(bytes(index));
        QVERIFY(!reading.ok);
        QVERIFY(reading.error.contains(QLatin1String("ipod6g")));
    }

    void refusesAFileOverTheLimit()
    {
        // A good index made larger by a key it does not know, by one byte.
        QJsonObject index = goodIndex();
        index.insert(QStringLiteral("pad"), QString());
        const qint64 base = bytes(index).size();
        const qint64 limit = tacetos::device::kIndexMaxBytes;
        index.insert(QStringLiteral("pad"), QString(limit - base, QLatin1Char('x')));
        QCOMPARE(bytes(index).size(), limit);
        QVERIFY(parseIndex(bytes(index)).ok);

        index.insert(QStringLiteral("pad"), QString(limit - base + 1, QLatin1Char('x')));
        const IndexReading reading = parseIndex(bytes(index));
        QVERIFY(!reading.ok);
        QVERIFY(reading.error.contains(QLatin1String("larger")));
    }
};

QTEST_GUILESS_MAIN(TestUpdateIndex)
#include "tst_update_index.moc"
