// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The report page (docs/specs/install-screens.md sections 10 and 14.2) on journals made here.
// The clipboard, the save dialog and the browser are made by the test; the data folder and the
// documents folder are temporary folders.

#include "journalconstants.h"
#include "report.h"
#include "wizard/reportpage.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

using namespace tacetos;
using namespace tacetos::journal;

namespace {

const QString kHome = QStringLiteral("/home/anna");
const QString kSerial = QStringLiteral("000A27001234ABCD");
const QString kLabel = QStringLiteral("ANNA MUSIC");

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

class TestReportPage : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir data_;
    QTemporaryDir documents_;

    ReportInput input() const
    {
        ReportInput in;
        in.appVersion = QStringLiteral("0.2.0");
        in.system = QStringLiteral("Fedora Linux 44");
        in.helperVersion = QStringLiteral("0.2.0");
        in.model = QStringLiteral("iPod Classic 6G");
        in.family = QStringLiteral("classic");
        in.firmwareLine = QStringLiteral("apple");
        in.serial = kSerial;
        in.flow = QStringLiteral("install");
        in.path = QStringLiteral("clean");
        in.step = QStringLiteral("firmware");
        in.failures.append({QStringLiteral("firmware-write-failed"), QStringLiteral("io error")});
        in.entries << makeEntry(0, QStringLiteral("old line before the flow"))
                   << makeEntry(10, QStringLiteral("start serial ") + kSerial)
                   << makeEntry(11, QStringLiteral("volume ") + kLabel + QStringLiteral(" mounted"))
                   << makeEntry(12, QStringLiteral("saved to ") + kHome + QStringLiteral("/Music"),
                                Level::Warning)
                   << makeEntry(13, QStringLiteral("write failed"), Level::Error);
        in.flowStart = at(10);
        in.volumeLabels = {kLabel};
        in.hostName = QStringLiteral("annas-laptop");
        in.home = kHome;
        in.dataFolder = data_.path();
        return in;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(data_.isValid());
        QVERIFY(documents_.isValid());
    }

    void previewIsTheAnonymisedSummary()
    {
        ReportPage page;
        page.setInput(input());
        const Report expected = buildReport(input());
        QCOMPARE(page.summaryText(), expected.summary);
        QCOMPARE(page.shownText(), expected.summary);
        QCOMPARE(page.field()->toPlainText(), expected.summary);
        QVERIFY(page.field()->isReadOnly());
        QVERIFY(!page.summaryText().contains(kSerial));
        QVERIFY(!page.summaryText().contains(kLabel));
        QVERIFY(!page.summaryText().contains(QStringLiteral("anna")));
    }

    void countsTheCharacters()
    {
        ReportPage page;
        page.setInput(input());
        const QString expected = QStringLiteral("%1 of %2 characters")
                                     .arg(QLocale().toString(qsizetype(page.summaryText().size())),
                                          QLocale().toString(kReportSummaryMax));
        QCOMPARE(page.countText(), expected);
        QVERIFY(page.summaryText().size() <= kReportSummaryMax);
    }

    void copyPutsTheSummaryInTheClipboard()
    {
        ReportPage page;
        page.setInput(input());
        QString copied;
        page.setClipboardWriter([&copied](const QString &text) { copied = text; });
        QVERIFY(page.noteText().isEmpty());
        page.copyButton()->click();
        QCOMPARE(copied, page.field()->toPlainText());
        QCOMPARE(copied, buildReport(input()).summary);
        QVERIFY(page.noteText().startsWith(QStringLiteral("Copied.")));
    }

    void copyKeepsTheSummaryWhileTheJournalIsShown()
    {
        ReportPage page;
        page.setInput(input());
        QString copied;
        page.setClipboardWriter([&copied](const QString &text) { copied = text; });
        page.journalButton()->click();
        page.copyButton()->click();
        QCOMPARE(copied, page.summaryText());
    }

