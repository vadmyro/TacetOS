// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "report.h"

#include "journalconstants.h"

#include <QDir>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSaveFile>
#include <algorithm>
#include <limits>

namespace tacetos::journal {

namespace {

const QString kSeparators = QStringLiteral("[/\\\\]");

QString normalSlashes(const QString &path)
{
    QString out = path;
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (out.size() > 1 && out.endsWith(QLatin1Char('/')))
        out.chop(1);
    return out;
}

// True when `path` is the home folder or inside it, in the form of any system.
bool isUnder(const QString &path, const QString &home)
{
    const QString p = normalSlashes(path);
    const QString h = normalSlashes(home);
    if (h.isEmpty() || h == QLatin1String("/"))
        return false;
    return p.compare(h, Qt::CaseInsensitive) == 0 ||
           p.startsWith(h + QLatin1Char('/'), Qt::CaseInsensitive);
}

// A folder as a pattern where a separator of either kind matches.
QString folderPattern(const QString &folder)
{
    const QString normal = normalSlashes(folder);
    QStringList parts;
    const QStringList raw = normal.split(QLatin1Char('/'));
    for (int i = 0; i < raw.size(); ++i) {
        // The first part of an absolute folder is empty: it is the root.
        if (i == 0 || !raw.at(i).isEmpty())
            parts << QRegularExpression::escape(raw.at(i));
    }
    return parts.join(kSeparators);
}

QString headerLine(const QString &label, const QString &value)
{
    return label + QStringLiteral(": ") + (value.isEmpty() ? QStringLiteral("-") : value);
}

QString render(const QStringList &head, int headCount, const QStringList &logs, int omitted)
{
    QString out = QStringList(head.mid(0, headCount)).join(QLatin1Char('\n'));
    if (!logs.isEmpty())
        out += QStringLiteral("\n```\n") + logs.join(QLatin1Char('\n')) + QStringLiteral("\n```");
    if (omitted > 0)
        out += QStringLiteral("\n... (%1 more lines in the full report)").arg(omitted);
    return out;
}

} // namespace

QByteArray loadReportKey(const QString &dataFolder)
{
    const QString path = QDir(dataFolder).filePath(QString(kReportKeyFile));
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray key = file.readAll();
        if (key.size() == kReportKeyBytes)
            return key;
    }

    QByteArray key(kReportKeyBytes, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(key.data()),
                                          kReportKeyBytes / int(sizeof(quint32)));

    QDir().mkpath(dataFolder);
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(key);
        out.commit();
    }
    return key;
}

QString serialName(const QByteArray &key, const QString &serial)
{
    const QByteArray mac =
        QMessageAuthenticationCode::hash(serial.toUtf8(), key, QCryptographicHash::Sha256);
    return QStringLiteral("ipod-") + QString::fromLatin1(mac.toHex().left(8));
}

QString safeSerial(const QString &serial)
{
    QString safe = serial;
    for (QChar &c : safe) {
        const bool plain = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                           (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                           c == QLatin1Char('-') || c == QLatin1Char('_');
        if (!plain)
            c = QLatin1Char('_');
    }
    return safe;
}

ReportScrubber::ReportScrubber(const Rules &rules) : m_home(rules.home)
{
    struct Rule
    {
        int weight;
        QString pattern;
        QString replacement;
    };
    QList<Rule> list;

    auto addLiteral = [&list](const QString &text, const QString &replacement) {
        list.append({int(text.size()),
                     QStringLiteral("(?i:") + QRegularExpression::escape(text) + QLatin1Char(')'),
                     replacement});
    };

    for (const QString &serial : rules.serials) {
        if (serial.isEmpty())
            continue;
        const QString name = serialName(rules.key, serial);
        addLiteral(serial, name);
        const QString safe = safeSerial(serial);
        if (safe != serial)
            addLiteral(safe, name);
    }

    for (const QString &label : rules.volumeLabels) {
        // A label of one or two characters would wreck the text for nothing.
        if (label.size() >= 3)
            addLiteral(label, QStringLiteral("<volume>"));
    }

    if (!rules.hostName.isEmpty())
        addLiteral(rules.hostName, QStringLiteral("<host>"));

    // Under the home folder, the folder already becomes `~` below.
    if (!rules.backupFolder.isEmpty() && !isUnder(rules.backupFolder, rules.home)) {
        list.append({int(rules.backupFolder.size()),
                     QStringLiteral("(?i:") + folderPattern(rules.backupFolder) +
                         QStringLiteral(")(?![\\w-])"),
                     QStringLiteral("<backups>")});
    }

    // The longest first, so that a serial inside a longer name loses.
    std::stable_sort(list.begin(), list.end(),
                     [](const Rule &a, const Rule &b) { return a.weight > b.weight; });

    QStringList alternatives;
    for (const Rule &rule : list) {
        alternatives << QLatin1Char('(') + rule.pattern + QLatin1Char(')');
        m_replacements << rule.replacement;
    }
    if (!alternatives.isEmpty())
        m_re = QRegularExpression(alternatives.join(QLatin1Char('|')));
}

QString ReportScrubber::apply(const QString &text) const
{
    QString out;
    if (m_replacements.isEmpty()) {
        out = text;
    } else {
        int last = 0;
        QRegularExpressionMatchIterator it = m_re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (match.capturedLength() == 0)
                continue;
            out += text.mid(last, match.capturedStart() - last);
            for (int i = 0; i < m_replacements.size(); ++i) {
                if (match.capturedStart(i + 1) >= 0) {
                    out += m_replacements.at(i);
                    break;
                }
            }
            last = match.capturedEnd();
        }
        out += text.mid(last);
    }
    // The home folder and the account name, done again: some texts do not
    // come from the journal. No cut: a line is cut by what builds it.
    return cleanText(out, m_home, std::numeric_limits<int>::max());
}

