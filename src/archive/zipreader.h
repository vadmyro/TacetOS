// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "inflate.h"

#include <QFile>
#include <QList>
#include <QString>
#include <functional>

// A reader of zip archives (docs/specs/install-wizard.md section 7.6): the
// public zip format, Zip64 included, with the methods stored and deflate.

namespace tacetos::archive {

struct ZipEntry
{
    // As written in the archive, `/` between folders. A folder ends with `/`.
    QString name;
    bool folder = false;
    // 0: stored, 8: deflate.
    int method = 0;
    quint32 crc = 0;
    qint64 packedSize = 0;
    qint64 size = 0;
    // Where the data of the entry starts in the archive.
    qint64 dataOffset = 0;
};

// What an archive may hold at most. The caller gives them: a firmware and an
// album do not have the same.
struct ZipLimits
{
    int entries = 0;
    qint64 unpackedBytes = 0;
};

class ZipReader
{
public:
    explicit ZipReader(const QString &path) : m_file(path) {}

    // Reads the list of entries and checks it. Refused with the whole
    // archive, the reason in `error()`: an encrypted entry, another method, a
    // name with `..`, a name that starts with `/` or a drive letter, a `\` in
    // a name, two names that differ only by case, a link, more entries or
    // more unpacked bytes than the limits, a central directory that does not
    // match the local headers.
    bool open(const ZipLimits &limits);

    QString error() const { return m_error; }
    QList<ZipEntry> entries() const { return m_entries; }
    // The sum of the sizes of the entries.
    qint64 unpackedSize() const { return m_unpacked; }

    // Unpacks an entry, giving it to `sink` by pieces, and checks its size and
    // CRC-32. A sink that returns false stops it (error "stopped").
    bool extract(const ZipEntry &entry, const Sink &sink);
    // The same into `out`. `stop` is looked at between two pieces.
    bool extractTo(const ZipEntry &entry, QIODevice &out,
                   const std::function<bool()> &stop = std::function<bool()>());
    // A small entry in memory. Empty with `error()` set when it fails.
    QByteArray read(const ZipEntry &entry);

private:
    bool fail(const QString &why);
    bool readEnd(qint64 *count, qint64 *directoryOffset, qint64 *directorySize,
                 qint64 *directoryEnd);
    bool readDirectory(const QByteArray &directory, qint64 count, const ZipLimits &limits);
    bool checkLocal(ZipEntry &entry, const QByteArray &rawName, int flags, qint64 directoryOffset);

    QFile m_file;
    QString m_error;
    QList<ZipEntry> m_entries;
    qint64 m_unpacked = 0;
};

} // namespace tacetos::archive
