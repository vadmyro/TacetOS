// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QDateTime>
#include <QLatin1String>
#include <QString>

// One entry of the journal and the pure functions around it
// (docs/specs/device-state.md sections 3.1 and 3.2).

namespace tacetos::journal {

enum class Level {
    Info,
    Warning,
    Error,
};

enum class Domain {
    // The USB list.
    Usb,
    // Disks, volumes, marker files.
    Disk,
    // The decision of section 8 of the detection spec.
    Merge,
    // Operations.
    Op,
    // Everything else: start, stop, the journal itself.
    App,
};

struct Entry
{
    // UTC.
    QDateTime time;
    Level level = Level::Info;
    Domain domain = Domain::App;
    // The port path of the iPod it is about, empty for none.
    QString port;
    // One line of English, already through cleanText.
    QString text;
};

// `info`, `warning`, `error`.
QLatin1String levelName(Level level);

// `usb`, `disk`, `merge`, `op`, `app`.
QLatin1String domainName(Domain domain);

// Section 3.2: makes a text fit on one line of the journal. A control
// character becomes a space. `home`, the home folder of the account, becomes
// `~` wherever a path starts with it, in the forms of every system (a
// separator of either kind matches). The name of the account, when it is a
// whole path component elsewhere, becomes `<user>`. A text over `maxLength`
// is cut and ends with "...". `home` is given, never looked up, so that the
// function does not depend on the machine.
QString cleanText(const QString &text, const QString &home, int maxLength);

// Section 3.4: a port path never holds a space or a control character, so
// that a line can be cut on its first four spaces. Each one becomes `_`.
QString cleanPort(const QString &port);

// Section 3.4: `time level domain port text`, one space between the fields,
// `-` for no port. No line end. The text is expected to be clean already.
QString formatEntry(const Entry &entry);

} // namespace tacetos::journal
