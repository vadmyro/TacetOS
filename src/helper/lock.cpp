// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "lock.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace tacetos::helper {

QString DiskLock::defaultFolder()
{
    QString base = QDir::tempPath();
#if defined(Q_OS_LINUX)
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir::isAbsolutePath(runtime))
        base = runtime;
#endif
    return QDir(base).filePath(QStringLiteral("tacetos/locks"));
}

QString DiskLock::lockFilePath(const QString &folder, const QString &key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
    return QDir(folder).filePath(QString::fromLatin1(hash.toHex().left(32)) +
                                 QStringLiteral(".lock"));
}

bool DiskLock::processRuns(qint64 pid)
{
    if (pid <= 0)
        return false;
#if defined(Q_OS_WIN)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    if (::kill(pid_t(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

static qint64 readPid(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    return f.read(32).trimmed().toLongLong();
}

std::unique_ptr<DiskLock> DiskLock::acquire(const QString &folder, const QString &key, QString *why,
                                            bool *held)
{
    auto fail = [&](const QString &reason, bool isHeld) -> std::unique_ptr<DiskLock> {
        if (why)
            *why = reason;
        if (held)
            *held = isHeld;
        return nullptr;
    };
    if (!QDir().mkpath(folder))
        return fail(QStringLiteral("cannot create the lock folder %1").arg(folder), false);

    const qint64 me = QCoreApplication::applicationPid();
    const QString path = lockFilePath(folder, key);
    // The file is written whole under a private name, then moved to its place: a
    // rename never replaces a file that exists, so there is one winner and nobody
    // reads a lock file that is still empty.
    const QString mine = path + QLatin1Char('.') + QString::number(me) + QStringLiteral(".tmp");

    for (int attempt = 0; attempt < 4; ++attempt) {
        {
            QFile f(mine);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                f.write(QByteArray::number(me) + '\n') < 0)
                return fail(QStringLiteral("cannot write the lock file in %1").arg(folder), false);
        }
        if (QFile::rename(mine, path))
            return std::unique_ptr<DiskLock>(new DiskLock(path, me));
        QFile::remove(mine);

        const qint64 owner = readPid(path);
        if (processRuns(owner))
            return fail(QStringLiteral("the disk is locked by the process %1").arg(owner), true);
        // Stale: the owner is gone. Taken over, unless the file changed under us.
        if (readPid(path) == owner)
            QFile::remove(path);
    }
    return fail(QStringLiteral("cannot take over a stale lock in %1").arg(folder), false);
}

DiskLock::~DiskLock()
{
    if (readPid(m_path) == m_pid)
        QFile::remove(m_path);
}

} // namespace tacetos::helper
