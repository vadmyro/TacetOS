// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "entry.h"
#include "journalconstants.h"

#include <QList>
#include <QObject>
#include <deque>
#include <functional>
#include <memory>

// The journal (docs/specs/device-state.md section 3): the last entries in
// memory, announced as they come, and optionally written to a file. It lives
// in one thread, the one of the app.

namespace tacetos::journal {

class JournalFile;

class Journal : public QObject
{
    Q_OBJECT

public:
    explicit Journal(QObject *parent = nullptr);
    ~Journal() override;

    // Adds an entry. The text goes through cleanText, the port through
    // cleanPort, the time is the clock's (now, in UTC, unless set). The entry
    // is kept in the ring, announced once, and written to the file if there is
    // one that works.
    void add(Level level, Domain domain, const QString &port, const QString &text);

    // The entries in the ring, the oldest first.
    QList<Entry> entries() const;

    // Starts writing to `<folder>/logs/tacetos.log`. `folder` is the data
    // folder of the app, given by the caller. Returns false when the file
    // cannot be used: one warning is added to the ring and the journal goes on
    // in memory. Replaces a file attached before.
    bool attachFile(const QString &folder);

    // True while a file is attached and works.
    bool fileActive() const;

    // The home folder that cleanText shortens to `~`. The account's one, from
    // the start; the tests set their own.
    void setHomeFolder(const QString &home) { m_home = home; }

    // The clock of the entries, for the tests.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

    // The size of the file and the number of files, for the tests.
    void setFileLimits(qint64 maxBytes, int files);

signals:
    // Sent once for each entry, after it is in the ring.
    void entryAdded(const tacetos::journal::Entry &entry);

private:
    void push(const Entry &entry);
    void dropFile(const QString &reason);

    std::deque<Entry> m_entries;
    std::unique_ptr<JournalFile> m_file;
    QString m_home;
    std::function<QDateTime()> m_clock;
    qint64 m_fileMaxBytes = kJournalFileMaxBytes;
    int m_fileCount = kJournalFiles;
};

} // namespace tacetos::journal
