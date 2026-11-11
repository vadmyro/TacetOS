// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "journalfile.h"

#include <QDir>
#include <QFileInfo>

namespace tacetos::journal {

namespace {

// `logs/tacetos.log` and the number 1 give `logs/tacetos.1.log`.
QString oldPath(const QString &path, int number)
{
    const QFileInfo info(path);
    QString name = info.completeBaseName() + QLatin1Char('.') + QString::number(number);
    if (!info.suffix().isEmpty())
        name += QLatin1Char('.') + info.suffix();
    return info.absoluteDir().filePath(name);
}

} // namespace

JournalFile::JournalFile(const QString &folder, qint64 maxBytes, int files)
    : m_path(QDir(folder).filePath(kJournalFile)), m_maxBytes(maxBytes), m_files(files)
{
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(dir)) {
        fail(QStringLiteral("cannot create the journal folder %1").arg(dir));
        return;
    }
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
        fail(QStringLiteral("cannot open the journal file %1: %2")
                 .arg(m_path, m_file.errorString()));
}

JournalFile::~JournalFile() = default;

bool JournalFile::fail(const QString &what)
{
    m_error = what;
    if (m_file.isOpen())
        m_file.close();
    return false;
}

bool JournalFile::write(const Entry &entry)
{
    if (!m_file.isOpen())
        return false;

    // No Text flag: the line end is \n on every system.
    const QByteArray line = formatEntry(entry).toUtf8() + '\n';
    if (m_file.size() > 0 && m_file.size() + line.size() > m_maxBytes && !rotate())
        return false;

    if (m_file.write(line) != line.size() || !m_file.flush())
        return fail(QStringLiteral("cannot write the journal file: %1").arg(m_file.errorString()));
    return true;
}

bool JournalFile::rotate()
{
    m_file.close();

    // The oldest goes, then each one takes the next number.
    for (int number = m_files - 1; number >= 1; --number) {
        const QString from = number == 1 ? m_path : oldPath(m_path, number - 1);
        const QString to = oldPath(m_path, number);
        if (QFileInfo::exists(to) && !QFile::remove(to))
            return fail(QStringLiteral("cannot remove the old journal file %1").arg(to));
        if (QFileInfo::exists(from) && !QFile::rename(from, to))
            return fail(QStringLiteral("cannot rotate the journal file %1").arg(from));
    }
    // With a single file there is no old one: the current one is emptied.
    if (m_files <= 1 && QFileInfo::exists(m_path) && !QFile::remove(m_path))
        return fail(QStringLiteral("cannot remove the journal file %1").arg(m_path));

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
        return fail(QStringLiteral("cannot open the journal file %1: %2")
                        .arg(m_path, m_file.errorString()));
    return true;
}

} // namespace tacetos::journal
