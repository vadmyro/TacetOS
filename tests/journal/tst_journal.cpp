// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The journal (docs/specs/device-state.md sections 3 and 5.3). Every file is
// written in a temporary folder given by the test, never the data folder of
// the system.

#include "entry.h"
#include "journal.h"
#include "journalfile.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace tacetos::journal;

namespace {

const QString kHome = QStringLiteral("/home/anna");

QDateTime at(int ms = 481)
{
    return QDateTime(QDate(2026, 11, 10), QTime(14, 3, 22, ms), QTimeZone::UTC);
}

Entry makeEntry(const QString &text, const QString &port = QStringLiteral("1-2"))
{
    Entry entry;
    entry.time = at();
    entry.level = Level::Info;
    entry.domain = Domain::Usb;
    entry.port = port;
    entry.text = text;
    return entry;
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = file.readAll();
    if (all.isEmpty())
        return {};
    // A line end is \n and nothing else, on every system.
    return QString::fromUtf8(all).split(QLatin1Char('\n'));
}

QString logPath(const QTemporaryDir &dir, const QString &name = QStringLiteral("tacetos.log"))
{
    return dir.filePath(QStringLiteral("logs/") + name);
}

} // namespace

class TstJournal : public QObject
{
    Q_OBJECT

private slots:
    void formatWithPort();
    void formatWithoutPort();
    void formatCutsBackIntoFiveFields();
    void formatTimeIsUtc();
    void levelsAndDomains();

    void cleanLineBreaks();
    void cleanTooLong();
    void cleanTooLongKeepsSurrogatePair();
    void cleanHomeUnix();
    void cleanHomeWindows();
    void cleanHomeMac();
    void cleanOtherAccountUnchanged();
    void cleanUserAsComponent();
    void cleanHomeNeedsWholeComponent();
    void cleanNoHome();
    void cleanPort();

    void addCleansAndStamps();
    void bufferFull();
    void entryAddedOnce();

    void fileWritesLines();
    void fileAppendsAcrossStarts();
    void fileUtf8AndLineEnd();
    void rotationSteps();
    void rotationNeverSplitsAnEntry();
    void rotationAtStartWhenTooBig();
    void rotationKeepsOneFileOnly();

    void folderCannotBeCreated();
    void fileCannotBeOpened();
    void writeFailsInRotation();
    void noFileIsAllowed();
};

void TstJournal::formatWithPort()
{
    QCOMPARE(formatEntry(makeEntry(QStringLiteral("added 05ac:1261 normal"))),
             QStringLiteral("2026-11-10T14:03:22.481Z info usb 1-2 added 05ac:1261 normal"));
}

void TstJournal::formatWithoutPort()
{
    Entry entry = makeEntry(QStringLiteral("started"), QString());
    entry.level = Level::Warning;
    entry.domain = Domain::App;
    QCOMPARE(formatEntry(entry), QStringLiteral("2026-11-10T14:03:22.481Z warning app - started"));
}

void TstJournal::formatCutsBackIntoFiveFields()
{
    Entry entry = makeEntry(QStringLiteral("waiting > ready, with  spaces"));
    entry.level = Level::Error;
    entry.domain = Domain::Merge;
    const QString line = formatEntry(entry);

    // The first four spaces cut the line: the text keeps its own spaces.
    QStringList fields;
    int from = 0;
    for (int i = 0; i < 4; ++i) {
        const int space = line.indexOf(QLatin1Char(' '), from);
        QVERIFY(space > 0);
        fields << line.mid(from, space - from);
        from = space + 1;
    }
    fields << line.mid(from);

    QCOMPARE(fields.size(), 5);
    QCOMPARE(fields.at(0), QStringLiteral("2026-11-10T14:03:22.481Z"));
    QCOMPARE(fields.at(1), QStringLiteral("error"));
    QCOMPARE(fields.at(2), QStringLiteral("merge"));
    QCOMPARE(fields.at(3), QStringLiteral("1-2"));
    QCOMPARE(fields.at(4), QStringLiteral("waiting > ready, with  spaces"));
}

