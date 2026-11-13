// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "udevrule.h"

#include "helperconstants.h"
#include "udevruletext.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

namespace tacetos::helper {

namespace {

class QProcessRunner : public ProcessRunner
{
public:
    int run(const QString &program, const QStringList &arguments, QString *error) override
    {
        QProcess process;
        process.start(program, arguments);
        // The user may take a long time to type a password: no time limit.
        if (!process.waitForStarted()) {
            if (error)
                *error = process.errorString();
            return -1;
        }
        process.waitForFinished(-1);
        if (process.exitStatus() != QProcess::NormalExit) {
            if (error)
                *error = QStringLiteral("the program crashed");
            return -1;
        }
        return process.exitCode();
    }
};

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

UdevOutcome refuse(const QLatin1String &code, const QString &detail)
{
    UdevOutcome o;
    o.code = QString(code);
    o.detail = detail;
    return o;
}

} // namespace

QString udevRuleText()
{
    return QString::fromUtf8(kUdevRuleText);
}

QString udevInstallScript()
{
    return QStringLiteral(
               "set -e; install -m 0644 -o root -g root \"$1\" %1; udevadm control --reload-rules")
        .arg(kUdevRulesFile);
}

QStringList pkexecArguments(const QString &ruleCopyPath)
{
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"), udevInstallScript(),
            QStringLiteral("tacetos-udev"), ruleCopyPath};
}

UdevOutcome installUdevRule(const UdevEnv &env)
{
    const QByteArray rule = udevRuleText().toUtf8();
    // Step 1: already there, nothing is asked.
    if (readAll(env.rulesFile) == rule) {
        UdevOutcome o;
        o.ok = true;
        o.alreadyInstalled = true;
        return o;
    }
    if (env.pkexecPath.isEmpty() || !env.runner)
        return refuse(err::unsupportedSystem, QStringLiteral("pkexec is not installed"));

    // Step 2: a copy in a folder of the helper, then one call of pkexec.
    QTemporaryDir folder(env.tempBase.isEmpty() ? QString()
                                                : env.tempBase + QStringLiteral("/tacetos-XXXXXX"));
    if (!folder.isValid())
        return refuse(err::udevFailed, QStringLiteral("no temporary folder"));
    const QString copy = folder.filePath(QStringLiteral("70-tacetos-dfu.rules"));
    QFile f(copy);
    if (!f.open(QIODevice::WriteOnly) || f.write(rule) != rule.size())
        return refuse(err::udevFailed,
                      QStringLiteral("the rule cannot be written to the temporary folder"));
    f.close();

    QString error;
    const int code = env.runner->run(env.pkexecPath, pkexecArguments(copy), &error);
    // Step 3.
    if (code == 126)
        return refuse(err::cancelled, QStringLiteral("the password window was closed"));
    if (code == 127)
        return refuse(err::permissionDenied, QStringLiteral("the authorisation was refused"));
    if (code != 0)
        return refuse(err::udevFailed,
                      QStringLiteral("pkexec ended with code %1 %2").arg(code).arg(error));
    // Step 4: read back.
    if (readAll(env.rulesFile) != rule)
        return refuse(err::udevFailed,
                      QStringLiteral("the rule in the system is not the one that was sent"));
    UdevOutcome o;
    o.ok = true;
    return o;
}

std::unique_ptr<ProcessRunner> makeProcessRunner()
{
    return std::make_unique<QProcessRunner>();
}

} // namespace tacetos::helper
