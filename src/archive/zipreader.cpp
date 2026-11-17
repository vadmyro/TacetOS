// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "zipreader.h"

#include "crc32.h"

#include <QSet>
#include <QStringDecoder>

namespace tacetos::archive {

namespace {

// The marks that start each record of the format.
constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kDirectoryHeader = 0x02014b50;
constexpr quint32 kEndRecord = 0x06054b50;
constexpr quint32 kZip64EndRecord = 0x06064b50;
constexpr quint32 kZip64Locator = 0x07064b50;

// The fixed sizes of the records.
constexpr int kLocalSize = 30;
constexpr int kDirectorySize = 46;
constexpr int kEndSize = 22;
constexpr int kZip64EndSize = 56;
constexpr int kLocatorSize = 20;
constexpr int kLongestComment = 0xFFFF;

// The extra field that holds the 64-bit values of an entry.
constexpr quint16 kZip64Extra = 0x0001;
constexpr quint32 kNotHere32 = 0xFFFFFFFFu;
constexpr quint16 kNotHere16 = 0xFFFFu;

// General purpose flags.
constexpr int kEncrypted = 0x0001;
constexpr int kSizesAfterData = 0x0008;
constexpr int kStrongEncryption = 0x0040;
constexpr int kUtf8Name = 0x0800;

// The system that made an entry, and the type of a link in its mode bits.
constexpr int kMadeOnUnix = 3;
constexpr quint32 kTypeBits = 0xF000;
constexpr quint32 kLinkType = 0xA000;

constexpr int kStored = 0;
constexpr int kDeflated = 8;

constexpr qint64 kPiece = 1024 * 1024;

quint64 little(const QByteArray &bytes, qsizetype at, int size)
{
    quint64 value = 0;
    for (int i = size - 1; i >= 0; --i)
        value = (value << 8) | quint8(bytes.at(at + i));
    return value;
}

quint16 le16(const QByteArray &bytes, qsizetype at)
{
    return quint16(little(bytes, at, 2));
}
quint32 le32(const QByteArray &bytes, qsizetype at)
{
    return quint32(little(bytes, at, 4));
}
quint64 le64(const QByteArray &bytes, qsizetype at)
{
    return little(bytes, at, 8);
}

QString decodeName(const QByteArray &raw, int flags)
{
    if (flags & kUtf8Name)
        return QString::fromUtf8(raw);
    // Many tools write UTF-8 without the flag. What is not UTF-8 is read as
    // Latin-1, so that every byte gives a character.
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString name = utf8.decode(raw);
    return utf8.hasError() ? QString::fromLatin1(raw) : name;
}

// Section 7.6: the names that could write outside the folder they are
// unpacked into, or mean another file on another system. Empty when fine.
QString nameProblem(const QString &name, bool folder)
{
    if (name.isEmpty())
        return QStringLiteral("an entry with no name");
    if (name.contains(QLatin1Char('\\')))
        return QStringLiteral("a name with a backslash");
    if (name.contains(QChar(0)))
        return QStringLiteral("a name with a null character");
    if (name.startsWith(QLatin1Char('/')))
        return QStringLiteral("a name that starts with /");
    if (name.size() >= 2 && name.at(1) == QLatin1Char(':') && name.at(0).unicode() < 128 &&
        name.at(0).isLetter())
        return QStringLiteral("a name that starts with a drive letter");
    QStringList parts = name.split(QLatin1Char('/'));
    if (folder)
        parts.removeLast();
    for (const QString &part : std::as_const(parts)) {
        if (part == QLatin1String(".."))
            return QStringLiteral("a name with ..");
        if (part.isEmpty())
            return QStringLiteral("a name with an empty folder");
    }
    return QString();
}

} // namespace

bool ZipReader::fail(const QString &why)
{
    m_error = why;
    return false;
}

bool ZipReader::open(const ZipLimits &limits)
{
    m_entries.clear();
    m_unpacked = 0;
    m_error.clear();
    if (!m_file.isOpen() && !m_file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("the archive cannot be opened: ") + m_file.errorString());
    qint64 count = 0;
    qint64 directoryOffset = 0;
    qint64 directorySize = 0;
    qint64 directoryEnd = 0;
    if (!readEnd(&count, &directoryOffset, &directorySize, &directoryEnd))
        return false;
    if (count > limits.entries)
        return fail(QStringLiteral("more entries than allowed"));
    if (!m_file.seek(directoryOffset))
        return fail(QStringLiteral("the central directory cannot be read"));
    const QByteArray directory = m_file.read(directorySize);
    if (directory.size() != directorySize)
        return fail(QStringLiteral("the central directory cannot be read"));
    if (!readDirectory(directory, count, limits)) {
        m_entries.clear();
        m_unpacked = 0;
        return false;
    }
    return true;
}

bool ZipReader::readEnd(qint64 *count, qint64 *directoryOffset, qint64 *directorySize,
                        qint64 *directoryEnd)
{
    const qint64 size = m_file.size();
    if (size < kEndSize)
        return fail(QStringLiteral("not a zip archive"));
    // The end record is the last one, followed only by its comment.
    const qint64 tailSize = std::min<qint64>(size, kEndSize + kLongestComment);
    if (!m_file.seek(size - tailSize))
        return fail(QStringLiteral("not a zip archive"));
    const QByteArray tail = m_file.read(tailSize);
    qsizetype at = -1;
    for (qsizetype i = tail.size() - kEndSize; i >= 0; --i) {
        if (le32(tail, i) == kEndRecord && i + kEndSize + le16(tail, i + 20) == tail.size()) {
            at = i;
            break;
        }
    }
    if (at < 0)
        return fail(QStringLiteral("no end record: not a zip archive, or a cut one"));
    const qint64 endOffset = size - tailSize + at;
    quint64 disk = le16(tail, at + 4);
    quint64 directoryDisk = le16(tail, at + 6);
    quint64 onThisDisk = le16(tail, at + 8);
    quint64 total = le16(tail, at + 10);
    quint64 dirSize = le32(tail, at + 12);
    quint64 dirOffset = le32(tail, at + 16);
    quint64 dirEnd = quint64(endOffset);

    const bool zip64 = disk == kNotHere16 || directoryDisk == kNotHere16 ||
                       onThisDisk == kNotHere16 || total == kNotHere16 || dirSize == kNotHere32 ||
                       dirOffset == kNotHere32;
    if (zip64) {
        if (endOffset < kLocatorSize || !m_file.seek(endOffset - kLocatorSize))
            return fail(QStringLiteral("a Zip64 archive without its locator"));
        const QByteArray locator = m_file.read(kLocatorSize);
        if (locator.size() != kLocatorSize || le32(locator, 0) != kZip64Locator)
            return fail(QStringLiteral("a Zip64 archive without its locator"));
        if (le32(locator, 16) > 1)
            return fail(QStringLiteral("an archive in several parts"));
        const quint64 recordOffset = le64(locator, 8);
        if (recordOffset + kZip64EndSize > quint64(endOffset) || !m_file.seek(qint64(recordOffset)))
            return fail(QStringLiteral("the Zip64 end record is outside the archive"));
        const QByteArray record = m_file.read(kZip64EndSize);
        if (record.size() != kZip64EndSize || le32(record, 0) != kZip64EndRecord)
            return fail(QStringLiteral("the Zip64 end record is damaged"));
        disk = le32(record, 16);
        directoryDisk = le32(record, 20);
        onThisDisk = le64(record, 24);
        total = le64(record, 32);
        dirSize = le64(record, 40);
        dirOffset = le64(record, 48);
        dirEnd = recordOffset;
    }
    if (disk != 0 || directoryDisk != 0 || onThisDisk != total)
        return fail(QStringLiteral("an archive in several parts"));
    if (dirOffset > dirEnd || dirSize > dirEnd - dirOffset)
        return fail(QStringLiteral("the central directory is outside the archive"));
    // The smallest record of each entry bounds the count before anything is
    // read for it.
    if (total > dirSize / kDirectorySize)
        return fail(QStringLiteral("the central directory is damaged"));
    *count = qint64(total);
    *directoryOffset = qint64(dirOffset);
    *directorySize = qint64(dirSize);
    *directoryEnd = qint64(dirEnd);
    return true;
}

bool ZipReader::readDirectory(const QByteArray &directory, qint64 count, const ZipLimits &limits)
{
    const qint64 directoryOffset = m_file.pos() - directory.size();
    QSet<QString> seen;
    qsizetype at = 0;
    for (qint64 i = 0; i < count; ++i) {
        if (at + kDirectorySize > directory.size() || le32(directory, at) != kDirectoryHeader)
            return fail(QStringLiteral("the central directory is damaged"));
        const int madeBy = le16(directory, at + 4);
        const int flags = le16(directory, at + 8);
        const int method = le16(directory, at + 10);
        const quint32 crc = le32(directory, at + 16);
        quint64 packed = le32(directory, at + 20);
        quint64 size = le32(directory, at + 24);
        const int nameSize = le16(directory, at + 28);
        const int extraSize = le16(directory, at + 30);
        const int commentSize = le16(directory, at + 32);
        const quint32 external = le32(directory, at + 38);
        quint64 offset = le32(directory, at + 42);
        const qsizetype next = at + kDirectorySize + nameSize + extraSize + commentSize;
        if (next > directory.size())
            return fail(QStringLiteral("the central directory is damaged"));
        const QByteArray rawName = directory.mid(at + kDirectorySize, nameSize);
        const QByteArray extra = directory.mid(at + kDirectorySize + nameSize, extraSize);

        // The 64-bit values come in this order, each only when its 32-bit
        // field says it is not there.
        for (qsizetype e = 0; e + 4 <= extra.size();) {
            const quint16 id = le16(extra, e);
            const quint16 length = le16(extra, e + 2);
            if (e + 4 + length > extra.size())
                return fail(QStringLiteral("the central directory is damaged"));
            if (id == kZip64Extra) {
                qsizetype p = e + 4;
                const qsizetype end = p + length;
                for (quint64 *value : {&size, &packed, &offset}) {
                    if (*value != kNotHere32)
                        continue;
                    if (p + 8 > end)
                        return fail(QStringLiteral("the central directory is damaged"));
                    *value = le64(extra, p);
                    p += 8;
                }
            }
            e += 4 + length;
        }

        if ((flags & kEncrypted) || (flags & kStrongEncryption))
            return fail(QStringLiteral("an encrypted entry"));
        if (method != kStored && method != kDeflated)
            return fail(QStringLiteral("an entry packed with method %1").arg(method));
        const QString name = decodeName(rawName, flags);
        const bool folder = name.endsWith(QLatin1Char('/'));
        if (const QString problem = nameProblem(name, folder); !problem.isEmpty())
            return fail(problem);
        if ((madeBy >> 8) == kMadeOnUnix && ((external >> 16) & kTypeBits) == kLinkType)
            return fail(QStringLiteral("a link"));
        if (folder && size > 0)
            return fail(QStringLiteral("a folder with data"));
        if (method == kStored && packed != size)
            return fail(QStringLiteral("a stored entry whose two sizes differ"));
        const QString key = (folder ? name.chopped(1) : name).toCaseFolded();
        if (seen.contains(key))
            return fail(QStringLiteral("two names that differ only by case"));
        seen.insert(key);
        if (size > quint64(limits.unpackedBytes) ||
            m_unpacked > limits.unpackedBytes - qint64(size))
            return fail(QStringLiteral("more unpacked bytes than allowed"));
        m_unpacked += qint64(size);
        if (offset > quint64(directoryOffset) || packed > quint64(directoryOffset))
            return fail(QStringLiteral("an entry outside the archive"));

        ZipEntry entry;
        entry.name = name;
        entry.folder = folder;
        entry.method = method;
        entry.crc = crc;
        entry.packedSize = qint64(packed);
        entry.size = qint64(size);
        entry.dataOffset = qint64(offset);
        if (!checkLocal(entry, rawName, flags, directoryOffset))
            return false;
        m_entries << entry;
        at = next;
    }
    return true;
}

// The local header of an entry must say what the central directory says.
// `entry.dataOffset` holds the offset of the header, and then of the data.
bool ZipReader::checkLocal(ZipEntry &entry, const QByteArray &rawName, int flags,
                           qint64 directoryOffset)
{
    const auto mismatch = [this] {
        return fail(QStringLiteral("the central directory does not match the local headers"));
    };
    const qint64 offset = entry.dataOffset;
    if (offset + kLocalSize > directoryOffset || !m_file.seek(offset))
        return fail(QStringLiteral("an entry outside the archive"));
    const QByteArray header = m_file.read(kLocalSize);
    if (header.size() != kLocalSize || le32(header, 0) != kLocalHeader)
        return mismatch();
    const int localFlags = le16(header, 6);
    const int nameSize = le16(header, 26);
    const int extraSize = le16(header, 28);
    if (m_file.read(nameSize) != rawName || le16(header, 8) != entry.method ||
        (localFlags & kEncrypted) != (flags & kEncrypted))
        return mismatch();
    if (!(localFlags & kSizesAfterData)) {
        const quint32 packed = le32(header, 18);
        const quint32 size = le32(header, 22);
        if (le32(header, 14) != entry.crc ||
            (packed != kNotHere32 && packed != quint64(entry.packedSize)) ||
            (size != kNotHere32 && size != quint64(entry.size)))
            return mismatch();
    }
    entry.dataOffset = offset + kLocalSize + nameSize + extraSize;
    if (entry.dataOffset > directoryOffset || entry.packedSize > directoryOffset - entry.dataOffset)
        return fail(QStringLiteral("an entry outside the archive"));
    return true;
}

bool ZipReader::extract(const ZipEntry &entry, const Sink &sink)
{
    if (!m_file.seek(entry.dataOffset))
        return fail(QStringLiteral("the data of an entry cannot be read"));
    Crc32 crc;
    qint64 total = 0;
    const Sink counting = [&](const char *data, qint64 size) {
        crc.add(data, size);
        total += size;
        return sink(data, size);
    };
    if (entry.method == kStored) {
        for (qint64 left = entry.packedSize; left > 0;) {
            const QByteArray piece = m_file.read(std::min(left, kPiece));
            if (piece.isEmpty())
                return fail(QStringLiteral("the data ends in the middle"));
            left -= piece.size();
            if (!counting(piece.constData(), piece.size()))
                return fail(QStringLiteral("stopped"));
        }
    } else {
        QString why;
        if (!inflate(m_file, entry.packedSize, entry.size, counting, &why))
            return fail(why);
    }
    if (total != entry.size)
        return fail(QStringLiteral("the data unpacks to less than its declared size"));
    if (crc.value() != entry.crc)
        return fail(QStringLiteral("the CRC-32 of an entry does not match"));
    return true;
}

bool ZipReader::extractTo(const ZipEntry &entry, QIODevice &out, const std::function<bool()> &stop)
{
    bool writeFailed = false;
    const bool ok = extract(entry, [&](const char *data, qint64 size) {
        if (stop && stop())
            return false;
        writeFailed = out.write(data, size) != size;
        return !writeFailed;
    });
    if (!ok && writeFailed)
        return fail(QStringLiteral("the unpacked data cannot be written: ") + out.errorString());
    return ok;
}

QByteArray ZipReader::read(const ZipEntry &entry)
{
    QByteArray bytes;
    bytes.reserve(qsizetype(std::min<qint64>(entry.size, kPiece)));
    const bool ok = extract(entry, [&bytes](const char *data, qint64 size) {
        bytes.append(data, qsizetype(size));
        return true;
    });
    return ok ? bytes : QByteArray();
}

} // namespace tacetos::archive
