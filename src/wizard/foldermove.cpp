// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "foldermove.h"

#include "classicdfu.h"
#include "constants.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace tacetos::wizard {

namespace {

// A settings or screen file is a few kilobytes: a bigger one is not one of them.
constexpr qint64 kPathFileMax = qint64(4) << 20;

bool isDir(const QString &path)
{
    return QFileInfo(path).isDir();
}

// Calls `beforeWrite` the first time only.
class FirstWrite
{
public:
    explicit FirstWrite(const std::function<void()> &call) : m_call(call) {}
    void operator()()
    {
        if (!m_done && m_call)
            m_call();
        m_done = true;
    }

private:
    const std::function<void()> &m_call;
    bool m_done = false;
};

// The files of step 4: the settings files at the root of the folder, then those
// of its themes folder and the screen files of its wps folder, at any depth.
QStringList pathFiles(const QString &folderPath)
{
    const QDir folder(folderPath);
    const QDir::Filters files = QDir::Files | QDir::Hidden | QDir::NoSymLinks;
    QStringList found;
    const QStringList settings{QString(device::kSettingsFilePattern)};
    for (const QString &name : folder.entryList(settings, files))
        found << folder.filePath(name);
    const auto walk = [&](const QString &sub, const QStringList &patterns) {
        QDirIterator it(folder.filePath(sub), patterns, files, QDirIterator::Subdirectories);
        while (it.hasNext())
            found << it.next();
    };
    walk(QString(device::kThemesFolder), settings);
    QStringList screens;
    for (const char *pattern : device::kScreenFilePatterns)
        screens << QLatin1String(pattern);
    walk(QString(device::kScreensFolder), screens);
    return found;
}

} // namespace

bool bootloaderFirst(const QString &volumeRoot, const QString &folder, const QStringList &records,
                     const QString &serial)
{
    const QString old(device::kFirmwareFolder);
    if (volumeRoot.isEmpty() || folder.compare(old, Qt::CaseInsensitive) == 0 ||
        !isDir(QDir(volumeRoot).filePath(old)))
        return false;
    if (classicStateBefore(records, serial) == QLatin1String("absent"))
        return false;
    return classicRecordFolder(records, serial).compare(folder, Qt::CaseInsensitive) != 0;
}

QString moveFirmwareFolder(const QString &volumeRoot, const QString &from, const QString &to,
                           const std::function<void()> &beforeWrite)
{
    const QDir root(volumeRoot);
    const QString source = root.filePath(from);
    const QString target = root.filePath(to);
    if (volumeRoot.isEmpty() || !isDir(source))
        return QString();
    FirstWrite first(beforeWrite);
    if (!QFileInfo::exists(target)) {
        first();
        return QDir().rename(source, target)
                   ? QString()
                   : QStringLiteral("the old firmware folder cannot be renamed");
    }
    if (!isDir(target))
        return QStringLiteral("the new firmware folder is a file");

    // Both are there: an earlier move was cut. Every file goes in, or goes away
    // when the new folder has its own copy.
    QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    QStringList moved;
    while (it.hasNext())
        moved << it.next();
    std::sort(moved.begin(), moved.end());
    for (const QString &file : std::as_const(moved)) {
        const QString place = QDir(target).filePath(QDir(source).relativeFilePath(file));
        first();
        if (QFileInfo::exists(place)) {
            if (!QFile::remove(file))
                return QStringLiteral("a file of the old firmware folder cannot be removed");
            continue;
        }
        if (!QDir().mkpath(QFileInfo(place).absolutePath()) || !QFile::rename(file, place))
            return QStringLiteral("a file of the old firmware folder cannot be moved");
    }

    // Then its folders, the deepest first. A folder that is not empty stays,
    // and so does the old folder: nothing is deleted that was not moved.
    QStringList folders;
    QDirIterator dirs(source, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                      QDirIterator::Subdirectories);
    while (dirs.hasNext())
        folders << dirs.next();
    std::sort(folders.begin(), folders.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    first();
    for (const QString &folder : std::as_const(folders))
        QDir().rmdir(folder);
    return QDir().rmdir(source) ? QString()
                                : QStringLiteral("the old firmware folder is not empty");
}

QByteArray rewrittenPaths(const QByteArray &text, const QString &from, const QString &to)
{
    // A path starts a value: the start of a line, or after a space, a separator
    // of the settings or of a tag of the screens, or a quote. The folder ends
    // the path or is followed by its slash.
    const QRegularExpression path(QStringLiteral("(^|[\\s:,(|=\"'])/%1(?=/|[\\s,)|\"']|$)")
                                      .arg(QRegularExpression::escape(from)),
                                  QRegularExpression::CaseInsensitiveOption |
                                      QRegularExpression::MultilineOption);
    // Latin-1 keeps every byte as it was: the names are ASCII.
    QString latin = QString::fromLatin1(text);
    latin.replace(path, QStringLiteral("\\1/") + to);
    return latin.toLatin1();
}

QString rewriteFolderPaths(const QString &folderPath, const QString &from, const QString &to,
                           const std::function<void()> &beforeWrite)
{
    if (folderPath.isEmpty() || !isDir(folderPath))
        return QString();
    FirstWrite first(beforeWrite);
    for (const QString &name : pathFiles(folderPath)) {
        QFile in(name);
        if (in.size() > kPathFileMax)
            continue;
        if (!in.open(QIODevice::ReadOnly))
            return QStringLiteral("a settings file cannot be read");
        const QByteArray text = in.readAll();
        in.close();
        const QByteArray rewritten = rewrittenPaths(text, from, to);
        if (rewritten == text)
            continue;
        first();
        QSaveFile out(name);
        if (!out.open(QIODevice::WriteOnly) || out.write(rewritten) != rewritten.size() ||
            !out.commit())
            return QStringLiteral("a settings file cannot be written");
    }
    return QString();
}

} // namespace tacetos::wizard
