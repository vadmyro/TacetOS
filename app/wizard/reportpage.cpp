// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/reportpage.h"

#include "entry.h"
#include "journalconstants.h"

#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace tacetos {

using namespace journal;

namespace {

// Screens section 10.2: the invitation to the TacetOS Discord, put in by the author.
constexpr const char *kDiscordInvite = "TACETOS_DISCORD_INVITE";

// Screens section 10.2: how long "Copied" stays on screen.
constexpr int kNoteMs = 4000;

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
}

QPushButton *makeButton(const QString &name, const QString &text, QWidget *parent, const char *role)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setAccessibleName(text);
    b->setProperty("role", QString::fromLatin1(role));
    return b;
}

} // namespace

ReportPage::ReportPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("reportPage"));
    clipboard_ = [](const QString &text) {
        if (QClipboard *board = QGuiApplication::clipboard())
            board->setText(text);
    };
    picker_ = [this](const QString &folder, const QString &name) {
        return QFileDialog::getSaveFileName(this, wtr("Save full report"),
                                            QDir(folder).filePath(name), wtr("Text files (*.txt)"));
    };
    opener_ = [](const QString &url) { QDesktopServices::openUrl(QUrl(url)); };
    clock_ = [] { return QDateTime::currentDateTime(); };
    documents_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    noteTimer_.setSingleShot(true);
    noteTimer_.setInterval(kNoteMs);
    connect(&noteTimer_, &QTimer::timeout, this, [this] { note_->clear(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);

    title_ = new QLabel(wtr("Report"), this);
    title_->setObjectName(QStringLiteral("reportTitle"));
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);

    intro_ = new QLabel(wtr("Paste this on the TacetOS Discord to get help. It holds no serial "
                            "number, no name of a file of yours and no name of your computer."),
                        this);
    intro_->setObjectName(QStringLiteral("reportIntro"));
    intro_->setWordWrap(true);

    discord_ = makeButton(QStringLiteral("openDiscord"), wtr("Open the TacetOS Discord"), this,
                          "secondary");
    discord_->setFlat(true);
    discord_->setCursor(Qt::PointingHandCursor);
    connect(discord_, &QPushButton::clicked, this,
            [this] { opener_(QString::fromLatin1(kDiscordInvite)); });

    field_ = new QPlainTextEdit(this);
    field_->setObjectName(QStringLiteral("reportText"));
    field_->setAccessibleName(wtr("Report"));
    field_->setReadOnly(true);
    field_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    field_->setMinimumHeight(240);
    field_->setMaximumHeight(240);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    field_->setFont(mono);

    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("reportCount"));
    count_->setProperty("role", QStringLiteral("secondary"));
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("reportNote"));
    note_->setWordWrap(true);

    back_ = makeButton(QStringLiteral("reportBack"), wtr("Back"), this, "secondary");
    journalToggle_ =
        makeButton(QStringLiteral("showJournal"), wtr("Show the journal"), this, "secondary");
    save_ = makeButton(QStringLiteral("saveReport"), wtr("Save full report..."), this, "secondary");
    copy_ = makeButton(QStringLiteral("copyReport"), wtr("Copy report"), this, "primary");
    connect(back_, &QPushButton::clicked, this, &ReportPage::backRequested);
    connect(journalToggle_, &QPushButton::clicked, this, &ReportPage::toggleJournal);
    connect(save_, &QPushButton::clicked, this, &ReportPage::saveReport);
    connect(copy_, &QPushButton::clicked, this, &ReportPage::copyReport);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(back_);
    buttons->addWidget(journalToggle_);
    buttons->addStretch();
    buttons->addWidget(save_);
    buttons->addWidget(copy_);

    layout->addWidget(title_);
    layout->addSpacing(8);
    layout->addWidget(intro_);
    layout->addWidget(discord_, 0, Qt::AlignLeft);
    layout->addSpacing(8);
    layout->addWidget(field_);
    layout->addWidget(count_);
    layout->addWidget(note_);
    layout->addSpacing(8);
    layout->addLayout(buttons);
    layout->addStretch();

    setFocusProxy(copy_);
    refresh();
}

QString ReportPage::fileNameFor(const QDateTime &local)
{
    return QStringLiteral("tacetos-report-") + local.toString(QStringLiteral("yyyy-MM-dd_HHmmss")) +
           QStringLiteral(".txt");
}

QPushButton *ReportPage::failureButton(QWidget *parent)
{
    return makeButton(QStringLiteral("reportFromFailure"), wtr("Copy report for the Discord"),
                      parent, "secondary");
}

void ReportPage::setInput(const ReportInput &input)
{
    report_ = buildReport(input);

    // The journal view: the entries of the flow, with the same masking as the report.
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
    QStringList lines;
    for (const Entry &entry : input.entries) {
        if (input.flowStart.isValid() && entry.time < input.flowStart)
            continue;
        lines << scrubber.apply(formatEntry(entry));
    }
    journal_ = lines.join(QLatin1Char('\n'));

    showingJournal_ = false;
    note_->clear();
    refresh();
}

QString ReportPage::shownText() const
{
    return showingJournal_ ? journal_ : report_.summary;
}

QString ReportPage::countText() const
{
    return count_->text();
}

QString ReportPage::noteText() const
{
    return note_->text();
}

void ReportPage::refresh()
{
    field_->setPlainText(shownText());
    journalToggle_->setText(showingJournal_ ? wtr("Show the report") : wtr("Show the journal"));
    journalToggle_->setAccessibleName(journalToggle_->text());
    // The count is the one of the summary, the text that Copy puts in the clipboard.
    QLocale locale;
    count_->setText(wtr("%1 of %2 characters")
                        .arg(locale.toString(qsizetype(report_.summary.size())),
                             locale.toString(kReportSummaryMax)));
}

void ReportPage::toggleJournal()
{
    showingJournal_ = !showingJournal_;
    refresh();
}

void ReportPage::showNote(const QString &text)
{
    note_->setText(text);
    noteTimer_.start();
    QAccessibleEvent alert(note_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void ReportPage::copyReport()
{
    clipboard_(report_.summary);
    showNote(wtr("Copied. Paste it in the help channel of the Discord"));
}

void ReportPage::saveReport()
{
    const QString path = picker_(documents_, fileNameFor(clock_()));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showNote(wtr("The report could not be saved there."));
        return;
    }
    file.write(report_.full.toUtf8());
    if (!file.commit()) {
        showNote(wtr("The report could not be saved there."));
        return;
    }
    showNote(wtr("Saved."));
}

} // namespace tacetos
