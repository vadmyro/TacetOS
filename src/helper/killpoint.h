// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QtGlobal>

// Test builds only (docs/specs/helper.md section 15.2): a way to kill the helper
// at a chosen point of a command, to see what the iPod holds after a cut in the
// middle of an operation. Without `TACETOS_TEST_KILLPOINTS` every function below
// does nothing, and no environment variable is read: the normal helper has no
// trace of this. Only the program `tacet-helper-killtest` is built with it, and
// only for the tests.
//
// The variable is `TACETOS_TEST_KILL=<point>:<n>`. The n-th time the point is
// reached in a command, the process kills itself with SIGKILL:
//
// - `read-before`: just before the n-th read of the disk;
// - `write-before`: just before the n-th write, which is not made;
// - `write-torn`: the first half of the n-th write is made, then the kill;
// - `write-after`: right after the n-th write, before any flush;
// - `flush-after`: right after the n-th flush.

#if defined(TACETOS_TEST_KILLPOINTS)
#include <QByteArray>
#include <csignal>
#include <cstdlib>

namespace tacetos::helper {

class KillPoints
{
public:
    KillPoints()
    {
        const QByteArray spec = qgetenv("TACETOS_TEST_KILL");
        const int colon = spec.indexOf(':');
        if (colon <= 0)
            return;
        m_point = spec.left(colon);
        m_at = spec.mid(colon + 1).toInt();
    }

    void beforeRead() { reach("read-before", m_reads); }
    void beforeWrite() { reach("write-before", m_writes); }
    // Sectors of the next write to make before the kill, 0 when it is not a torn
    // write point. The caller makes them, then calls `die()`.
    qint64 tornSectors(qint64 count)
    {
        ++m_torn;
        if (m_point != "write-torn" || m_torn != m_at)
            return 0;
        return qMax<qint64>(1, count / 2);
    }
    void afterWrite() { reach("write-after", m_afterWrites); }
    void afterFlush() { reach("flush-after", m_flushes); }

    [[noreturn]] static void die()
    {
        std::raise(SIGKILL);
        std::abort();
    }

private:
    void reach(const char *point, int &counter)
    {
        ++counter;
        if (m_point == point && counter == m_at)
            die();
    }

    QByteArray m_point;
    int m_at = 0;
    int m_reads = 0;
    int m_writes = 0;
    int m_torn = 0;
    int m_afterWrites = 0;
    int m_flushes = 0;
};

} // namespace tacetos::helper

#else

namespace tacetos::helper {

class KillPoints
{
public:
    void beforeRead() {}
    void beforeWrite() {}
    qint64 tornSectors(qint64) { return 0; }
    void afterWrite() {}
    void afterFlush() {}
    [[noreturn]] static void die() { qFatal("no kill point in this build"); }
};

} // namespace tacetos::helper

#endif
