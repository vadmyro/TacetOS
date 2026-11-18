// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The anonymised report (docs/specs/install-wizard.md sections 12 and 15.5) on
// journals made here. Nothing is read from the machine: the home folder, the
// host and the labels are given, and the data folder is a temporary one.

#include "journalconstants.h"
#include "report.h"

#include <QDir>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace tacetos::journal;

namespace {

const QString kHome = QStringLiteral("/home/anna");
const QString kSerial = QStringLiteral("000A27001234ABCD");
const QString kOtherSerial = QStringLiteral("000A27009999EEEE");
const QString kOdd = QStringLiteral("A1:B2.C3");

QDateTime at(int second)
{
    return QDateTime(QDate(2026, 11, 10), QTime(14, 3, 0).addSecs(second), QTimeZone::UTC);
}

Entry makeEntry(int second, const QString &text, Level level = Level::Info)
{
    Entry entry;
    entry.time = at(second);
    entry.level = level;
    entry.domain = Domain::Op;
    entry.port = QStringLiteral("1-2");
    entry.text = text;
    return entry;
}

class TestJournalReport : public QObject
{
    Q_OBJECT

private:
    ReportInput base() const
    {
        ReportInput input;
        input.appVersion = QStringLiteral("0.1.0");
        input.system = QStringLiteral("Fedora Linux 44");
        input.helperVersion = QStringLiteral("0.1.0");
        input.model = QStringLiteral("iPod Classic 6G");
        input.family = QStringLiteral("classic");
        input.firmwareLine = QStringLiteral("Apple");
        input.serial = kSerial;
        input.flow = QStringLiteral("install");
        input.path = QStringLiteral("clean");
        input.step = QStringLiteral("backup-copy");
        input.home = kHome;
        input.dataFolder = m_data.path();
        return input;
    }

    QTemporaryDir m_data;

private slots:
    void initTestCase() { QVERIFY(m_data.isValid()); }

    void serialBecomesStableName()
    {
        ReportInput input = base();
        input.entries << makeEntry(0,
                                   QStringLiteral("added ") + kSerial + QStringLiteral(" normal"))
                      << makeEntry(1, QStringLiteral("again ") + kSerial.toLower());
        const Report report = buildReport(input);

        const QByteArray key = loadReportKey(m_data.path());
        QCOMPARE(key.size(), kReportKeyBytes);
        const QByteArray mac =
            QMessageAuthenticationCode::hash(kSerial.toUtf8(), key, QCryptographicHash::Sha256);
        const QString name = QStringLiteral("ipod-") + QString::fromLatin1(mac.toHex().left(8));
        QCOMPARE(serialName(key, kSerial), name);
        QVERIFY(QRegularExpression(QStringLiteral("^ipod-[0-9a-f]{8}$")).match(name).hasMatch());

        QVERIFY(!report.full.contains(kSerial, Qt::CaseInsensitive));
        QVERIFY(!report.summary.contains(kSerial, Qt::CaseInsensitive));
        QVERIFY(report.full.contains(QStringLiteral("added ") + name + QStringLiteral(" normal")));
        QVERIFY(report.full.contains(QStringLiteral("again ") + name));
        QVERIFY(report.summary.contains(QStringLiteral("iPod name: ") + name));
    }

    void sameSerialSameNameTwoSerialsTwoNames()
    {
        const QByteArray key = loadReportKey(m_data.path());
        QCOMPARE(serialName(key, kSerial), serialName(key, kSerial));
        QCOMPARE(serialName(loadReportKey(m_data.path()), kSerial), serialName(key, kSerial));
        QVERIFY(serialName(key, kSerial) != serialName(key, kOtherSerial));

        ReportInput input = base();
        const Report first = buildReport(input);
        const Report second = buildReport(input);
        QCOMPARE(first.full, second.full);
        QCOMPARE(first.summary, second.summary);

        // Another computer has another key, so another name.
        QTemporaryDir other;
        QVERIFY(other.isValid());
        QVERIFY(serialName(loadReportKey(other.path()), kSerial) != serialName(key, kSerial));
    }