Report buildReport(const ReportInput &input)
{
    ReportScrubber::Rules rules;
    rules.key = loadReportKey(input.dataFolder);
    rules.serials = input.serials;
    if (!input.serial.isEmpty())
        rules.serials.append(input.serial);
    rules.volumeLabels = input.volumeLabels;
    rules.backupFolder = input.backupFolder;
    rules.hostName = input.hostName;
    rules.home = input.home;
    const ReportScrubber scrubber(rules);

    // Section 12.1, one line per item.
    QStringList head;
    head << QStringLiteral("TacetOS report");
    head << scrubber.apply(headerLine(QStringLiteral("App"), input.appVersion));
    head << scrubber.apply(headerLine(QStringLiteral("System"), input.system));
    head << scrubber.apply(headerLine(QStringLiteral("Disk service"), input.helperVersion));
    QString ipod = input.model.isEmpty() ? QStringLiteral("-") : input.model;
    if (!input.family.isEmpty())
        ipod += QStringLiteral(" (") + input.family + QLatin1Char(')');
    head << scrubber.apply(QStringLiteral("iPod: ") + ipod);
    head << scrubber.apply(headerLine(QStringLiteral("Firmware"), input.firmwareLine));
    // Made by serialName, nothing in it to take out.
    head << headerLine(QStringLiteral("iPod name"),
                       input.serial.isEmpty() ? QString() : serialName(rules.key, input.serial));
    head << scrubber.apply(headerLine(QStringLiteral("Flow"), input.flow));
    head << scrubber.apply(headerLine(QStringLiteral("Path"), input.path));
    head << scrubber.apply(headerLine(QStringLiteral("Step"), input.step));
    if (input.failures.isEmpty()) {
        head << QStringLiteral("Failures: none");
    } else {
        for (const ReportFailure &failure : input.failures) {
            QString line = QStringLiteral("Failure: ") + failure.code;
            if (!failure.detail.isEmpty())
                line += QStringLiteral(": ") + failure.detail;
            head << scrubber.apply(line);
        }
    }

    // The entries of the last flow, and the ones before it.
    QList<Entry> flow;
    QList<Entry> before;
    for (const Entry &entry : input.entries) {
        if (input.flowStart.isValid() && entry.time < input.flowStart)
            before.append(entry);
        else
            flow.append(entry);
    }
    if (before.size() > kReportContext)
        before = before.mid(before.size() - kReportContext);

    auto lineOf = [&scrubber](const Entry &entry) { return scrubber.apply(formatEntry(entry)); };

    Report report;

    // The full report.
    QStringList full = head;
    full << QString();
    full << QStringLiteral("Journal: %1 entries before the last flow, %2 in it")
                .arg(before.size())
                .arg(flow.size());
    for (const Entry &entry : before)
        full << lineOf(entry);
    if (input.flowStart.isValid())
        full << QStringLiteral("# The last flow starts here");
    for (const Entry &entry : flow)
        full << lineOf(entry);
    report.full = full.join(QLatin1Char('\n')) + QLatin1Char('\n');

    // The summary. Its lines are the head, then the entries of the last flow:
    // `warning` and `error` first, then the newest, until there is no room.
    QStringList flowLines;
    for (const Entry &entry : flow)
        flowLines << lineOf(entry);
    const int totalLines = int(head.size() + before.size() + flowLines.size());

    // Everything fits: no trailer, and no entry of the context to leave out.
    if (before.isEmpty()) {
        const QString all = render(head, int(head.size()), flowLines, 0);
        if (all.size() <= kReportSummaryMax) {
            report.summary = all;
            return report;
        }
    }

    // The trailer is counted with the largest N it can have.
    const int reserve = render(QStringList(), 0, QStringList(), totalLines).size();
    const int room = kReportSummaryMax - reserve;

    int headCount = 0;
    while (headCount < head.size() && render(head, headCount + 1, QStringList(), 0).size() <= room)
        ++headCount;

    QList<int> chosen;
    if (headCount == head.size()) {
        QList<int> order;
        for (int i = int(flow.size()) - 1; i >= 0; --i) {
            if (flow.at(i).level != Level::Info)
                order.append(i);
        }
        for (int i = int(flow.size()) - 1; i >= 0; --i) {
            if (flow.at(i).level == Level::Info)
                order.append(i);
        }
        for (int index : order) {
            QList<int> trial = chosen;
            trial.append(index);
            std::sort(trial.begin(), trial.end());
            QStringList lines;
            for (int i : trial)
                lines << flowLines.at(i);
            if (render(head, headCount, lines, 0).size() > room)
                break;
            chosen = trial;
        }
    }

    QStringList shown;
    for (int i : chosen)
        shown << flowLines.at(i);
    const int omitted = totalLines - headCount - int(shown.size());
    report.summary = render(head, headCount, shown, omitted);
    return report;
}

} // namespace tacetos::journal
