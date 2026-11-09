// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "toml.h"

#include <QDate>
#include <QFile>
#include <QRegularExpression>

namespace tacetos::device {

namespace {

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

} // namespace

TomlDocument parseTomlText(const QByteArray &text)
{
    TomlDocument doc;
    TomlTable *current = &doc.root;
    int lineNo = 0;
    for (const QByteArray &rawLine : text.split('\n')) {
        ++lineNo;
        const QString line = stripComment(QString::fromUtf8(rawLine)).trimmed();
        if (line.isEmpty())
            continue;

        QString err;
        if (line.startsWith(QLatin1String("[["))) {
            if (line != QLatin1String("[[model]]")) {
                doc.error = QStringLiteral("line %1: unexpected %2").arg(lineNo).arg(line);
                return doc;
            }
            doc.models.append(TomlTable());
            current = &doc.models.last();
        } else if (line.startsWith(QLatin1Char('['))) {
            const QStringList path = line.mid(1, line.size() - 2).split(QLatin1Char('.'));
            TomlTable *base = nullptr;
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

TomlDocument parseTomlFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        TomlDocument doc;
        doc.error = QStringLiteral("cannot open ") + path;
        return doc;
    }
    return parseTomlText(file.readAll());
}

} // namespace tacetos::device
