// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The TOML reader and the USB id to models mapping of the device library
// (docs/specs/detection.md section 3.2).

#include "models.h"
#include "toml.h"

#include <QTest>

using namespace tacetos::device;

namespace {

Model model(const char *id, QStringList normal, QStringList dfu = {})
{
    Model m;
    m.id = QString::fromLatin1(id);
    m.usbNormal = normal;
    m.usbDiskMode = normal;
    m.usbDfu = dfu;
    return m;
}

} // namespace

class TestDeviceModels : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void tomlValues();
    void tomlErrors_data();
    void tomlErrors();
    void tomlMissingFile();

    void loadsTheTenModels();
    void modelListErrors_data();
    void modelListErrors();

    void classicNormal();
    void classicDfu();
    void miniHasTwoCandidates();
    void nanoDfuIds();
    void unknownIdHasNoCandidate();
    void idIsNormalised();
    void normalAndDfuClash();
    void everyIdOfTheFileMapsBack();

private:
    QList<Model> m_models;
};

void TestDeviceModels::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    m_models = list.models;
}

void TestDeviceModels::tomlValues()
{
    const TomlDocument doc = parseTomlText("format_version = 1 # comment\n"
                                           "[sources.a]\n"
                                           "title = \"has # inside, and a comma\"\n"
                                           "checked = 2026-11-08\n"
                                           "\n"
                                           "[[model]]\n"
                                           "id = \"m\"\n"
                                           "to_verify = [\"x\", \"y\"]\n"
                                           "[model.screen]\n"
                                           "diagonal_inches = 2.5\n"
                                           "colour = true\r\n"
                                           "[[model]]\n"
                                           "id = \"n\"\n");
    QVERIFY2(doc.error.isEmpty(), qPrintable(doc.error));
    QCOMPARE(doc.root.values.value(QStringLiteral("format_version")).toInt(), 1);
    const TomlTable a =
        doc.root.children.value(QStringLiteral("sources")).children.value(QStringLiteral("a"));
    QCOMPARE(a.values.value(QStringLiteral("title")).toString(),
             QStringLiteral("has # inside, and a comma"));
    QCOMPARE(a.values.value(QStringLiteral("checked")).toDate(), QDate(2026, 11, 8));
    QCOMPARE(doc.models.size(), 2);
    QCOMPARE(doc.models[0].values.value(QStringLiteral("to_verify")).toList().size(), 2);
    const TomlTable screen = doc.models[0].children.value(QStringLiteral("screen"));
    QCOMPARE(screen.values.value(QStringLiteral("diagonal_inches")).toDouble(), 2.5);
    QVERIFY(screen.values.value(QStringLiteral("colour")).toBool());
    QVERIFY(doc.models[1].children.isEmpty());
}

void TestDeviceModels::tomlErrors_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("no equal sign") << QByteArray("a\n") << QStringLiteral("line 1: no '='");
    QTest::newRow("duplicate key")
        << QByteArray("a = 1\na = 2\n") << QStringLiteral("line 2: duplicate key a");
    QTest::newRow("unterminated string")
        << QByteArray("a = \"x\n") << QStringLiteral("line 1: unterminated string");
    QTest::newRow("unterminated array")
        << QByteArray("a = [1, 2\n") << QStringLiteral("line 1: unterminated array");
    QTest::newRow("unknown value")
        << QByteArray("a = nope\n") << QStringLiteral("line 1: cannot read value");
    QTest::newRow("other array of tables")
        << QByteArray("\n[[other]]\n") << QStringLiteral("line 2: unexpected");
    QTest::newRow("model table first")
        << QByteArray("[model.usb]\n") << QStringLiteral("line 1: [model.*] before [[model]]");
}

void TestDeviceModels::tomlErrors()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, expected);
    const TomlDocument doc = parseTomlText(text);
    QVERIFY2(doc.error.startsWith(expected), qPrintable(doc.error));
}

void TestDeviceModels::tomlMissingFile()
{
    const TomlDocument doc = parseTomlFile(QStringLiteral("/nonexistent/models.toml"));
    QVERIFY(doc.error.startsWith(QLatin1String("cannot open")));
    QVERIFY(loadModels(QStringLiteral("/nonexistent/models.toml"))
                .error.startsWith(QLatin1String("cannot open")));
}

void TestDeviceModels::loadsTheTenModels()
{
    QCOMPARE(m_models.size(), 10);
    QCOMPARE(m_models.first().id, QStringLiteral("classic-6g"));
    QCOMPARE(m_models.first().family, QStringLiteral("classic"));
    QVERIFY(!m_models.first().name.isEmpty());
    QCOMPARE(m_models.first().usbDfu, QStringList{QStringLiteral("05ac:1223")});
    // The first and second generations have no USB id.
    for (const Model &m : m_models) {
        if (m.id == QLatin1String("ipod-1g2g"))
            QVERIFY(m.usbNormal.isEmpty() && m.usbDiskMode.isEmpty() && m.usbDfu.isEmpty());
    }
}

