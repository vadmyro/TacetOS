// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md sections 7.1 and 15.4: the checks of a package
// of the firmware, one case for each refusal. The packages are made by the
// test, with files that are no real build.

#include "backup.h"
#include "constants.h"
#include "package.h"
#include "testzip.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::wizard;
using tacetos::wizard::testing::storedZip;
namespace device = tacetos::device;

namespace {

const QString kTarget = QStringLiteral("ipod6g");

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QString sha(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray fw(const char *rest)
{
    return QByteArray(device::kFirmwareFolder.data(), device::kFirmwareFolder.size()) + rest;
}

} // namespace

class TstWizardPackage : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    int m_count = 0;

    // A good package in a folder of its own. Returns the path of the manifest;
    // `change` edits the manifest before it is written.
    QString makePackage(const std::function<void(QJsonObject &, QString folder)> &change = {},
                        const QByteArray &zip = QByteArray())
    {
        const QString folder = m_tmp.filePath(QStringLiteral("p%1").arg(++m_count));
        QDir().mkpath(folder);
        const QByteArray firmware = zip.isEmpty()
                                        ? storedZip({{fw("/"), QByteArray()},
                                                     {fw("/tacetos.ipod"), QByteArray(4000, 'f')},
                                                     {fw("/themes/a.cfg"), QByteArray("theme")}})
                                        : zip;
        const QByteArray bootloader(2000, 'b');
        writeFile(folder + QStringLiteral("/tacetos-ipod6g-0.2.0.zip"), firmware);
        writeFile(folder + QStringLiteral("/tacetos-bootloader-ipod6g-0.2.0.ipod"), bootloader);
        auto file = [](const char *name, const char *kind, const QByteArray &bytes) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), QLatin1String(name));
            o.insert(QStringLiteral("kind"), QLatin1String(kind));
            o.insert(QStringLiteral("size"), double(bytes.size()));
            o.insert(QStringLiteral("sha256"), sha(bytes));
            return o;
        };
        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), 1);
        manifest.insert(QStringLiteral("target"), kTarget);
        manifest.insert(QStringLiteral("version"), QStringLiteral("0.2.0+gabc1234"));
        manifest.insert(
            QStringLiteral("files"),
            QJsonArray{file("tacetos-ipod6g-0.2.0.zip", "firmware", firmware),
                       file("tacetos-bootloader-ipod6g-0.2.0.ipod", "bootloader", bootloader)});
        if (change)
            change(manifest, folder);
        const QString path = folder + QStringLiteral("/tacetos-ipod6g-0.2.0.json");
        writeFile(path, QJsonDocument(manifest).toJson());
        return path;
    }

    static QJsonArray files(const QJsonObject &manifest)
    {
        return manifest.value(QStringLiteral("files")).toArray();
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void good()
    {
        const QString manifest = makePackage();
        PackageFiles found;
        const PackageCheck check =
            checkPackage(manifest, kTarget, QString(device::kFirmwareFolder), &found);
        QVERIFY2(check.code.isEmpty(), qPrintable(check.code + check.message));
        QCOMPARE(check.unpackedBytes, 4005);
        QFile f(manifest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(check.manifestSha256, sha(f.readAll()));
        QCOMPARE(found.manifest, manifest);
        QVERIFY(found.firmware.endsWith(QStringLiteral("/tacetos-ipod6g-0.2.0.zip")));
        QVERIFY(found.bootloader.endsWith(QStringLiteral("/tacetos-bootloader-ipod6g-0.2.0.ipod")));
        QCOMPARE(found.version, QStringLiteral("0.2.0+gabc1234"));

        // Section 7.1: the same package as at the start of the flow, or not.
        QVERIFY(sameAsFlow(check, check.manifestSha256).code.isEmpty());
        QVERIFY(sameAsFlow(check, QString()).code.isEmpty());
        QCOMPARE(sameAsFlow(check, sha("other")).code, QString(error::packageChanged));
        PackageCheck refused = check;
        refused.code = QString(error::packageDamaged);
        QCOMPARE(sameAsFlow(refused, sha("other")).code, QString(error::packageDamaged));
    }

    // A model with its own firmware folder takes only a zip in that folder.
    void folderOfTheModel()
    {
        const QByteArray own =
            storedZip({{".own/", QByteArray()}, {".own/tacetos.ipod", QByteArray(4000, 'f')}});
        const QString ownPackage = makePackage({}, own);
        QVERIFY(checkPackage(ownPackage, kTarget, QStringLiteral(".own")).code.isEmpty());
        QCOMPARE(checkPackage(ownPackage, kTarget, QString(device::kFirmwareFolder)).code,
                 QString(error::packageDamaged));
        QCOMPARE(checkPackage(makePackage(), kTarget, QStringLiteral(".own")).code,
                 QString(error::packageDamaged));
    }

    // Each check of section 7.1, in its order.
    void refused_data()
    {
        QTest::addColumn<QString>("problem");
        QTest::addColumn<QString>("code");
        const QString format(error::packageFormat);
        const QString model(error::packageWrongModel);
        const QString incomplete(error::packageIncomplete);
        const QString damaged(error::packageDamaged);
        QTest::newRow("not JSON") << "not-json" << format;
        QTest::newRow("a list") << "list" << format;
        QTest::newRow("too large") << "large" << format;
        QTest::newRow("format 2") << "format-2" << format;
        QTest::newRow("format as text") << "format-text" << format;
        QTest::newRow("another model") << "target" << model;
        QTest::newRow("no target") << "no-target" << model;
        QTest::newRow("one file") << "one-file" << incomplete;
        QTest::newRow("three files") << "three-files" << incomplete;
        QTest::newRow("two firmwares") << "two-firmwares" << incomplete;
        QTest::newRow("a folder in a name") << "folder-name" << incomplete;
        QTest::newRow("a file missing") << "missing" << incomplete;
        QTest::newRow("no manifest") << "no-manifest" << incomplete;
        QTest::newRow("another size") << "size" << damaged;
        QTest::newRow("another sha256") << "sha" << damaged;
        QTest::newRow("not a zip") << "not-zip" << damaged;
        QTest::newRow("a file outside the folder") << "outside" << damaged;
        QTest::newRow("a refused zip") << "refused-zip" << damaged;
    }

    void refused()
    {
        QFETCH(QString, problem);
        QFETCH(QString, code);
        QByteArray zip;
        if (problem == QLatin1String("not-zip"))
            zip = QByteArray(3000, 'x');
        else if (problem == QLatin1String("outside"))
            zip = storedZip({{fw("/a.cfg"), "a"}, {"README.txt", "r"}});
        else if (problem == QLatin1String("refused-zip"))
            zip = storedZip({{fw("/../a.cfg"), "a"}});
        QString manifest = makePackage(
            [&problem](QJsonObject &m, const QString &folder) {
                QJsonArray list = files(m);
                QJsonObject first = list.at(0).toObject();
                if (problem == QLatin1String("format-2"))
                    m.insert(QStringLiteral("format"), 2);
                else if (problem == QLatin1String("format-text"))
                    m.insert(QStringLiteral("format"), QStringLiteral("1"));
                else if (problem == QLatin1String("large"))
                    m.insert(QStringLiteral("notes"), QString(70 * 1024, QLatin1Char('n')));
                else if (problem == QLatin1String("target"))
                    m.insert(QStringLiteral("target"), QStringLiteral("ipodvideo"));
                else if (problem == QLatin1String("no-target"))
                    m.remove(QStringLiteral("target"));
                else if (problem == QLatin1String("one-file"))
                    list.removeLast();
                else if (problem == QLatin1String("three-files"))
                    list.append(first);
                else if (problem == QLatin1String("two-firmwares")) {
                    QJsonObject second = list.at(1).toObject();
                    second.insert(QStringLiteral("kind"), QStringLiteral("firmware"));
                    list.replace(1, second);
                } else if (problem == QLatin1String("folder-name")) {
                    first.insert(QStringLiteral("name"),
                                 QStringLiteral("sub/tacetos-ipod6g-0.2.0.zip"));
                    list.replace(0, first);
                } else if (problem == QLatin1String("missing"))
                    QFile::remove(folder + QStringLiteral("/tacetos-bootloader-ipod6g-0.2.0.ipod"));
                else if (problem == QLatin1String("size")) {
                    first.insert(QStringLiteral("size"),
                                 first.value(QStringLiteral("size")).toDouble() + 1);
                    list.replace(0, first);
                } else if (problem == QLatin1String("sha")) {
                    first.insert(QStringLiteral("sha256"), sha("something else"));
                    list.replace(0, first);
                }
                m.insert(QStringLiteral("files"), list);
            },
            zip);
        if (problem == QLatin1String("not-json"))
            writeFile(manifest, "{ format: 1");
        else if (problem == QLatin1String("list"))
            writeFile(manifest, "[1]");
        else if (problem == QLatin1String("no-manifest"))
            QFile::remove(manifest);
        PackageFiles found;
        const PackageCheck check =
            checkPackage(manifest, kTarget, QString(device::kFirmwareFolder), &found);
        QCOMPARE(check.code, code);
        QVERIFY(!check.message.isEmpty());
        QVERIFY(found.firmware.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstWizardPackage)
#include "tst_wizard_package.moc"
