// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The udev rule file of the Classic in DFU mode (docs/specs/bootloader-classic.md,
// sections 15.2 and 17, last point): it matches 05ac and 1223 and nothing else.

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

class UdevRuleTest : public QObject
{
    Q_OBJECT

private:
    static QStringList ruleLines()
    {
        QFile file(QStringLiteral(TACETOS_UDEV_RULE));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        QStringList lines;
        const QStringList all = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (const QString &raw : all) {
            const QString line = raw.trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
                lines.append(line);
        }
        return lines;
    }

private slots:
    void oneRule() { QCOMPARE(ruleLines().size(), 1); }

    void matchesTheDfuIdOnly()
    {
        const QString rule = ruleLines().value(0);
        QVERIFY(!rule.isEmpty());
        QVERIFY(rule.contains(QStringLiteral("SUBSYSTEM==\"usb\"")));
        QVERIFY(rule.contains(QStringLiteral("ATTR{idVendor}==\"05ac\"")));
        QVERIFY(rule.contains(QStringLiteral("ATTR{idProduct}==\"1223\"")));
        // Exactly one test on each id attribute, with ==, and no other id anywhere.
        QCOMPARE(rule.count(QStringLiteral("idVendor")), 1);
        QCOMPARE(rule.count(QStringLiteral("idProduct")), 1);
        QVERIFY(!rule.contains(QStringLiteral("!=")));
        QVERIFY(!rule.contains(QStringLiteral("*")));
        QVERIFY(!rule.contains(QStringLiteral("?")));
        QVERIFY(!rule.contains(QStringLiteral("|")));
        const QStringList ids = {QStringLiteral("05ac"), QStringLiteral("1223")};
        auto it = QRegularExpression(QStringLiteral("\"([0-9a-fA-F]{4})\"")).globalMatch(rule);
        QStringList found;
        while (it.hasNext())
            found << it.next().captured(1).toLower();
        QCOMPARE(found, ids);
    }

    void neverTheNormalMode()
    {
        const QString rule = ruleLines().value(0);
        QVERIFY(!rule.contains(QStringLiteral("1261")));
        // The WTF ids and the Disk Mode id are not matched either.
        for (const char *other : {"1241", "1245", "1247", "1250"})
            QVERIFY2(!rule.contains(QLatin1String(other)), other);
    }

    void tagsForTheLoggedInUser()
    {
        const QString rule = ruleLines().value(0);
        QVERIFY(rule.contains(QStringLiteral("TAG+=\"uaccess\"")));
        // No permissions given to everyone, no program run, no group.
        QVERIFY(!rule.contains(QStringLiteral("MODE")));
        QVERIFY(!rule.contains(QStringLiteral("GROUP")));
        QVERIFY(!rule.contains(QStringLiteral("RUN")));
        QVERIFY(!rule.contains(QStringLiteral("OWNER")));
    }
};

QTEST_GUILESS_MAIN(UdevRuleTest)
#include "tst_installer_udevrule.moc"