void TestDeviceModels::modelListErrors_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("reader error") << QByteArray("oops\n") << QStringLiteral("line 1");
    QTest::newRow("no id") << QByteArray("[[model]]\nname = \"x\"\n")
                           << QStringLiteral("a [[model]] has no id");
    QTest::newRow("same id twice") << QByteArray("[[model]]\nid = \"a\"\n[[model]]\nid = \"a\"\n")
                                   << QStringLiteral("a: id used twice");
    QTest::newRow("upper case usb id")
        << QByteArray("[[model]]\nid = \"a\"\n[model.usb]\nnormal = [\"05AC:1261\"]\n")
        << QStringLiteral("a: bad usb id '05AC:1261' in usb.normal");
    QTest::newRow("usb id without colon")
        << QByteArray("[[model]]\nid = \"a\"\n[model.usb]\ndfu = [\"05ac1223\"]\n")
        << QStringLiteral("a: bad usb id '05ac1223' in usb.dfu");
}

void TestDeviceModels::modelListErrors()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, expected);
    const ModelList list = modelsFromDocument(parseTomlText(text));
    QVERIFY2(list.error.startsWith(expected), qPrintable(list.error));
}

void TestDeviceModels::classicNormal()
{
    const UsbMatch match = matchUsbId(m_models, QStringLiteral("05ac:1261"));
    QCOMPARE(match.mode, UsbMode::Normal);
    QCOMPARE(match.candidates, QStringList{QStringLiteral("classic-6g")});
}

void TestDeviceModels::classicDfu()
{
    const UsbMatch match = matchUsbId(m_models, QStringLiteral("05ac:1223"));
    QCOMPARE(match.mode, UsbMode::Dfu);
    QCOMPARE(match.candidates, QStringList{QStringLiteral("classic-6g")});
}

void TestDeviceModels::miniHasTwoCandidates()
{
    const UsbMatch match = matchUsbId(m_models, QStringLiteral("05ac:1205"));
    QCOMPARE(match.mode, UsbMode::Normal);
    // Both lists of one model hold the id: the model is still listed once.
    QCOMPARE(match.candidates, (QStringList{QStringLiteral("mini-1g"), QStringLiteral("mini-2g")}));
}

void TestDeviceModels::nanoDfuIds()
{
    for (const char *id : {"05ac:1220", "05ac:1240"}) {
        const UsbMatch match = matchUsbId(m_models, QString::fromLatin1(id));
        QCOMPARE(match.mode, UsbMode::Dfu);
        QCOMPARE(match.candidates, QStringList{QStringLiteral("nano-2g")});
    }
}

void TestDeviceModels::unknownIdHasNoCandidate()
{
    for (const char *id : {"05ac:12a8", "05ac:ffff", "046d:c52b", "", "nonsense"}) {
        const UsbMatch match = matchUsbId(m_models, QString::fromLatin1(id));
        QCOMPARE(match.mode, UsbMode::None);
        QVERIFY(match.candidates.isEmpty());
    }
}

void TestDeviceModels::idIsNormalised()
{
    const UsbMatch match = matchUsbId(m_models, QStringLiteral(" 05AC:120A "));
    QCOMPARE(match.mode, UsbMode::Normal);
    QCOMPARE(match.candidates, QStringList{QStringLiteral("nano-1g")});
}

void TestDeviceModels::normalAndDfuClash()
{
    const QList<Model> models = {model("a", {QStringLiteral("05ac:1111")}),
                                 model("b", {}, {QStringLiteral("05ac:1111")})};
    const UsbMatch match = matchUsbId(models, QStringLiteral("05ac:1111"));
    QCOMPARE(match.mode, UsbMode::Other);
    QCOMPARE(match.candidates, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestDeviceModels::everyIdOfTheFileMapsBack()
{
    int checked = 0;
    for (const Model &m : m_models) {
        const QStringList all = m.usbNormal + m.usbDiskMode + m.usbDfu;
        for (const QString &id : all) {
            const UsbMatch match = matchUsbId(m_models, id);
            QVERIFY2(match.candidates.contains(m.id),
                     qPrintable(QStringLiteral("%1 does not map back to %2").arg(id, m.id)));
            QVERIFY(match.mode != UsbMode::None);
            ++checked;
        }
    }
    QVERIFY(checked > 0);
}

QTEST_GUILESS_MAIN(TestDeviceModels)
#include "tst_device_models.moc"
