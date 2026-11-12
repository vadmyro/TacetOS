// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "server.h"

#include "commands.h"
#include "dfuimage.h"
#include "helperconstants.h"
#include "lock.h"
#include "readstate.h"
#include "sectordevice.h"
#include "target.h"

namespace tacetos::helper {

Server::Server(ServerIo io, QString lockFolder)
    : m_io(std::move(io)), m_lockFolder(std::move(lockFolder))
{
}

bool Server::send(const QJsonObject &event)
{
    return m_io.write(eventLine(event));
}

int Server::run()
{
    const bool classic = !installer::builtInDfuPartsDir().isEmpty();
    if (!send(makeHello(classic, TACETOS_HELPER_HAS_USB != 0)))
        return kExitOutputClosed;

    LineReader reader(m_io.readByte);
    for (;;) {
        const LineReader::Item item = reader.next();
        if (item.kind == LineReader::Kind::End)
            return kExitNormal;
        if (!handleLine(item))
            return kExitOutputClosed;
    }
}

bool Server::handleLine(const LineReader::Item &item)
{
    if (item.kind == LineReader::Kind::TooLong)
        return send(makeResultError(
            0, err::badRequest,
            QStringLiteral("the line is longer than %1 bytes").arg(kMaxRequestLine)));

    const ParseResult parsed = parseRequest(item.line, m_lastId);
    if (!parsed.ok) {
        // An id that was read is counted, even in a refused request.
        if (parsed.refusal.id > m_lastId)
            m_lastId = parsed.refusal.id;
        return send(makeResultError(parsed.refusal.id, parsed.refusal.code, parsed.refusal.detail));
    }
    m_lastId = parsed.request.id;
    if (parsed.request.cmd == cmd::readState)
        return handleReadState(parsed.request);
    // The table and this dispatch are changed together.
    return send(makeResultError(parsed.request.id, err::unknownCommand,
                                QStringLiteral("no such command: ") + parsed.request.cmd));
}

bool Server::handleReadState(const Request &request)
{
    const qint64 id = request.id;
    bool alive = true;
    auto push = [&](const QJsonObject &event) { alive = send(event) && alive; };

    // Step 4: the target.
    const ResolvedTarget target =
        resolveTarget(request.args.value(QStringLiteral("target")).toObject());
    if (!target.ok)
        return send(makeResultError(id, target.code, target.detail));
    push(makeAccepted(id));

    // Step 5: the lock, kept until the result is written.
    QString why;
    bool held = false;
    const std::unique_ptr<DiskLock> lock =
        DiskLock::acquire(m_lockFolder, target.lockKey, &why, &held);
    if (!lock) {
        push(makeResultError(id, held ? err::diskLocked : err::openFailed, why));
        return alive;
    }

    // Step 6: the disk, for reading only.
    push(makeProgress(id, QStringLiteral("open"), 0, 0, true));
    installer::FileSectorDevice dev(target.path, target.sectorSize,
                                    installer::FileSectorDevice::Mode::ReadOnly);
    const installer::IoResult opened = dev.open();
    if (!opened) {
        push(makeResultError(id, err::openFailed, opened.error));
        return alive;
    }

    push(makeLog(id, QStringLiteral("info"),
                 QStringLiteral("read-state on the image %1").arg(target.path)));
    const CommandOutcome outcome = readState(
        dev, [&](const QString &level, const QString &text) { push(makeLog(id, level, text)); },
        [&](const QString &stage) { push(makeProgress(id, stage, 0, 0, true)); });
    push(outcome.ok ? makeResultOk(id, outcome.data)
                    : makeResultError(id, outcome.error, outcome.detail));
    return alive;
}

} // namespace tacetos::helper