void TstJournal::formatTimeIsUtc()
{
    Entry entry = makeEntry(QStringLiteral("x"));
    // The same instant given as local time of another zone.
    entry.time = QDateTime(QDate(2026, 11, 10), QTime(15, 3, 22, 481),
                           QTimeZone::fromSecondsAheadOfUtc(3600));
    QVERIFY(formatEntry(entry).startsWith(QStringLiteral("2026-11-10T14:03:22.481Z ")));

    entry.time = at(5);
    QVERIFY(formatEntry(entry).startsWith(QStringLiteral("2026-11-10T14:03:22.005Z ")));
}

void TstJournal::levelsAndDomains()
{
    QCOMPARE(levelName(Level::Info), QLatin1String("info"));
    QCOMPARE(levelName(Level::Warning), QLatin1String("warning"));
    QCOMPARE(levelName(Level::Error), QLatin1String("error"));
    QCOMPARE(domainName(Domain::Usb), QLatin1String("usb"));
    QCOMPARE(domainName(Domain::Disk), QLatin1String("disk"));
    QCOMPARE(domainName(Domain::Merge), QLatin1String("merge"));
    QCOMPARE(domainName(Domain::Op), QLatin1String("op"));
    QCOMPARE(domainName(Domain::App), QLatin1String("app"));
}

void TstJournal::cleanLineBreaks()
{
    QCOMPARE(cleanText(QStringLiteral("a\nb"), kHome, 1000), QStringLiteral("a b"));
    QCOMPARE(cleanText(QStringLiteral("a\r\nb"), kHome, 1000), QStringLiteral("a  b"));
    QCOMPARE(cleanText(QStringLiteral("a\tb\x01"
                                      "c\x7f"
                                      "d"),
                       kHome, 1000),
             QStringLiteral("a b c d"));
    QCOMPARE(cleanText(QStringLiteral("a\u2028b\u2029c"), kHome, 1000), QStringLiteral("a b c"));
}

void TstJournal::cleanTooLong()
{
    const QString text(2500, QLatin1Char('x'));
    const QString cut = cleanText(text, kHome, 1000);
    QCOMPARE(cut.size(), 1000);
    QVERIFY(cut.endsWith(QStringLiteral("...")));
    QCOMPARE(cut.left(997), QString(997, QLatin1Char('x')));

    // Exactly at the limit: left alone.
    const QString exact(1000, QLatin1Char('y'));
    QCOMPARE(cleanText(exact, kHome, 1000), exact);
}

void TstJournal::cleanTooLongKeepsSurrogatePair()
{
    // The cut would fall between the two halves of the emoji.
    const QString text = QString(996, QLatin1Char('x')) + QStringLiteral("\U0001F600") +
                         QString(10, QLatin1Char('x'));
    const QString cut = cleanText(text, kHome, 1000);
    QVERIFY(cut.size() <= 1000);
    QVERIFY(cut.endsWith(QStringLiteral("...")));
    QVERIFY(!cut.at(cut.size() - 4).isHighSurrogate());
    QVERIFY(!cut.toUtf8().contains('?'));
}

void TstJournal::cleanHomeUnix()
{
    QCOMPARE(
        cleanText(QStringLiteral("reading /home/anna/x/y"), QStringLiteral("/home/anna"), 1000),
        QStringLiteral("reading ~/x/y"));
    QCOMPARE(cleanText(QStringLiteral("at /home/anna"), QStringLiteral("/home/anna"), 1000),
             QStringLiteral("at ~"));
    QCOMPARE(cleanText(QStringLiteral("from /home/anna/a to /home/anna/b"),
                       QStringLiteral("/home/anna"), 1000),
             QStringLiteral("from ~/a to ~/b"));
}

void TstJournal::cleanHomeWindows()
{
    const QString home = QStringLiteral("C:\\Users\\anna");
    QCOMPARE(cleanText(QStringLiteral("reading C:\\Users\\anna\\x"), home, 1000),
             QStringLiteral("reading ~\\x"));
    // QDir::homePath() gives forward slashes on Windows: both forms match.
    QCOMPARE(cleanText(QStringLiteral("reading C:\\Users\\anna\\x"),
                       QStringLiteral("C:/Users/anna"), 1000),
             QStringLiteral("reading ~\\x"));
    QCOMPARE(cleanText(QStringLiteral("reading C:/Users/anna/x"), home, 1000),
             QStringLiteral("reading ~/x"));
}

