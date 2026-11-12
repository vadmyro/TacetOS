// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// The closed list of commands (docs/specs/helper.md section 6.1) and the
// arguments of each. A command that is not in the table does not exist: adding
// one means changing the spec first.

namespace tacetos::helper {

enum class ArgType {
    String,
    Integer,
    // A string that is an absolute path (section 3.5).
    Path,
    // The object of section 5.1.
    Target,
};

struct ArgSpec
{
    QString name;
    ArgType type;
};

struct CommandSpec
{
    QString name;
    QList<ArgSpec> args;
};

// Null for a name that is not in the list.
const CommandSpec *findCommand(const QString &name);

// Section 3.5: exactly the arguments of the command, each of its type. An empty
// `why` when the arguments are fine.
QString checkArguments(const CommandSpec &spec, const QJsonObject &args);

// The shape of a target object (section 5.1), types only: whether the file is
// really there is for `resolveTarget`. An empty string when it is fine.
QString checkTargetShape(const QJsonObject &target);

} // namespace tacetos::helper
