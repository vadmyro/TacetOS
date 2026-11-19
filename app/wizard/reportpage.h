// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "report.h"

#include <QDateTime>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace tacetos {

// The report page (docs/specs/install-screens.md section 10.2). It shows the anonymised summary
// of the last flow, copies it, saves the full report and shows the journal behind it. It sends
// nothing anywhere. The window puts it over the page of the step and wires `backRequested`.
class ReportPage : public QWidget
{
    Q_OBJECT

public:
    explicit ReportPage(QWidget *parent = nullptr);

    // Builds both forms from what the window gathered, and shows the summary.
    void setInput(const journal::ReportInput &input);

    // What the system gives, as functions the tests replace: the clipboard, the save dialog (it
    // gets the folder and the suggested name, and returns the file or nothing), the browser, the
    // local clock for the name of the file, and the documents folder.
    void setClipboardWriter(const std::function<void(const QString &)> &writer)
    {
        clipboard_ = writer;
    }
    void setSavePicker(const std::function<QString(const QString &, const QString &)> &picker)
    {
        picker_ = picker;
    }
    void setLinkOpener(const std::function<void(const QString &)> &opener) { opener_ = opener; }
    void setClock(const std::function<QDateTime()> &clock) { clock_ = clock; }
    void setDocumentsFolder(const QString &folder) { documents_ = folder; }

    // `tacetos-report-2026-11-18_210213.txt`: the local time, never a `:`.
    static QString fileNameFor(const QDateTime &local);

    // The button of a failure page, "Copy report for the Discord" (section 10.1). The page that
    // shows it connects its `clicked` to the window, which opens this page.
    static QPushButton *failureButton(QWidget *parent);

    // What the user reads and what Copy puts in the clipboard.
    QString summaryText() const { return report_.summary; }
    QString fullText() const { return report_.full; }
    QString journalText() const { return journal_; }
    QString shownText() const;
    QString countText() const;
    QString noteText() const;
    bool isShowingJournal() const { return showingJournal_; }

    QPushButton *copyButton() const { return copy_; }
    QPushButton *saveButton() const { return save_; }
    QPushButton *journalButton() const { return journalToggle_; }
    QPushButton *backButton() const { return back_; }
    QPushButton *discordButton() const { return discord_; }
    QPlainTextEdit *field() const { return field_; }

signals:
    void backRequested();

private:
    void copyReport();
    void saveReport();
    void toggleJournal();
    void showNote(const QString &text);
    void refresh();

    journal::Report report_;
    QString journal_;
    bool showingJournal_ = false;
    QString documents_;
    std::function<void(const QString &)> clipboard_;
    std::function<QString(const QString &, const QString &)> picker_;
    std::function<void(const QString &)> opener_;
    std::function<QDateTime()> clock_;
    QTimer noteTimer_;

    QLabel *title_ = nullptr;
    QLabel *intro_ = nullptr;
    QPushButton *discord_ = nullptr;
    QPlainTextEdit *field_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *note_ = nullptr;
    QPushButton *back_ = nullptr;
    QPushButton *journalToggle_ = nullptr;
    QPushButton *save_ = nullptr;
    QPushButton *copy_ = nullptr;
};

} // namespace tacetos
