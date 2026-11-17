// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>

namespace tacetos::helper {
class HelperClient;
}

// The commands a step of the wizard sends to the helper
// (docs/specs/helper.md sections 3.3, 6.12 and 10).

namespace tacetos::wizard {

// The answer of the helper to one command.
struct HelperAnswer
{
    QString code;
    QString detail;
    QJsonObject data;
    bool ok() const { return code.isEmpty(); }
};

// What the caller wants to hear while a command runs.
struct HelperWatch
{
    // Each `progress` event, in the thread of the caller.
    std::function<void(const QJsonObject &event)> progress;
    // Looked at while the command runs: once it says yes, `cancel` is sent
    // (helper.md section 6.12). The command then ends with `cancelled`, or goes
    // on to its end after its first write.
    std::function<bool()> stop;
};

// Sends one command and waits for its result. May be called from any thread:
// from another one than the client's, the requests are sent in the thread of
// the client, which must run its loop.
HelperAnswer callHelper(helper::HelperClient &client, const QString &cmd, const QJsonObject &args,
                        const QString &port = QString(), const HelperWatch &watch = HelperWatch());

// The commands about one iPod: the target is added to the arguments.
class HelperCommands
{
public:
    virtual ~HelperCommands() = default;
    virtual HelperAnswer call(const QString &cmd, const QJsonObject &args,
                              const HelperWatch &watch = HelperWatch()) = 0;
    // `read-state` for a new scan token (helper.md section 5.3), then `cmd`
    // with it.
    HelperAnswer callWithToken(const QString &cmd, QJsonObject args,
                               const HelperWatch &watch = HelperWatch());
};

// The real one, through the client of the helper.
class ClientHelperCommands : public HelperCommands
{
public:
    ClientHelperCommands(helper::HelperClient &client, const QJsonObject &target,
                         const QString &port = QString());
    HelperAnswer call(const QString &cmd, const QJsonObject &args,
                      const HelperWatch &watch = HelperWatch()) override;

private:
    helper::HelperClient &m_client;
    QJsonObject m_target;
    QString m_port;
};

} // namespace tacetos::wizard
