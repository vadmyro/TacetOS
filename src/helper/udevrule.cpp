// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "udevrule.h"

#include "helperconstants.h"
#include "udevruletext.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <utility>

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

QString udevRuleTextWithoutPolkitLine()
{
    const QString text = udevRuleText();
    const qsizetype end = text.endsWith(QLatin1Char('\n')) ? text.size() - 1 : text.size();
    return text.left(text.lastIndexOf(QLatin1Char('\n'), end - 1) + 1);
}

QString polkitRuleText()
{
    return QStringLiteral(
        "// SPDX-License-Identifier: GPL-3.0-or-later\n"
        "// Copyright (C) 2026 vadmyro\n"
        "//\n"
        "// Opens an iPod for the user of a local, active session without a password\n"
        "// window. It answers for one action and one kind of drive: a removable USB\n"
        "// drive whose vendor is Apple and whose model starts with iPod. Every other\n"
        "// request goes on to the usual rules of the system.\n"
        "polkit.addRule(function(action, subject) {\n"
        "    if (action.id != \"org.freedesktop.udisks2.open-device\")\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    if (!subject.local || !subject.active)\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    if (action.lookup(\"drive.vendor\") != \"Apple\")\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    var model = action.lookup(\"drive.model\");\n"
        "    if (!model || model.indexOf(\"iPod\") != 0)\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    if (action.lookup(\"drive.removable\") != \"true\")\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    if (action.lookup(\"drive.removable.bus\") != \"usb\")\n"
        "        return polkit.Result.NOT_HANDLED;\n"
        "    return polkit.Result.YES;\n"
        "});\n");
}

QString udevInstallScriptFor(const QString &udevFile, const QString &polkitFile,
                             const QString &ownerOptions)
{
    // "$1" and "$2" are the copies of the two rules, "$1.short" the udev rule without
    // its last line. The folder of the polkit rules is the only thing looked at.
    return QStringLiteral("set -e; u=\"%1\"; p=\"%2\"; "
                          "if [ -d \"${p%/*}\" ]; then "
                          "install -m 0644 %3 \"$1\" \"$u\"; install -m 0644 %3 \"$2\" \"$p\"; "
                          "udevadm control --reload-rules; "
                          "cmp \"$1\" \"$u\" || exit 3; cmp \"$2\" \"$p\" || exit 3; "
                          "else "
                          "sed '$d' \"$1\" > \"$1.short\"; install -m 0644 %3 \"$1.short\" \"$u\"; "
                          "udevadm control --reload-rules; "
                          "cmp \"$1.short\" \"$u\" || exit 3; exit 4; "
                          "fi")
        .arg(udevFile, polkitFile, ownerOptions);
}

QString udevInstallScript()
{
    return udevInstallScriptFor(kUdevRulesFile, kPolkitRulesFile,
                                QStringLiteral("-o root -g root"));
}

QStringList pkexecArguments(const QString &udevCopyPath, const QString &polkitCopyPath)
{
    return {QStringLiteral("/bin/sh"),      QStringLiteral("-c"), udevInstallScript(),
            QStringLiteral("tacetos-udev"), udevCopyPath,         polkitCopyPath};
}

UdevOutcome installUdevRule(const UdevEnv &env)
{
    const QByteArray rule = udevRuleText().toUtf8();
    const QByteArray shortRule = udevRuleTextWithoutPolkitLine().toUtf8();
    const bool polkitFolder = QFileInfo(env.polkitRulesFile).dir().exists();
    // Step 1: already there, nothing is asked. Without the last line, the rule is
    // missing, unless the system has no folder of polkit rules.
    const QByteArray inPlace = readAll(env.rulesFile);
    if (inPlace == rule || (!polkitFolder && inPlace == shortRule)) {
        UdevOutcome o;
        o.ok = true;
        o.alreadyInstalled = true;
        o.polkit = inPlace == rule;
        return o;
    }
    if (env.pkexecPath.isEmpty() || !env.runner)
        return refuse(err::unsupportedSystem, QStringLiteral("pkexec is not installed"));

    // Step 2: a copy of each rule in a folder of the helper, then one call of pkexec.
    QTemporaryDir folder(env.tempBase.isEmpty() ? QString()
                                                : env.tempBase + QStringLiteral("/tacetos-XXXXXX"));
    if (!folder.isValid())
        return refuse(err::udevFailed, QStringLiteral("no temporary folder"));
    const QString udevCopy = folder.filePath(QStringLiteral("70-tacetos-dfu.rules"));
    const QString polkitCopy = folder.filePath(QStringLiteral("70-tacetos-ipod.rules"));
    const QByteArray polkitRule = polkitRuleText().toUtf8();
    for (const auto &[path, bytes] :
         {std::pair(udevCopy, rule), std::pair(polkitCopy, polkitRule)}) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
            return refuse(err::udevFailed,
                          QStringLiteral("a rule cannot be written to the temporary folder"));
    }

    QString error;
    const int code = env.runner->run(env.pkexecPath, pkexecArguments(udevCopy, polkitCopy), &error);
    // Step 3.
    if (code == 126)
        return refuse(err::cancelled, QStringLiteral("the password window was closed"));
    if (code == 127)
        return refuse(err::permissionDenied, QStringLiteral("the authorisation was refused"));
    if (code == 3)
        return refuse(err::udevFailed,
                      QStringLiteral("a rule in the system differs from the copy that was sent"));
    if (code != 0 && code != 4)
        return refuse(err::udevFailed,
                      QStringLiteral("pkexec ended with code %1 %2").arg(code).arg(error));
    // Step 4: read back.
    if (readAll(env.rulesFile) != (code == 4 ? shortRule : rule))
        return refuse(err::udevFailed,
                      QStringLiteral("the rule in the system is not the one that was sent"));
    UdevOutcome o;
    o.ok = true;
    o.polkit = code == 0;
    return o;
}

std::unique_ptr<ProcessRunner> makeProcessRunner()
{
    return std::make_unique<QProcessRunner>();
}

} // namespace tacetos::helper