void TstJournal::cleanHomeMac()
{
    QCOMPARE(
        cleanText(QStringLiteral("reading /Users/anna/x"), QStringLiteral("/Users/anna"), 1000),
        QStringLiteral("reading ~/x"));
}

void TstJournal::cleanOtherAccountUnchanged()
{
    const QString text =
        QStringLiteral("reading /home/bob/x and /Users/bob/y and C:\\Users\\bob\\z");
    QCOMPARE(cleanText(text, kHome, 1000), text);
}

void TstJournal::cleanUserAsComponent()
{
    QCOMPARE(cleanText(QStringLiteral("mounted at /run/media/anna/IPOD"), kHome, 1000),
             QStringLiteral("mounted at /run/media/<user>/IPOD"));
    QCOMPARE(cleanText(QStringLiteral("mounted at /Volumes/anna"), kHome, 1000),
             QStringLiteral("mounted at /Volumes/<user>"));
    // A word that is not a path component stays.
    QCOMPARE(cleanText(QStringLiteral("anna plugged it in"), kHome, 1000),
             QStringLiteral("anna plugged it in"));
    QCOMPARE(cleanText(QStringLiteral("/run/media/annabel/IPOD"), kHome, 1000),
             QStringLiteral("/run/media/annabel/IPOD"));
}

void TstJournal::cleanHomeNeedsWholeComponent()
{
    // /home/annabel is another account, and not a path below /home/anna.
    QCOMPARE(cleanText(QStringLiteral("/home/annabel/x"), kHome, 1000),
             QStringLiteral("/home/annabel/x"));
    // /srv/home/anna is not the home folder, but the name is a path component.
    QCOMPARE(cleanText(QStringLiteral("/srv/home/anna/x"), kHome, 1000),
             QStringLiteral("/srv/home/<user>/x"));
}

void TstJournal::cleanNoHome()
{
    // A home that is only a root, or empty, would shorten every path: not used.
    QCOMPARE(cleanText(QStringLiteral("/usr/lib"), QStringLiteral("/"), 1000),
             QStringLiteral("/usr/lib"));
    QCOMPARE(cleanText(QStringLiteral("/usr/lib"), QString(), 1000), QStringLiteral("/usr/lib"));
}

void TstJournal::cleanPort()
{
    QCOMPARE(tacetos::journal::cleanPort(QStringLiteral("1-2.3")), QStringLiteral("1-2.3"));
    QCOMPARE(tacetos::journal::cleanPort(QStringLiteral("1 2\n3")), QStringLiteral("1_2_3"));
}

void TstJournal::addCleansAndStamps()
{
    Journal journal;
    journal.setHomeFolder(kHome);
    journal.setClock([] { return at(); });
    journal.add(Level::Warning, Domain::Op, QStringLiteral("1 2"),
                QStringLiteral("read /home/anna/x\nnext"));

    const QList<Entry> entries = journal.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).time, at());
    QCOMPARE(entries.at(0).level, Level::Warning);
    QCOMPARE(entries.at(0).domain, Domain::Op);
    QCOMPARE(entries.at(0).port, QStringLiteral("1_2"));
    QCOMPARE(entries.at(0).text, QStringLiteral("read ~/x next"));
}

void TstJournal::bufferFull()
{
    Journal journal;
    for (int i = 0; i < kJournalMaxEntries; ++i)
        journal.add(Level::Info, Domain::App, QString(), QString::number(i));
    QCOMPARE(journal.entries().size(), kJournalMaxEntries);
    QCOMPARE(journal.entries().first().text, QStringLiteral("0"));

    // The 1001st drops the first, the order is kept.
    journal.add(Level::Info, Domain::App, QString(), QStringLiteral("new"));
    const QList<Entry> entries = journal.entries();
    QCOMPARE(entries.size(), kJournalMaxEntries);
    QCOMPARE(entries.first().text, QStringLiteral("1"));
    QCOMPARE(entries.at(1).text, QStringLiteral("2"));
    QCOMPARE(entries.last().text, QStringLiteral("new"));
    QCOMPARE(entries.at(kJournalMaxEntries - 2).text, QString::number(kJournalMaxEntries - 1));
}

