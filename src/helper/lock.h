// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <memory>

// The lock on each disk (docs/specs/helper.md section 8, and rule 10 of
// bootloader-legacy.md section 24): one file per locked thing, holding the
// process id of its owner. A lock left by a process that does not run any more
// is stale and is taken over. A lock that is held is a refusal, never a wait.

namespace tacetos::helper {

class DiskLock
{
public:
    // `$XDG_RUNTIME_DIR/tacetos/locks/` on Linux, or the temporary folder when the
    // variable is not set (and on the other systems).
    static QString defaultFolder();

    // The file of the lock for `key` in `folder`. `key` is the canonical path of an
    // image file, the device number of a real disk, or `05ac:1223` for DFU mode.
    static QString lockFilePath(const QString &folder, const QString &key);

    // Whether a process with this id is running.
    static bool processRuns(qint64 pid);

    // Takes the lock. Null when it is held by a running process (`why` says which)
    // or when the folder cannot be used.
    static std::unique_ptr<DiskLock> acquire(const QString &folder, const QString &key,
                                             QString *why = nullptr, bool *held = nullptr);

    // Releases the lock, if the file is still ours.
    ~DiskLock();
    DiskLock(const DiskLock &) = delete;
    DiskLock &operator=(const DiskLock &) = delete;

private:
    explicit DiskLock(const QString &path, qint64 pid) : m_path(path), m_pid(pid) {}

    QString m_path;
    qint64 m_pid;
};

} // namespace tacetos::helper