    void realClipboard()
    {
        ReportPage page;
        page.setInput(input());
        page.copyButton()->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), page.summaryText());
    }

    void journalViewIsMaskedLikeTheReport()
    {
        ReportPage page;
        page.setInput(input());
        QCOMPARE(page.journalButton()->text(), QStringLiteral("Show the journal"));
        page.journalButton()->click();
        QVERIFY(page.isShowingJournal());
        QCOMPARE(page.journalButton()->text(), QStringLiteral("Show the report"));
        QCOMPARE(page.field()->toPlainText(), page.journalText());

        const QString journal = page.journalText();
        QCOMPARE(journal.count(QLatin1Char('\n')), 3);
        QVERIFY(!journal.contains(QStringLiteral("old line")));
        QVERIFY(!journal.contains(kSerial));
        QVERIFY(!journal.contains(kLabel));
        QVERIFY(!journal.contains(QStringLiteral("anna")));
        // Every line of the journal is also a line of the full report.
        for (const QString &line : journal.split(QLatin1Char('\n')))
            QVERIFY2(page.fullText().contains(line), qPrintable(line));

        page.journalButton()->click();
        QVERIFY(!page.isShowingJournal());
        QCOMPARE(page.field()->toPlainText(), page.summaryText());
    }

    void fileNameHasNoColon()
    {
        const QDateTime local(QDate(2026, 11, 18), QTime(21, 2, 13));
        QCOMPARE(ReportPage::fileNameFor(local),
                 QStringLiteral("tacetos-report-2026-11-18_210213.txt"));
        QVERIFY(!ReportPage::fileNameFor(local).contains(QLatin1Char(':')));
    }

    void saveWritesTheFullReport()
    {
        ReportPage page;
        page.setInput(input());
        page.setDocumentsFolder(documents_.path());
        page.setClock([] { return QDateTime(QDate(2026, 11, 18), QTime(21, 2, 13)); });
        QString folder;
        QString suggested;
        const QString target = documents_.filePath(QStringLiteral("out.txt"));
        page.setSavePicker([&](const QString &f, const QString &name) {
            folder = f;
            suggested = name;
            return target;
        });
        page.saveButton()->click();
        QCOMPARE(folder, documents_.path());
        QCOMPARE(suggested, QStringLiteral("tacetos-report-2026-11-18_210213.txt"));

        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString written = QString::fromUtf8(file.readAll());
        QCOMPARE(written, buildReport(input()).full);
        QVERIFY(written.size() > page.summaryText().size() || written == page.summaryText());
        QVERIFY(!written.contains(kSerial));
        QVERIFY(!written.contains(kLabel));
        QVERIFY(!written.contains(QStringLiteral("anna")));
        QVERIFY(page.noteText().startsWith(QStringLiteral("Saved")));
    }

    void saveCancelledWritesNothing()
    {
        ReportPage page;
        page.setInput(input());
        page.setSavePicker([](const QString &, const QString &) { return QString(); });
        page.saveButton()->click();
        QVERIFY(page.noteText().isEmpty());
        QVERIFY(QDir(documents_.path())
                    .entryList(QStringList{QStringLiteral("tacetos-*")}, QDir::Files)
                    .isEmpty());
    }

    void saveToAFolderThatDoesNotExistSaysSo()
    {
        ReportPage page;
        page.setInput(input());
        const QString target = documents_.filePath(QStringLiteral("nothing/here/out.txt"));
        page.setSavePicker([&target](const QString &, const QString &) { return target; });
        page.saveButton()->click();
        QVERIFY(page.noteText().contains(QStringLiteral("could not be saved")));
    }

    void discordLinkOpensTheInvite()
    {
        ReportPage page;
        QString opened;
        page.setLinkOpener([&opened](const QString &url) { opened = url; });
        page.discordButton()->click();
        QCOMPARE(opened, QStringLiteral("TACETOS_DISCORD_INVITE"));
    }

    void backIsSignalled()
    {
        ReportPage page;
        QSignalSpy spy(&page, &ReportPage::backRequested);
        page.backButton()->click();
        QCOMPARE(spy.count(), 1);
    }

    void failureButton()
    {
        QWidget parent;
        QPushButton *button = ReportPage::failureButton(&parent);
        QCOMPARE(button->text(), QStringLiteral("Copy report for the Discord"));
        QCOMPARE(button->accessibleName(), button->text());
        QCOMPARE(button->objectName(), QStringLiteral("reportFromFailure"));
    }

    void namesAndAccessibility()
    {
        ReportPage page;
        QVERIFY(page.findChild<QPlainTextEdit *>(QStringLiteral("reportText")));
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("copyReport")));
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("saveReport")));
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("showJournal")));
        for (QPushButton *button : page.findChildren<QPushButton *>())
            QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(button->objectName()));
    }
};

} // namespace

QTEST_MAIN(TestReportPage)
#include "tst_reportpage.moc"