void TstJournal::entryAddedOnce()
{
    Journal journal;
    QSignalSpy spy(&journal, &Journal::entryAdded);
    for (int i = 0; i < kJournalMaxEntries + 5; ++i)
        journal.add(Level::Info, Domain::App, QString(), QString::number(i));
    // Once per entry, also once the ring is full.
    QCOMPARE(spy.count(), kJournalMaxEntries + 5);
    QCOMPARE(spy.at(0).at(0).value<Entry>().text, QStringLiteral("0"));
    QCOMPARE(spy.last().at(0).value<Entry>().text, QString::number(kJournalMaxEntries + 4));
}

void TstJournal::fileWritesLines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Journal journal;
    journal.setClock([] { return at(); });
    QVERIFY(journal.attachFile(dir.path()));
    QVERIFY(journal.fileActive());

    journal.add(Level::Info, Domain::Usb, QStringLiteral("1-2"),
                QStringLiteral("added 05ac:1261 normal"));
    journal.add(Level::Warning, Domain::Op, QStringLiteral("1-2"),
                QStringLiteral("unplugged during eject"));
    journal.add(Level::Info, Domain::App, QString(), QStringLiteral("stopped"));

    // Flushed at once: readable while the journal is still open.
    const QStringList lines = readLines(logPath(dir));
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(0),
             QStringLiteral("2026-11-10T14:03:22.481Z info usb 1-2 added 05ac:1261 normal"));
    QCOMPARE(lines.at(1),
             QStringLiteral("2026-11-10T14:03:22.481Z warning op 1-2 unplugged during eject"));
    QCOMPARE(lines.at(2), QStringLiteral("2026-11-10T14:03:22.481Z info app - stopped"));
    QCOMPARE(lines.at(3), QString());
}

void TstJournal::fileAppendsAcrossStarts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        JournalFile file(dir.path());
        QVERIFY(file.isOpen());
        QVERIFY(file.write(makeEntry(QStringLiteral("first start"))));
    }
    {
        JournalFile file(dir.path());
        QVERIFY(file.isOpen());
        QVERIFY(file.write(makeEntry(QStringLiteral("second start"))));
    }
    const QStringList lines = readLines(logPath(dir));
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).endsWith(QStringLiteral("first start")));
    QVERIFY(lines.at(1).endsWith(QStringLiteral("second start")));
}

void TstJournal::fileUtf8AndLineEnd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    JournalFile file(dir.path());
    QVERIFY(file.isOpen());
    QVERIFY(file.write(makeEntry(QStringLiteral("caf\u00e9"))));

    QFile raw(logPath(dir));
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QByteArray bytes = raw.readAll();
    QVERIFY(bytes.endsWith(QByteArray("caf\xc3\xa9\n")));
    QVERIFY(!bytes.contains('\r'));
}

// One line is `2026-11-10T14:03:22.481Z info usb 1-2 ` (38 bytes) plus the text
// plus `\n`. With a 10-character text, 49 bytes: three fit in 150 bytes.
static Entry lineOf49(int i)
{
    return makeEntry(QStringLiteral("entry %1   ").arg(i, 2, 10, QLatin1Char('0')).left(10));
}

