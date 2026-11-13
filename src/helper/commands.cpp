// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "commands.h"

#include "helperconstants.h"
#include "protocol.h"

#include <QDir>
#include <QSet>

namespace tacetos::helper {

static const QList<CommandSpec> &commandTable()
{
    // Only the commands that are wired so far. The others of section 6.1 are added
    // with their code.
    using A = ArgType;
    const auto target = ArgSpec{QStringLiteral("target"), A::Target};
    const auto token = ArgSpec{QStringLiteral("scanToken"), A::String};
    const auto expected = ArgSpec{QStringLiteral("expectedState"), A::String};
    const auto bootFile = ArgSpec{QStringLiteral("bootloaderFile"), A::Path};
    const auto bootVersion = ArgSpec{QStringLiteral("bootloaderVersion"), A::String};
    const auto dataDir = ArgSpec{QStringLiteral("dataDir"), A::Path};
    const QList<ArgSpec> format = {target,
                                   token,
                                   {QStringLiteral("ipodName"), A::String},
                                   {QStringLiteral("typedName"), A::String},
                                   {QStringLiteral("volumeLabel"), A::String}};
    QList<ArgSpec> convert = format;
    convert.append(dataDir);
    static const QList<CommandSpec> table = {
        {QString(cmd::readState), {target}},
        {QString(cmd::backupFirmware), {target, token, dataDir}},
        {QString(cmd::installBootloader),
         {target, token, expected, bootFile, bootVersion, dataDir}},
        {QString(cmd::updateBootloader), {target, token, expected, bootFile, bootVersion, dataDir}},
        {QString(cmd::uninstallBootloader), {target, token, expected, dataDir}},
        {QString(cmd::restoreBackup),
         {target, token, {QStringLiteral("backupFile"), A::Path}, dataDir}},
        {QString(cmd::formatMusic), format},
        {QString(cmd::convertToFat32), convert},
        {QString(cmd::readDeviceSheet), {target}},
        {QString(cmd::installUdevRule), {}},
        {QString(cmd::cancel), {{QStringLiteral("request"), A::Integer}}},
    };
    return table;
}

const CommandSpec *findCommand(const QString &name)
{
    for (const CommandSpec &spec : commandTable()) {
        if (spec.name == name)
            return &spec;
    }
    return nullptr;
}

// Exactly the keys of `names`, nothing more, nothing less.
static QString checkKeys(const QJsonObject &obj, const QStringList &names, const QString &where)
{
    for (const QString &key : obj.keys()) {
        if (!names.contains(key))
            return QStringLiteral("%1: the argument \"%2\" is not taken").arg(where, key);
    }
    for (const QString &name : names) {
        if (!obj.contains(name))
            return QStringLiteral("%1: the argument \"%2\" is missing").arg(where, name);
    }
    return {};
}

static QString checkType(const QJsonValue &value, ArgType type, const QString &name)
{
    switch (type) {
    case ArgType::String:
        if (!value.isString())
            return QStringLiteral("\"%1\" is not a string").arg(name);
        break;
    case ArgType::Integer:
        if (!isInteger(value))
            return QStringLiteral("\"%1\" is not an integer").arg(name);
        break;
    case ArgType::Path:
        if (!value.isString())
            return QStringLiteral("\"%1\" is not a string").arg(name);
        if (!QDir::isAbsolutePath(value.toString()))
            return QStringLiteral("\"%1\" is not an absolute path").arg(name);
        break;
    case ArgType::Target:
        if (!value.isObject())
            return QStringLiteral("\"%1\" is not an object").arg(name);
        return checkTargetShape(value.toObject());
    }
    return {};
}

QString checkArguments(const CommandSpec &spec, const QJsonObject &args)
{
    QStringList names;
    for (const ArgSpec &arg : spec.args)
        names.append(arg.name);
    QString why = checkKeys(args, names, spec.name);
    if (!why.isEmpty())
        return why;
    for (const ArgSpec &arg : spec.args) {
        why = checkType(args.value(arg.name), arg.type, arg.name);
        if (!why.isEmpty())
            return QStringLiteral("%1: %2").arg(spec.name, why);
    }
    return {};
}

QString checkTargetShape(const QJsonObject &target)
{
    const QJsonValue kind = target.value(QStringLiteral("kind"));
    if (!kind.isString())
        return QStringLiteral("target: \"kind\" is missing or not a string");
    const QString k = kind.toString();
    QList<ArgSpec> fields;
    if (k == QLatin1String("disk")) {
        fields = {{QStringLiteral("node"), ArgType::String},
                  {QStringLiteral("sizeBytes"), ArgType::Integer},
                  {QStringLiteral("serial"), ArgType::String},
                  {QStringLiteral("port"), ArgType::String}};
    } else if (k == QLatin1String("image")) {
        fields = {{QStringLiteral("path"), ArgType::Path},
                  {QStringLiteral("sectorSize"), ArgType::Integer}};
    } else {
        return QStringLiteral("target: \"kind\" is neither disk nor image");
    }
    QStringList names = {QStringLiteral("kind")};
    for (const ArgSpec &f : fields)
        names.append(f.name);
    QString why = checkKeys(target, names, QStringLiteral("target"));
    if (!why.isEmpty())
        return why;
    for (const ArgSpec &f : fields) {
        why = checkType(target.value(f.name), f.type, QStringLiteral("target.") + f.name);
        if (!why.isEmpty())
            return why;
    }
    return {};
}

} // namespace tacetos::helper
