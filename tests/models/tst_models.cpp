// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Reads models/models.toml and checks that the ten models are there with every
// field of the format described in models/README.md. Qt has no TOML reader, so
// this file has a small one that understands only what models.toml uses:
// comments, [table] and [[array of tables]] headers, and single-line values
// (string, integer, float, boolean, date, array).

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

namespace {

struct Table
{
    QMap<QString, QVariant> values;
    QMap<QString, Table> children;
};

struct Document
{
    Table root;
    QList<Table> models;
    QString error;
};

QString stripComment(const QString &line)
{
    bool inString = false;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('"'))
            inString = !inString;
        else if (line[i] == QLatin1Char('#') && !inString)
            return line.left(i);
    }
    return line;
}

// Splits "a", "b" or 1, 2 on the commas that are outside strings.
QStringList splitArray(const QString &inner)
{
    QStringList parts;
    QString current;
    bool inString = false;
    for (const QChar c : inner) {
        if (c == QLatin1Char('"'))
            inString = !inString;
        if (c == QLatin1Char(',') && !inString) {
            parts << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.trimmed().isEmpty())
        parts << current.trimmed();
    return parts;
}

QVariant parseValue(const QString &text, QString *error)
{
    static const QRegularExpression dateRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    static const QRegularExpression intRe(QStringLiteral("^-?\\d+$"));
    static const QRegularExpression floatRe(QStringLiteral("^-?\\d+\\.\\d+$"));

    if (text.startsWith(QLatin1Char('"'))) {
        if (text.size() < 2 || !text.endsWith(QLatin1Char('"')))
            return *error = QStringLiteral("unterminated string: ") + text, QVariant();
        return text.mid(1, text.size() - 2);
    }
    if (text.startsWith(QLatin1Char('['))) {
        if (!text.endsWith(QLatin1Char(']')))
            return *error = QStringLiteral("unterminated array: ") + text, QVariant();
        QVariantList list;
        for (const QString &part : splitArray(text.mid(1, text.size() - 2)))
            list << parseValue(part, error);
        return list;
    }
    if (text == QLatin1String("true"))
        return true;
    if (text == QLatin1String("false"))
        return false;
    if (dateRe.match(text).hasMatch())
        return QDate::fromString(text, Qt::ISODate);
    if (intRe.match(text).hasMatch())
        return text.toLongLong();
    if (floatRe.match(text).hasMatch())
        return text.toDouble();
    *error = QStringLiteral("cannot read value: ") + text;
    return QVariant();
}

Document parseFile(const QString &path)
{
    Document doc;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        doc.error = QStringLiteral("cannot open ") + path;
        return doc;
    }

    Table *current = &doc.root;
    int lineNo = 0;
    while (!file.atEnd()) {
        ++lineNo;
        const QString line = stripComment(QString::fromUtf8(file.readLine())).trimmed();
        if (line.isEmpty())
            continue;

        QString err;
        if (line.startsWith(QLatin1String("[["))) {
            if (line != QLatin1String("[[model]]")) {
                doc.error = QStringLiteral("line %1: unexpected %2").arg(lineNo).arg(line);
                return doc;
            }
            doc.models.append(Table());
            current = &doc.models.last();
        } else if (line.startsWith(QLatin1Char('['))) {
            const QStringList path = line.mid(1, line.size() - 2).split(QLatin1Char('.'));
            Table *base = nullptr;
            int first = 0;
            if (path.first() == QLatin1String("model")) {
                if (doc.models.isEmpty()) {
                    doc.error = QStringLiteral("line %1: [model.*] before [[model]]").arg(lineNo);
                    return doc;
                }
                base = &doc.models.last();
                first = 1;
            } else {
                base = &doc.root;
            }
            for (int i = first; i < path.size(); ++i)
                base = &base->children[path[i]];
            current = base;
        } else {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq < 0) {
                doc.error = QStringLiteral("line %1: no '='").arg(lineNo);
                return doc;
            }
            const QString key = line.left(eq).trimmed();
            const QVariant value = parseValue(line.mid(eq + 1).trimmed(), &err);
            if (!err.isEmpty()) {
                doc.error = QStringLiteral("line %1: %2").arg(lineNo).arg(err);
                return doc;
            }
            if (current->values.contains(key)) {
                doc.error = QStringLiteral("line %1: duplicate key %2").arg(lineNo).arg(key);
                return doc;
            }
            current->values.insert(key, value);
        }
    }
    return doc;
}

