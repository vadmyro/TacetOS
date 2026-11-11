// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "entry.h"

#include "journalconstants.h"

#include <QRegularExpression>
#include <QStringList>

namespace tacetos::journal {

namespace {

bool isLineBreaker(QChar c)
{
    return c.category() == QChar::Other_Control || c == QChar(0x2028) || c == QChar(0x2029);
}

// The home folder as a pattern where the separators of the two kinds match.
QString homePattern(const QString &home)
{
    QStringList parts;
    const QStringList raw = home.split(QRegularExpression(QStringLiteral("[/\\\\]")));
    for (int i = 0; i < raw.size(); ++i) {
        // The first part of a folder like /home/anna is empty: keep it, it is the root.
        if (i == 0 || !raw.at(i).isEmpty())
            parts << QRegularExpression::escape(raw.at(i));
    }
    return parts.join(QStringLiteral("[/\\\\]"));
}

QString lastComponent(const QString &home)
{
    const QStringList raw = home.split(QRegularExpression(QStringLiteral("[/\\\\]")));
    for (int i = raw.size() - 1; i >= 0; --i) {
        if (!raw.at(i).isEmpty())
            return raw.at(i);
    }
    return {};
}

} // namespace

QLatin1String levelName(Level level)
{
    switch (level) {
    case Level::Info:
        return QLatin1String("info");
    case Level::Warning:
        return QLatin1String("warning");
    case Level::Error:
        return QLatin1String("error");
    }
    return QLatin1String("info");
}

QLatin1String domainName(Domain domain)
{
    switch (domain) {
    case Domain::Usb:
        return QLatin1String("usb");
    case Domain::Disk:
        return QLatin1String("disk");
    case Domain::Merge:
        return QLatin1String("merge");
    case Domain::Op:
        return QLatin1String("op");
    case Domain::App:
        return QLatin1String("app");
    }
    return QLatin1String("app");
}

QString cleanText(const QString &text, const QString &home, int maxLength)
{
    QString out = text;
    for (QChar &c : out) {
        if (isLineBreaker(c))
            c = QLatin1Char(' ');
    }

    // A home folder that is only a root ("/") would shorten every path.
    const QString user = lastComponent(home);
    if (!user.isEmpty()) {
        const QRegularExpression homeRe(QStringLiteral("(?<![\\w.~-])") + homePattern(home) +
                                        QStringLiteral("(?![\\w-])"));
        out.replace(homeRe, QStringLiteral("~"));

        const QRegularExpression userRe(QStringLiteral("(?<=[/\\\\])") +
                                        QRegularExpression::escape(user) +
                                        QStringLiteral("(?![\\w.-])"));
        out.replace(userRe, QStringLiteral("<user>"));
    }

    if (maxLength >= 3 && out.size() > maxLength) {
        int keep = maxLength - 3;
        // Never cut a surrogate pair in two.
        if (keep > 0 && out.at(keep - 1).isHighSurrogate())
            --keep;
        out.truncate(keep);
        out += QStringLiteral("...");
    }
    return out;
}

QString cleanPort(const QString &port)
{
    QString out = port;
    for (QChar &c : out) {
        if (c.isSpace() || isLineBreaker(c))
            c = QLatin1Char('_');
    }
    return out;
}

QString formatEntry(const Entry &entry)
{
    const QString port = entry.port.isEmpty() ? QStringLiteral("-") : entry.port;
    return entry.time.toUTC().toString(kJournalTimeFormat) + QLatin1Char(' ') +
           levelName(entry.level) + QLatin1Char(' ') + domainName(entry.domain) + QLatin1Char(' ') +
           port + QLatin1Char(' ') + entry.text;
}

} // namespace tacetos::journal