void TstJournal::rotationSteps()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    JournalFile file(dir.path(), 150, 3);
    QVERIFY(file.isOpen());
    const qint64 lineSize = (formatEntry(lineOf49(0)) + QLatin1Char('\n')).toUtf8().size();
    QCOMPARE(lineSize, 49);

    // Three lines fill the file (147 of 150 bytes), the fourth turns it into .1.
    for (int i = 0; i < 3; ++i)
        QVERIFY(file.write(lineOf49(i)));
    QVERIFY(!QFileInfo::exists(logPath(dir, QStringLiteral("tacetos.1.log"))));
    QVERIFY(file.write(lineOf49(3)));
    QCOMPARE(readLines(logPath(dir, QStringLiteral("tacetos.1.log"))).size(), 4);
    QCOMPARE(readLines(logPath(dir)).size(), 2);
    QVERIFY(readLines(logPath(dir)).first().contains(QStringLiteral("entry 03")));

    // Fill the current one, rotate again: .1 goes to .2.
    for (int i = 4; i < 7; ++i)
        QVERIFY(file.write(lineOf49(i)));
    QVERIFY(readLines(logPath(dir, QStringLiteral("tacetos.2.log")))
                .first()
                .contains(QStringLiteral("entry 00")));
    QVERIFY(readLines(logPath(dir, QStringLiteral("tacetos.1.log")))
                .first()
                .contains(QStringLiteral("entry 03")));
    QVERIFY(readLines(logPath(dir)).first().contains(QStringLiteral("entry 06")));

    // Once more: the oldest is deleted, three files at most.
    for (int i = 7; i < 10; ++i)
        QVERIFY(file.write(lineOf49(i)));
    QVERIFY(!QFileInfo::exists(logPath(dir, QStringLiteral("tacetos.3.log"))));
    QVERIFY(readLines(logPath(dir, QStringLiteral("tacetos.2.log")))
                .first()
                .contains(QStringLiteral("entry 03")));
    QVERIFY(readLines(logPath(dir, QStringLiteral("tacetos.1.log")))
                .first()
                .contains(QStringLiteral("entry 06")));
    QVERIFY(readLines(logPath(dir)).first().contains(QStringLiteral("entry 09")));
    QCOMPARE(QDir(dir.filePath(QStringLiteral("logs"))).entryList(QDir::Files).size(), 3);
}

void TstJournal::rotationNeverSplitsAnEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    JournalFile file(dir.path(), 100, 3);
    QVERIFY(file.isOpen());
    for (int i = 0; i < 20; ++i)
        QVERIFY(file.write(makeEntry(QStringLiteral("number %1 of the twenty").arg(i))));

    // Every line of every file is whole, and no file is over the size.
    int total = 0;
    for (const QString &name : {QStringLiteral("tacetos.log"), QStringLiteral("tacetos.1.log"),
                                QStringLiteral("tacetos.2.log")}) {
        QVERIFY(QFileInfo(logPath(dir, name)).size() <= 100);
        const QStringList lines = readLines(logPath(dir, name));
        for (const QString &line : lines) {
            if (line.isEmpty())
                continue;
            ++total;
            QVERIFY2(
                line.startsWith(QStringLiteral("2026-11-10T14:03:22.481Z info usb 1-2 number ")),
                qPrintable(line));
            QVERIFY2(line.endsWith(QStringLiteral(" of the twenty")), qPrintable(line));
        }
    }
    QVERIFY(total >= 3);

    // An entry bigger than the size on its own is written whole, in a file of its own.
    QTemporaryDir other;
    JournalFile small(other.path(), 10, 3);
    QVERIFY(small.write(makeEntry(QStringLiteral("this one is longer than the whole size"))));
    QVERIFY(small.write(makeEntry(QStringLiteral("and so is this one, on its own"))));
    QVERIFY(readLines(logPath(other))
                .first()
                .endsWith(QStringLiteral("and so is this one, on its own")));
    QVERIFY(readLines(logPath(other, QStringLiteral("tacetos.1.log")))
                .first()
                .endsWith(QStringLiteral("this one is longer than the whole size")));
}

void TstJournal::rotationAtStartWhenTooBig()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        JournalFile file(dir.path(), 1000, 3);
        for (int i = 0; i < 20; ++i)
            QVERIFY(file.write(makeEntry(QStringLiteral("old %1").arg(i))));
    }
    const qint64 before = QFileInfo(logPath(dir)).size();
    QVERIFY(before > 0);

    // A start with a smaller size: the file is too big already, rotated at the first write.
    JournalFile file(dir.path(), before - 1, 3);
    QVERIFY(file.isOpen());
    QVERIFY(!QFileInfo::exists(logPath(dir, QStringLiteral("tacetos.1.log"))));
    QVERIFY(file.write(makeEntry(QStringLiteral("new"))));
    QCOMPARE(QFileInfo(logPath(dir, QStringLiteral("tacetos.1.log"))).size(), before);
    const QStringList lines = readLines(logPath(dir));
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.first().endsWith(QStringLiteral("new")));
}