const QStringList kExpectedIds = {
    QStringLiteral("ipod-1g2g"),  QStringLiteral("ipod-3g"), QStringLiteral("ipod-4g"),
    QStringLiteral("ipod-color"), QStringLiteral("mini-1g"), QStringLiteral("mini-2g"),
    QStringLiteral("ipod-video"), QStringLiteral("nano-1g"), QStringLiteral("nano-2g"),
    QStringLiteral("classic-6g"),
};

const QStringList kFactNames = {
    QStringLiteral("family"),
    QStringLiteral("name"),
    QStringLiteral("usb_normal"),
    QStringLiteral("usb_disk_mode"),
    QStringLiteral("usb_dfu"),
    QStringLiteral("screen"),
    QStringLiteral("storage"),
    QStringLiteral("install_method"),
    QStringLiteral("bootloader_target"),
};

QString modelId(const Table &model)
{
    return model.values.value(QStringLiteral("id")).toString();
}

} // namespace

class TestModels : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void formatVersion();
    void sourcesAreComplete();
    void hasTheTenModels();
    void topLevelFields();
    void usbIds();
    void screen();
    void storage();
    void install();
    void factSources();
    void toVerifyEntries();

private:
    const Table &model(const QString &id) const;
    void forEachModel(const std::function<void(const Table &, const QString &)> &check) const;

    Document m_doc;
};

