// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/firmware-branding.md section 8.3: the firmware folder of an iPod
// Classic moved to the folder of its model. Temporary folders play the music
// volume and the data folder of the app.

#include "constants.h"
#include "foldermove.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::wizard;
namespace device = tacetos::device;

namespace {

const QString kSerial = QStringLiteral("8K7MOVE");
const QString kOwn = QStringLiteral(".own");

QString base()
{
    return QString(device::kFirmwareFolder);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("?");
}

} // namespace

class TstWizardFolderMove : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    int m_count = 0;

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    QString record(const QString &operation, const QString &date, const QString &folder)
    {
        QJsonObject o{{"format", 1}, {"operation", operation}, {"date", date}, {"serial", kSerial}};
        if (!folder.isEmpty())
            o.insert(QStringLiteral("firmwareFolder"), folder);
        const QString path = newFolder(QStringLiteral("record")) + QStringLiteral("/r.json");
        writeFile(path, QJsonDocument(o).toJson());
        return path;
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // Step 1: the bootloader goes in first when the old folder is there and the
    // last install record does not say that its bootloader knows the new one.
    void bootloaderFirstFromRecords()
    {
        const QString volume = newFolder(QStringLiteral("volume"));
        const QString empty = newFolder(QStringLiteral("empty"));
        QVERIFY(QDir(volume).mkdir(base()));
        const QString older = record("install", "2026-01-01T10:00:00Z", QString());
        const QString knows = record("install", "2026-02-01T10:00:00Z", kOwn);
        const QString gone = record("uninstall", "2026-03-01T10:00:00Z", QString());

        QVERIFY(bootloaderFirst(volume, kOwn, {}, kSerial));
        QVERIFY(bootloaderFirst(volume, kOwn, {older}, kSerial));
        QVERIFY(!bootloaderFirst(volume, kOwn, {older, knows}, kSerial));
        QVERIFY(bootloaderFirst(volume, QStringLiteral(".other"), {knows}, kSerial));
        QVERIFY(!bootloaderFirst(volume, kOwn, {older, gone}, kSerial));
        QVERIFY(bootloaderFirst(volume, kOwn, {knows}, QStringLiteral("OTHER")));
        // Nothing to move: no old folder, the model keeps the default one, no volume.
        QVERIFY(!bootloaderFirst(empty, kOwn, {older}, kSerial));
        QVERIFY(!bootloaderFirst(volume, base(), {older}, kSerial));
        QVERIFY(!bootloaderFirst(QString(), kOwn, {older}, kSerial));
    }

    // Step 4: a path that starts with the old folder, after any separator the
    // settings and the screens use, any case. Nothing else changes.
    void rewritesPaths_data()
    {
        const QByteArray b = base().toUtf8();
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("setting") << "wps: /" + b + "/wps/a.wps\n"
                                 << QByteArray("wps: /.own/wps/a.wps\n");
        QTest::newRow("no space") << "font:/" + b + "/fonts/a.fnt"
                                  << QByteArray("font:/.own/fonts/a.fnt");
        QTest::newRow("tag") << "%xl(A,/" + b + "/a.bmp,0,0)"
                             << QByteArray("%xl(A,/.own/a.bmp,0,0)");
        QTest::newRow("start of a line") << "x\n/" + b + "/a\n" << QByteArray("x\n/.own/a\n");
        QTest::newRow("the folder alone") << "start: /" + b + "\n" << QByteArray("start: /.own\n");
        QTest::newRow("upper case") << "a: /" + b.toUpper() + "/x" << QByteArray("a: /.own/x");
        QTest::newRow("quoted") << "\"/" + b + "/x\"" << QByteArray("\"/.own/x\"");
        QTest::newRow("two on a line")
            << ",/" + b + "/a,/" + b + "/b" << QByteArray(",/.own/a,/.own/b");
        const QByteArray inside = "a: /Music/" + b + "/x\n";
        QTest::newRow("not at the start") << inside << inside;
        const QByteArray longer = "a: /" + b + "x/y\n";
        QTest::newRow("a longer name") << longer << longer;
        const QByteArray bytes = "name: \xc3\xa9\xff /" + b + "/x";
        QTest::newRow("other bytes kept") << bytes << QByteArray("name: \xc3\xa9\xff /.own/x");
    }

    void rewritesPaths()
    {
        QFETCH(QByteArray, text);
        QFETCH(QByteArray, expected);
        QCOMPARE(rewrittenPaths(text, base(), kOwn), expected);
    }

    // Step 4: only the settings at the root and in the themes folder, and the
    // screen files of the wps folder.
    void rewritesOnlyItsFiles()
    {
        const QString folder = newFolder(QStringLiteral("folder"));
        const QByteArray old = "x: /" + base().toUtf8() + "/a\n";
        const QByteArray now = "x: /.own/a\n";
        const QStringList touched{QStringLiteral("config.cfg"),   QStringLiteral(".resume.cfg"),
                                  QStringLiteral("themes/t.cfg"), QStringLiteral("themes/d/u.CFG"),
                                  QStringLiteral("wps/a.wps"),    QStringLiteral("wps/a.sbs"),
                                  QStringLiteral("wps/d/a.fms")};
        const QStringList kept{QStringLiteral("rocks.data/a.cfg"), QStringLiteral("wps/a.bmp"),
                               QStringLiteral("themes/a.wps"), QStringLiteral(".playlist_control"),
                               QStringLiteral("fonts/a.cfg")};
        for (const QString &name : touched + kept)
            QVERIFY(writeFile(folder + QLatin1Char('/') + name, old));
        int writes = 0;
        QCOMPARE(rewriteFolderPaths(folder, base(), kOwn, [&writes] { ++writes; }), QString());
        for (const QString &name : touched)
            QCOMPARE(readFile(folder + QLatin1Char('/') + name), now);
        for (const QString &name : kept)
            QCOMPARE(readFile(folder + QLatin1Char('/') + name), old);
        QCOMPARE(writes, 1);

        // Again: nothing left to rewrite, nothing written.
        writes = 0;
        QCOMPARE(rewriteFolderPaths(folder, base(), kOwn, [&writes] { ++writes; }), QString());
        QCOMPARE(writes, 0);
    }

    // Step 2: one rename when the new folder is not there; a merge when it is.
    void movesOrMerges()
    {
        const QString volume = newFolder(QStringLiteral("volume"));
        const QString old = volume + QLatin1Char('/') + base();
        const QString own = volume + QLatin1Char('/') + kOwn;
        QVERIFY(writeFile(old + QStringLiteral("/a.cfg"), "a"));
        QVERIFY(writeFile(old + QStringLiteral("/d/e/.b"), "b"));
        int writes = 0;
        QCOMPARE(moveFirmwareFolder(volume, base(), kOwn, [&writes] { ++writes; }), QString());
        QVERIFY(!QFileInfo::exists(old));
        QCOMPARE(readFile(own + QStringLiteral("/d/e/.b")), QByteArray("b"));
        QCOMPARE(writes, 1);

        // Both: the copy of the new folder wins, the rest goes in, the old folder goes.
        QVERIFY(writeFile(old + QStringLiteral("/a.cfg"), "old a"));
        QVERIFY(writeFile(old + QStringLiteral("/d/c"), "c"));
        QVERIFY(QDir().mkpath(old + QStringLiteral("/empty/deeper")));
        QCOMPARE(moveFirmwareFolder(volume, base(), kOwn), QString());
        QVERIFY(!QFileInfo::exists(old));
        QCOMPARE(readFile(own + QStringLiteral("/a.cfg")), QByteArray("a"));
        QCOMPARE(readFile(own + QStringLiteral("/d/c")), QByteArray("c"));

        // Nothing to move: nothing written.
        writes = 0;
        QCOMPARE(moveFirmwareFolder(volume, base(), kOwn, [&writes] { ++writes; }), QString());
        QCOMPARE(writes, 0);

        // A file where the new folder should be: refused, the old folder stays.
        const QString other = newFolder(QStringLiteral("volume"));
        QVERIFY(writeFile(other + QLatin1Char('/') + base() + QStringLiteral("/a"), "a"));
        QVERIFY(writeFile(other + QLatin1Char('/') + kOwn, "a file"));
        QVERIFY(!moveFirmwareFolder(other, base(), kOwn).isEmpty());
        QCOMPARE(readFile(other + QLatin1Char('/') + base() + QStringLiteral("/a")),
                 QByteArray("a"));
    }
};

QTEST_GUILESS_MAIN(TstWizardFolderMove)
#include "tst_wizard_foldermove.moc"