void TstJournal::rotationKeepsOneFileOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    JournalFile file(dir.path(), 100, 1);
    QVERIFY(file.isOpen());
    for (int i = 0; i < 10; ++i)
        QVERIFY(file.write(makeEntry(QStringLiteral("number %1").arg(i))));
    QCOMPARE(QDir(dir.filePath(QStringLiteral("logs"))).entryList(QDir::Files).size(), 1);
    QVERIFY(readLines(logPath(dir))
                .at(readLines(logPath(dir)).size() - 2)
                .endsWith(QStringLiteral("number 9")));
}

void TstJournal::folderCannotBeCreated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A file where the folder `logs` should be.
    QFile blocker(dir.filePath(QStringLiteral("logs")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    Journal journal;
    QSignalSpy spy(&journal, &Journal::entryAdded);
    QVERIFY(!journal.attachFile(dir.path()));
    QVERIFY(!journal.fileActive());

    // One warning in the ring, and the journal goes on in memory.
    QCOMPARE(journal.entries().size(), 1);
    QCOMPARE(journal.entries().first().level, Level::Warning);
    QCOMPARE(journal.entries().first().domain, Domain::App);
    QVERIFY(journal.entries().first().text.contains(QStringLiteral("journal file off")));
    journal.add(Level::Info, Domain::App, QString(), QStringLiteral("still here"));
    QCOMPARE(journal.entries().size(), 2);
    QCOMPARE(journal.entries().last().text, QStringLiteral("still here"));
    QCOMPARE(spy.count(), 2);

    JournalFile file(dir.path());
    QVERIFY(!file.isOpen());
    QVERIFY(!file.errorString().isEmpty());
    QVERIFY(!file.write(makeEntry(QStringLiteral("x"))));
}

void TstJournal::fileCannotBeOpened()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A folder where the file should be.
    QVERIFY(QDir().mkpath(logPath(dir)));

    Journal journal;
    QVERIFY(!journal.attachFile(dir.path()));
    QVERIFY(!journal.fileActive());
    QCOMPARE(journal.entries().size(), 1);
    QCOMPARE(journal.entries().first().level, Level::Warning);
    journal.add(Level::Info, Domain::App, QString(), QStringLiteral("still here"));
    QCOMPARE(journal.entries().size(), 2);
}

void TstJournal::writeFailsInRotation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Journal journal;
    journal.setFileLimits(100, 3);
    QVERIFY(journal.attachFile(dir.path()));

    // The old file `.1` is a folder with something in it: it cannot be replaced.
    QVERIFY(QDir().mkpath(logPath(dir, QStringLiteral("tacetos.1.log/inside"))));
    QFile inside(logPath(dir, QStringLiteral("tacetos.1.log/inside/x")));
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.close();
    // The old file `.2` is one too, so that the removal fails on every system.
    QVERIFY(QDir().mkpath(logPath(dir, QStringLiteral("tacetos.2.log/inside"))));
    QFile inside2(logPath(dir, QStringLiteral("tacetos.2.log/inside/x")));
    QVERIFY(inside2.open(QIODevice::WriteOnly));
    inside2.close();

    for (int i = 0; i < 5; ++i)
        journal.add(Level::Info, Domain::App, QString(),
                    QStringLiteral("a line to fill the file %1").arg(i));

    QVERIFY(!journal.fileActive());
    // The five lines and one warning, the warning where the file failed.
    const QList<Entry> entries = journal.entries();
    QCOMPARE(entries.size(), 6);
    int warnings = 0;
    for (const Entry &entry : entries) {
        if (entry.level == Level::Warning) {
            ++warnings;
            QVERIFY(entry.text.contains(QStringLiteral("journal file off")));
        }
    }
    QCOMPARE(warnings, 1);

    // Nothing is lost in memory afterwards, and no second warning comes.
    journal.add(Level::Info, Domain::App, QString(), QStringLiteral("after"));
    QCOMPARE(journal.entries().size(), 7);
    QCOMPARE(journal.entries().last().text, QStringLiteral("after"));
}

void TstJournal::noFileIsAllowed()
{
    Journal journal;
    QVERIFY(!journal.fileActive());
    journal.add(Level::Error, Domain::Op, QStringLiteral("1-2"),
                QStringLiteral("eject failed: Busy"));
    QCOMPARE(journal.entries().size(), 1);
}

QTEST_GUILESS_MAIN(TstJournal)
#include "tst_journal.moc"