    void keyFileMadeOnceAndKept()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QString(kReportKeyFile));
        QVERIFY(!QFile::exists(path));
        const QByteArray key = loadReportKey(dir.path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), key);
        QCOMPARE(loadReportKey(dir.path()), key);
    }

    void serialInTheFileNameForm()
    {
        ReportInput input = base();
        input.serial = kOdd;
        input.entries << makeEntry(0,
                                   QStringLiteral(
                                       "copy to ~/Documents/TacetOS Backups/A1_B2_C3/2026-11-10"))
                      << makeEntry(1, QStringLiteral("serial %1").arg(kOdd));
        const Report report = buildReport(input);
        const QString name = serialName(loadReportKey(m_data.path()), kOdd);
        QVERIFY(!report.full.contains(QStringLiteral("A1_B2_C3")));
        QVERIFY(!report.full.contains(kOdd));
        QVERIFY(report.full.contains(QStringLiteral("TacetOS Backups/") + name +
                                     QStringLiteral("/2026")));
    }

    void otherSerialsOfTheSession()
    {
        ReportInput input = base();
        input.serials << kOtherSerial;
        input.entries << makeEntry(0, QStringLiteral("another iPod ") + kOtherSerial);
        const Report report = buildReport(input);
        QVERIFY(!report.full.contains(kOtherSerial));
        QVERIFY(report.full.contains(serialName(loadReportKey(m_data.path()), kOtherSerial)));
    }

    void homeAndAccountName()
    {
        ReportInput input = base();
        input.entries << makeEntry(0, QStringLiteral("read /home/anna/.local/share/TacetOS/x"))
                      << makeEntry(1, QStringLiteral("mounted at /run/media/anna/IPOD"));
        input.failures << ReportFailure{QStringLiteral("backup-full-disk"),
                                        QStringLiteral("C:\\Users\\anna\\Documents full")};
        input.home = QStringLiteral("C:\\Users\\anna");
        const Report report = buildReport(input);
        QVERIFY(!report.full.contains(QStringLiteral("anna")));
        QVERIFY(!report.summary.contains(QStringLiteral("anna")));
        QVERIFY(report.full.contains(QStringLiteral("mounted at /run/media/<user>/IPOD")));
        QVERIFY(
            report.full.contains(QStringLiteral("Failure: backup-full-disk: ~\\Documents full")));

        input.home = kHome;
        input.failures.clear();
        const Report posix = buildReport(input);
        QVERIFY(posix.full.contains(QStringLiteral("read ~/.local/share/TacetOS/x")));
    }

    void volumeLabels()
    {
        ReportInput input = base();
        input.volumeLabels << QStringLiteral("Anna's Pod") << QStringLiteral("SECRET");
        input.entries << makeEntry(0, QStringLiteral("volume Anna's Pod mounted"))
                      << makeEntry(1, QStringLiteral("path /run/media/anna/secret/x"));
        const Report report = buildReport(input);
        QVERIFY(!report.full.contains(QStringLiteral("Anna's Pod")));
        QVERIFY(!report.full.contains(QStringLiteral("secret"), Qt::CaseInsensitive));
        QVERIFY(report.full.contains(QStringLiteral("volume <volume> mounted")));
        QVERIFY(report.full.contains(QStringLiteral("/run/media/<user>/<volume>/x")));
    }

    void backupFolder()
    {
        ReportInput input = base();
        input.backupFolder = QStringLiteral("/mnt/disk2/My Backups");
        input.entries << makeEntry(
                             0, QStringLiteral("backup in /mnt/disk2/My Backups/ipod/2026-11-10"))
                      << makeEntry(1, QStringLiteral("windows form \\mnt\\disk2\\My Backups\\x"));
        Report report = buildReport(input);
        QVERIFY(!report.full.contains(QStringLiteral("My Backups")));
        QVERIFY(report.full.contains(QStringLiteral("backup in <backups>/ipod/2026-11-10")));
        QVERIFY(report.full.contains(QStringLiteral("windows form <backups>\\x")));

        // Under the home folder, it is the home folder rule that applies.
        input.backupFolder = QStringLiteral("/home/anna/Documents/TacetOS Backups");
        input.entries.clear();
        input.entries << makeEntry(
            0, QStringLiteral("backup in /home/anna/Documents/TacetOS Backups/x"));
        report = buildReport(input);
        QVERIFY(report.full.contains(QStringLiteral("backup in ~/Documents/TacetOS Backups/x")));
        QVERIFY(!report.full.contains(QStringLiteral("<backups>")));
    }

    void hostName()
    {
        ReportInput input = base();
        input.hostName = QStringLiteral("annas-laptop");
        input.system = QStringLiteral("Fedora Linux 44 on ANNAS-LAPTOP");
        input.entries << makeEntry(0, QStringLiteral("hello from annas-laptop"));
        const Report report = buildReport(input);
        QVERIFY(!report.full.contains(QStringLiteral("laptop"), Qt::CaseInsensitive));
        QVERIFY(report.full.contains(QStringLiteral("hello from <host>")));
        QVERIFY(report.full.contains(QStringLiteral("System: Fedora Linux 44 on <host>")));
    }

    void longestFirstAndNoSecondPass()
    {
        // A label that is part of the name of the serial or of the host does
        // not eat what the other rule put in.
        ReportInput input = base();
        input.volumeLabels << QStringLiteral("ipod");
        input.entries << makeEntry(0, QStringLiteral("x ") + kSerial + QStringLiteral(" y"));
        const Report report = buildReport(input);
        const QString name = serialName(loadReportKey(m_data.path()), kSerial);
        QVERIFY(report.full.contains(QStringLiteral("x ") + name + QStringLiteral(" y")));
    }

    void headerLines()
    {
        ReportInput input = base();
        input.failures << ReportFailure{QStringLiteral("backup-verify-failed"),
                                        QStringLiteral("3 files")}
                       << ReportFailure{QStringLiteral("ipod-gone"), QString()};
        const Report report = buildReport(input);
        const QStringList lines = report.summary.split(QLatin1Char('\n'));
        QCOMPARE(lines.at(0), QStringLiteral("TacetOS report"));
        QVERIFY(lines.contains(QStringLiteral("App: 0.1.0")));
        QVERIFY(lines.contains(QStringLiteral("System: Fedora Linux 44")));
        QVERIFY(lines.contains(QStringLiteral("Disk service: 0.1.0")));
        QVERIFY(lines.contains(QStringLiteral("iPod: iPod Classic 6G (classic)")));
        QVERIFY(lines.contains(QStringLiteral("Firmware: Apple")));
        QVERIFY(lines.contains(QStringLiteral("Flow: install")));
        QVERIFY(lines.contains(QStringLiteral("Path: clean")));
        QVERIFY(lines.contains(QStringLiteral("Step: backup-copy")));
        QVERIFY(lines.contains(QStringLiteral("Failure: backup-verify-failed: 3 files")));
        QVERIFY(lines.contains(QStringLiteral("Failure: ipod-gone")));
        QVERIFY(!lines.contains(QStringLiteral("Failures: none")));
    }

    void noFailureNoEntryFitsWithoutTrailer()
    {
        ReportInput input = base();
        input.entries << makeEntry(0, QStringLiteral("one")) << makeEntry(1, QStringLiteral("two"));
        const Report report = buildReport(input);
        QVERIFY(report.summary.contains(QStringLiteral("Failures: none")));
        QVERIFY(report.summary.contains(QStringLiteral("```\n")));
        QVERIFY(report.summary.endsWith(QStringLiteral("two\n```")));
        QVERIFY(!report.summary.contains(QStringLiteral("more lines")));
    }

    void flowAndContext()
    {
        ReportInput input = base();
        for (int i = 0; i < 250; ++i)
            input.entries << makeEntry(i, QStringLiteral("before %1").arg(i));
        input.flowStart = at(250);
        for (int i = 250; i < 255; ++i)
            input.entries << makeEntry(i, QStringLiteral("flow %1").arg(i));
        const Report report = buildReport(input);

        QCOMPARE(report.full.count(QStringLiteral(" 1-2 before ")), kReportContext);
        QVERIFY(!report.full.contains(QStringLiteral(" before 49\n")));
        QVERIFY(report.full.contains(QStringLiteral(" before 50\n")));
        QVERIFY(report.full.contains(QStringLiteral(" before 249\n")));
        QCOMPARE(report.full.count(QStringLiteral(" 1-2 flow ")), 5);
        QVERIFY(report.full.indexOf(QStringLiteral(" before 249")) <
                report.full.indexOf(QStringLiteral(" flow 250")));

        // The summary has the flow only, and says how many lines are left.
        QVERIFY(report.summary.contains(QStringLiteral(" flow 254")));
        QVERIFY(!report.summary.contains(QStringLiteral(" before ")));
        QVERIFY(report.summary.size() <= kReportSummaryMax);
        QVERIFY(report.summary.endsWith(
            QStringLiteral("... (%1 more lines in the full report)").arg(kReportContext)));
    }

    void warningsFirstThenNewest()
    {
        ReportInput input = base();
        input.entries << makeEntry(0, QStringLiteral("old warning"), Level::Warning);
        for (int i = 1; i < 200; ++i)
            input.entries << makeEntry(
                i, QStringLiteral("filler entry number %1 with some text").arg(i));
        input.entries << makeEntry(300, QStringLiteral("late error"), Level::Error);
        const Report report = buildReport(input);
        QVERIFY(report.summary.size() <= kReportSummaryMax);
        QVERIFY(report.summary.contains(QStringLiteral("old warning")));
        QVERIFY(report.summary.contains(QStringLiteral("late error")));
        QVERIFY(report.summary.contains(QStringLiteral("filler entry number 199 ")));
        // In the order of the journal.
        QVERIFY(report.summary.indexOf(QStringLiteral("old warning")) <
                report.summary.indexOf(QStringLiteral("late error")));
    }

    void cutAtTheLimit_data()
    {
        QTest::addColumn<int>("textLength");
        for (int length = 1; length <= 400; length += 7)
            QTest::addRow("%d", length) << length;
        QTest::addRow("999") << 999;
    }

    void cutAtTheLimit()
    {
        QFETCH(int, textLength);
        ReportInput input = base();
        const int count = 60;
        for (int i = 0; i < count; ++i) {
            const QString number = QString::number(i).rightJustified(3, QLatin1Char('0'));
            input.entries << makeEntry(i, number + QString(textLength, QLatin1Char('x')));
        }
        const Report report = buildReport(input);
        const QString &summary = report.summary;

        QVERIFY(summary.size() <= kReportSummaryMax);
        QVERIFY(summary.startsWith(QStringLiteral("TacetOS report\n")));

        // Fences come in a pair, and the code block holds whole lines of the
        // full report only.
        QCOMPARE(summary.count(QStringLiteral("```")) % 2, 0);
        const int open = summary.indexOf(QStringLiteral("\n```\n"));
        const int close = summary.indexOf(QStringLiteral("\n```"), open + 4);
        QVERIFY(open >= 0 && close > open);
        const QStringList shown = summary.mid(open + 5, close - open - 5).split(QLatin1Char('\n'));
        QVERIFY(!shown.isEmpty());
        const QStringList fullLines = report.full.split(QLatin1Char('\n'));
        for (const QString &line : shown)
            QVERIFY2(fullLines.contains(line), qPrintable(line.left(40)));

        // The newest is kept, and a whole line is never half there.
        QVERIFY(shown.last().contains(QStringLiteral("059")));
        QVERIFY(shown.last().endsWith(QLatin1Char('x')));

        const int head = open == -1 ? 0 : summary.left(open).count(QLatin1Char('\n')) + 1;
        const int omitted = count - int(shown.size());
        if (omitted > 0) {
            QVERIFY2(summary.endsWith(
                         QStringLiteral("... (%1 more lines in the full report)").arg(omitted)),
                     qPrintable(QStringLiteral("omitted %1 head %2 shown %3 tail %4")
                                    .arg(omitted)
                                    .arg(head)
                                    .arg(shown.size())
                                    .arg(summary.right(50))));
        } else {
            QVERIFY(!summary.contains(QStringLiteral("more lines")));
        }
        // One more entry would not have fitted: at most the digits of the
        // count of the trailer are left over.
        if (omitted > 0) {
            const QString next = fullLines.at(fullLines.size() - 2 - int(shown.size()));
            QVERIFY(summary.size() + next.size() + 1 > kReportSummaryMax - 2);
        }
    }

    void nothingFromThisMachine()
    {
        ReportInput input = base();
        input.entries << makeEntry(0, QStringLiteral("started"));
        const Report report = buildReport(input);
        const QString home = QDir::homePath();
        if (home.size() > 3)
            QVERIFY(!report.full.contains(home));
        QVERIFY(report.full.contains(QStringLiteral("System: Fedora Linux 44")));
    }
};

} // namespace

QTEST_GUILESS_MAIN(TestJournalReport)
#include "tst_journal_report.moc"
