// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "entry.h"
#include "journalconstants.h"

#include <QFile>
#include <QString>

// The file of the journal (docs/specs/device-state.md section 3.4): one line
// per entry, flushed at once, rotated by size. It is given the data folder of
// the app and never looks for one itself.

namespace tacetos::journal {

class JournalFile
{
public:
    // Opens `<folder>/logs/tacetos.log` for appending, making the folder if
    // needed. When that fails the object stays closed: see isOpen() and
    // errorString(). `maxBytes` and `files` are the size and the count of
    // section 3.4, there to be made small by the tests.
    explicit JournalFile(const QString &folder, qint64 maxBytes = kJournalFileMaxBytes,
                         int files = kJournalFiles);
    ~JournalFile();

    JournalFile(const JournalFile &) = delete;
    JournalFile &operator=(const JournalFile &) = delete;

    // False after a failure: nothing more is written, for the rest of the session.
    bool isOpen() const { return m_file.isOpen(); }

    // Why the file is off, in English, empty while it is on.
    QString errorString() const { return m_error; }

    // The path of the current file.
    QString path() const { return m_path; }

    // Writes the line of the entry (formatEntry plus a line end) and flushes.
    // Rotates first when the line would take the file over the size, so that
    // an entry is never split. Returns false, and closes the file, on failure.
    bool write(const Entry &entry);

private:
    bool rotate();
    bool fail(const QString &what);

    QString m_path;
    qint64 m_maxBytes;
    int m_files;
    QFile m_file;
    QString m_error;
};

} // namespace tacetos::journal
