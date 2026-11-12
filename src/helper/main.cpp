// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "helperconstants.h"
#include "lock.h"
#include "server.h"

#include <cstdio>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

// tacet-helper: takes no option (docs/specs/helper.md section 2.3). Requests on
// the standard input, events on the standard output, one JSON object per line.
int main(int argc, char **)
{
    using namespace tacetos::helper;
    if (argc > 1) {
        std::fputs("tacet-helper takes no argument\n", stderr);
        return kExitCommandLine;
    }
#if defined(Q_OS_WIN)
    // A line feed is one byte, on every system (section 3.1).
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    // A closed output is reported by the write, not by a signal (section 3.7).
    std::signal(SIGPIPE, SIG_IGN);
#endif

    ServerIo io;
    io.readByte = [] { return std::getc(stdin); };
    io.write = [](const QByteArray &bytes) {
        return std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout) ==
                   size_t(bytes.size()) &&
               std::fflush(stdout) == 0;
    };
    Server server(io, DiskLock::defaultFolder());
    return server.run();
}
