// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "journal.h"

#include "journalfile.h"

#include <QDir>

namespace tacetos::journal {

Journal::Journal(QObject *parent)
    : QObject(parent), m_home(QDir::homePath()),
      m_clock([] { return QDateTime::currentDateTimeUtc(); })
{
}

Journal::~Journal() = default;

void Journal::add(Level level, Domain domain, const QString &port, const QString &text)
{
    Entry entry;
    entry.time = m_clock().toUTC();
    entry.level = level;
    entry.domain = domain;
    entry.port = cleanPort(port);
    entry.text = cleanText(text, m_home, kJournalLineMax);
    push(entry);

    if (m_file && !m_file->write(entry))
        dropFile(m_file->errorString());
}

QList<Entry> Journal::entries() const
{
    return QList<Entry>(m_entries.begin(), m_entries.end());
}

bool Journal::attachFile(const QString &folder)
{
    m_file = std::make_unique<JournalFile>(folder, m_fileMaxBytes, m_fileCount);
    if (m_file->isOpen())
        return true;
    dropFile(m_file->errorString());
    return false;
}

bool Journal::fileActive() const
{
    return m_file && m_file->isOpen();
}

void Journal::setFileLimits(qint64 maxBytes, int files)
{
    m_fileMaxBytes = maxBytes;
    m_fileCount = files;
}

void Journal::push(const Entry &entry)
{
    m_entries.push_back(entry);
    while (m_entries.size() > static_cast<std::size_t>(kJournalMaxEntries))
        m_entries.pop_front();
    emit entryAdded(entry);
}

// Section 3.4 rule 3: the file part is off for the rest of the session, one
// warning goes to the ring only, and the journal goes on.
void Journal::dropFile(const QString &reason)
{
    m_file.reset();

    Entry entry;
    entry.time = m_clock().toUTC();
    entry.level = Level::Warning;
    entry.domain = Domain::App;
    entry.text = cleanText(QStringLiteral("journal file off for this session: ") + reason, m_home,
                           kJournalLineMax);
    push(entry);
}

} // namespace tacetos::journal