void TestModels::initTestCase()
{
    m_doc = parseFile(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(m_doc.error.isEmpty(), qPrintable(m_doc.error));
}

const Table &TestModels::model(const QString &id) const
{
    for (const Table &m : m_doc.models) {
        if (modelId(m) == id)
            return m;
    }
    qFatal("model %s not found", qPrintable(id));
}

void TestModels::forEachModel(
    const std::function<void(const Table &, const QString &)> &check) const
{
    for (const Table &m : m_doc.models)
        check(m, modelId(m));
}

void TestModels::formatVersion()
{
    QCOMPARE(m_doc.root.values.value(QStringLiteral("format_version")).toInt(), 1);
}

void TestModels::sourcesAreComplete()
{
    const Table &sources = m_doc.root.children.value(QStringLiteral("sources"));
    QVERIFY(!sources.children.isEmpty());

    const QSet<QString> kinds = {
        QStringLiteral("database"),          QStringLiteral("firmware-sources"),
        QStringLiteral("installer-sources"), QStringLiteral("wiki"),
        QStringLiteral("datasheet"),         QStringLiteral("measured")};
    for (auto it = sources.children.cbegin(); it != sources.children.cend(); ++it) {
        const QByteArray id = it.key().toUtf8();
        const Table &s = it.value();
        for (const char *key : {"kind", "title", "location", "checked"})
            QVERIFY2(
                s.values.contains(QString::fromLatin1(key)),
                qPrintable(QStringLiteral("source %1 lacks %2").arg(it.key(), QLatin1String(key))));
        QVERIFY2(kinds.contains(s.values.value(QStringLiteral("kind")).toString()), id.constData());
        QVERIFY2(!s.values.value(QStringLiteral("title")).toString().isEmpty(), id.constData());
        QVERIFY2(!s.values.value(QStringLiteral("location")).toString().isEmpty(), id.constData());
        QVERIFY2(s.values.value(QStringLiteral("checked")).toDate().isValid(), id.constData());
    }
}

void TestModels::hasTheTenModels()
{
    QStringList ids;
    for (const Table &m : m_doc.models)
        ids << modelId(m);
    QCOMPARE(ids.size(), 10);
    QCOMPARE(QSet<QString>(ids.cbegin(), ids.cend()).size(), 10); // no duplicate id
    for (const QString &expected : kExpectedIds)
        QVERIFY2(ids.contains(expected), qPrintable(QStringLiteral("missing model ") + expected));
}

void TestModels::topLevelFields()
{
    const QSet<QString> families = {QStringLiteral("ipod"), QStringLiteral("mini"),
                                    QStringLiteral("nano"), QStringLiteral("classic")};
    const QRegularExpression idRe(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    QSet<QString> targets;

    forEachModel([&](const Table &m, const QString &id) {
        const QByteArray tag = id.toUtf8();
        QVERIFY2(idRe.match(id).hasMatch(), tag.constData());
        for (const char *key : {"family", "name", "bootloader_target"})
            QVERIFY2(!m.values.value(QString::fromLatin1(key)).toString().isEmpty(),
                     qPrintable(
                         QStringLiteral("%1: %2 is empty or missing").arg(id, QLatin1String(key))));
        QVERIFY2(families.contains(m.values.value(QStringLiteral("family")).toString()),
                 tag.constData());
        QVERIFY2(m.values.contains(QStringLiteral("to_verify")), tag.constData());
        QVERIFY2(!targets.contains(m.values.value(QStringLiteral("bootloader_target")).toString()),
                 qPrintable(id + ": bootloader_target is used by another model"));
        targets.insert(m.values.value(QStringLiteral("bootloader_target")).toString());
    });

    QCOMPARE(model(QStringLiteral("classic-6g")).values.value(QStringLiteral("family")).toString(),
             QStringLiteral("classic"));
    QCOMPARE(model(QStringLiteral("nano-2g")).values.value(QStringLiteral("family")).toString(),
             QStringLiteral("nano"));
}

void TestModels::usbIds()
{
    const QRegularExpression usbRe(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{4}$"));
    forEachModel([&](const Table &m, const QString &id) {
        const Table &usb = m.children.value(QStringLiteral("usb"));
        for (const char *key : {"normal", "disk_mode", "dfu"}) {
            const QString k = QString::fromLatin1(key);
            QVERIFY2(usb.values.contains(k),
                     qPrintable(QStringLiteral("%1: usb.%2 missing").arg(id, k)));
            const QVariantList list = usb.values.value(k).toList();
            for (const QVariant &v : list)
                QVERIFY2(usbRe.match(v.toString()).hasMatch(),
                         qPrintable(QStringLiteral("%1: bad usb id %2").arg(id, v.toString())));
        }
    });

    // The Classic is installed through DFU, so it must have a DFU id.
    const Table &classicUsb =
        model(QStringLiteral("classic-6g")).children.value(QStringLiteral("usb"));
    QVERIFY(!classicUsb.values.value(QStringLiteral("dfu")).toList().isEmpty());
}

void TestModels::screen()
{
    forEachModel([&](const Table &m, const QString &id) {
        const Table &s = m.children.value(QStringLiteral("screen"));
        const QByteArray tag = id.toUtf8();
        QVERIFY2(s.values.value(QStringLiteral("width")).toInt() > 0, tag.constData());
        QVERIFY2(s.values.value(QStringLiteral("height")).toInt() > 0, tag.constData());
        QVERIFY2(s.values.value(QStringLiteral("depth_bits")).toInt() > 0, tag.constData());
        QVERIFY2(s.values.value(QStringLiteral("colour")).typeId() == QMetaType::Bool,
                 tag.constData());
        if (s.values.contains(QStringLiteral("diagonal_inches")))
            QVERIFY2(s.values.value(QStringLiteral("diagonal_inches")).toDouble() > 0.0,
                     tag.constData());
    });
}

void TestModels::storage()
{
    const QSet<QString> kinds = {QStringLiteral("hdd"), QStringLiteral("flash")};
    const QSet<QString> forms = {QStringLiteral("1.8in"), QStringLiteral("1in"),
                                 QStringLiteral("chip")};
    const QSet<QString> interfaces = {QStringLiteral("ata"), QStringLiteral("nand")};

    forEachModel([&](const Table &m, const QString &id) {
        const Table &s = m.children.value(QStringLiteral("storage"));
        const QByteArray tag = id.toUtf8();
        QVERIFY2(kinds.contains(s.values.value(QStringLiteral("kind")).toString()),
                 tag.constData());
        QVERIFY2(forms.contains(s.values.value(QStringLiteral("form_factor")).toString()),
                 tag.constData());
        QVERIFY2(interfaces.contains(s.values.value(QStringLiteral("interface")).toString()),
                 tag.constData());

        const QVariantList caps = s.values.value(QStringLiteral("capacities_gb")).toList();
        QVERIFY2(!caps.isEmpty(), tag.constData());
        for (int i = 0; i < caps.size(); ++i) {
            QVERIFY2(caps[i].toInt() > 0, tag.constData());
            if (i > 0)
                QVERIFY2(caps[i].toInt() > caps[i - 1].toInt(),
                         qPrintable(id + ": capacities not ascending"));
        }

        // Cross-checks between keys: flash is a chip, a disk is not.
        const bool flash =
            s.values.value(QStringLiteral("kind")).toString() == QLatin1String("flash");
        const bool chip =
            s.values.value(QStringLiteral("form_factor")).toString() == QLatin1String("chip");
        QVERIFY2(flash == chip, tag.constData());
    });
}

void TestModels::install()
{
    forEachModel([&](const Table &m, const QString &id) {
        const Table &i = m.children.value(QStringLiteral("install"));
        const QString method = i.values.value(QStringLiteral("method")).toString();
        const QByteArray tag = id.toUtf8();
        QVERIFY2(method == QLatin1String("partition") || method == QLatin1String("dfu"),
                 tag.constData());
        QVERIFY2(i.values.value(QStringLiteral("dual_boot_required")).typeId() == QMetaType::Bool,
                 tag.constData());
    });

    // Rule: the Classic is never installed without dual boot.
    const Table &classic =
        model(QStringLiteral("classic-6g")).children.value(QStringLiteral("install"));
    QCOMPARE(classic.values.value(QStringLiteral("method")).toString(), QStringLiteral("dfu"));
    QVERIFY(classic.values.value(QStringLiteral("dual_boot_required")).toBool());
}

void TestModels::factSources()
{
    const Table &sources = m_doc.root.children.value(QStringLiteral("sources"));

    forEachModel([&](const Table &m, const QString &id) {
        const Table &fs = m.children.value(QStringLiteral("sources"));
        const QStringList toVerify = [&] {
            QStringList out;
            for (const QVariant &v : m.values.value(QStringLiteral("to_verify")).toList())
                out << v.toString();
            return out;
        }();

        for (const QString &fact : kFactNames) {
            QVERIFY2(fs.values.contains(fact),
                     qPrintable(QStringLiteral("%1: sources.%2 missing").arg(id, fact)));
            const QVariantList list = fs.values.value(fact).toList();
            QVERIFY2(!list.isEmpty(),
                     qPrintable(QStringLiteral("%1: sources.%2 is empty").arg(id, fact)));
            for (const QVariant &v : list)
                QVERIFY2(sources.children.contains(v.toString()),
                         qPrintable(QStringLiteral("%1: %2 points to undeclared source %3")
                                        .arg(id, fact, v.toString())));
            // A fact with fewer than two sources is not confirmed, so it must be listed.
            if (list.size() < 2)
                QVERIFY2(toVerify.contains(fact),
                         qPrintable(QStringLiteral("%1: %2 has one source and is not in to_verify")
                                        .arg(id, fact)));
        }
        // No unknown fact name hiding in the table.
        for (auto it = fs.values.cbegin(); it != fs.values.cend(); ++it)
            QVERIFY2(kFactNames.contains(it.key()),
                     qPrintable(QStringLiteral("%1: unknown fact %2").arg(id, it.key())));
    });
}

void TestModels::toVerifyEntries()
{
    forEachModel([&](const Table &m, const QString &id) {
        for (const QVariant &v : m.values.value(QStringLiteral("to_verify")).toList())
            QVERIFY2(
                kFactNames.contains(v.toString()),
                qPrintable(
                    QStringLiteral("%1: to_verify has unknown fact %2").arg(id, v.toString())));
    });
}

QTEST_GUILESS_MAIN(TestModels)
#include "tst_models.moc"
