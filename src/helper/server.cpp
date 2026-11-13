// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "server.h"

#include "commands.h"
#include "dfuimage.h"
#include "diskcmds.h"
#include "helperconstants.h"
#include "lock.h"
#include "readstate.h"
#include "sectordevice.h"
#include "target.h"
#include "writecmds.h"

#include <thread>

namespace tacetos::helper {

Server::Server(ServerIo io, QString lockFolder, SystemLayer *system)
    : m_io(std::move(io)), m_lockFolder(std::move(lockFolder)), m_system(system)
{
}

bool Server::send(const QJsonObject &event)
{
    if (!m_io.write(eventLine(event)))
        m_outputOk = false;
    return m_outputOk;
}

void Server::startPump()
{
    m_pump = std::make_shared<Pump>();
    // The thread may outlive the server when the input never ends: it only holds
    // what it needs.
    std::thread([pump = m_pump, readByte = m_io.readByte] {
        LineReader reader(readByte);
        for (;;) {
            LineReader::Item item = reader.next();
            const bool end = item.kind == LineReader::Kind::End;
            {
                std::lock_guard<std::mutex> lock(pump->mutex);
                pump->items.push_back(std::move(item));
            }
            pump->wake.notify_one();
            if (end)
                return;
        }
    }).detach();
}

LineReader::Item Server::nextItem()
{
    std::unique_lock<std::mutex> lock(m_pump->mutex);
    m_pump->wake.wait(lock, [&] { return !m_pump->items.empty(); });
    LineReader::Item item = std::move(m_pump->items.front());
    m_pump->items.pop_front();
    return item;
}

bool Server::tryItem(LineReader::Item &item)
{
    std::lock_guard<std::mutex> lock(m_pump->mutex);
    if (m_pump->items.empty())
        return false;
    item = std::move(m_pump->items.front());
    m_pump->items.pop_front();
    return true;
}

int Server::run()
{
    const bool classic = !installer::builtInDfuPartsDir().isEmpty();
    if (!send(makeHello(classic, TACETOS_HELPER_HAS_USB != 0)))
        return kExitOutputClosed;

    startPump();
    for (;;) {
        if (m_inputEnded)
            return kExitNormal;
        const LineReader::Item item = nextItem();
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
    const QString &name = parsed.request.cmd;
    if (name == cmd::readState || name == cmd::readDeviceSheet || isLegacyCommand(name))
        return handleDiskCommand(parsed.request);
    if (name == cmd::installUdevRule) {
        const qint64 id = parsed.request.id;
        send(makeAccepted(id));
        const CommandOutcome outcome = runInstallUdevRule(m_system);
        return send(outcome.ok ? makeResultOk(id, outcome.data)
                               : makeResultError(id, outcome.error, outcome.detail, outcome.data));
    }
    if (name == cmd::cancel)
        return handleCancel(parsed.request);
    // The table and this dispatch are changed together.
    return send(makeResultError(parsed.request.id, err::unknownCommand,
                                QStringLiteral("no such command: ") + name));
}

bool Server::handleCancel(const Request &request)
{
    const qint64 id = request.id;
    const qint64 target = qint64(request.args.value(QStringLiteral("request")).toDouble());
    if (m_runningId == 0 || target != m_runningId)
        return send(
            makeResultError(id, err::noSuchRequest, QStringLiteral("no such request is running")));
    if (!m_cancellable)
        return send(makeResultError(id, err::tooLate, QStringLiteral("the first write was made")));
    m_cancelAsked = true;
    return send(makeResultOk(id, {}));
}

void Server::pollInput()
{
    LineReader::Item item;
    while (tryItem(item)) {
        if (item.kind == LineReader::Kind::End) {
            // Section 3.7: before the first write, the end of the input is a cancel.
            m_inputEnded = true;
            if (m_cancellable)
                m_cancelAsked = true;
            return;
        }
        if (item.kind == LineReader::Kind::TooLong) {
            send(makeResultError(
                0, err::badRequest,
                QStringLiteral("the line is longer than %1 bytes").arg(kMaxRequestLine)));
            continue;
        }
        const ParseResult parsed = parseRequest(item.line, m_lastId);
        if (!parsed.ok) {
            if (parsed.refusal.id > m_lastId)
                m_lastId = parsed.refusal.id;
            send(makeResultError(parsed.refusal.id, parsed.refusal.code, parsed.refusal.detail));
            continue;
        }
        m_lastId = parsed.request.id;
        if (parsed.request.cmd == cmd::cancel)
            handleCancel(parsed.request);
        else
            send(makeResultError(parsed.request.id, err::busy,
                                 QStringLiteral("another command runs")));
    }
}

// What the commands of writecmds.cpp call back, as events of the request.
class Server::Running : public CommandHooks
{
public:
    Running(Server &server, qint64 id) : m_server(server), m_id(id) {}

    void log(const QString &level, const QString &text) override
    {
        m_server.send(makeLog(m_id, level, text));
    }
    void stage(const QString &name) override
    {
        m_stage = name;
        m_server.send(makeProgress(m_id, name, 0, 0, m_server.m_cancellable));
    }
    void progress(qint64 done, qint64 total) override
    {
        m_server.send(makeProgress(m_id, m_stage, done, total, m_server.m_cancellable));
    }
    bool cancelRequested() override
    {
        m_server.pollInput();
        return m_server.m_cancelAsked && m_server.m_cancellable;
    }
    bool beforeFirstWrite() override
    {
        m_server.pollInput();
        if (m_server.m_cancelAsked)
            return false;
        m_server.m_cancellable = false;
        m_stage = QStringLiteral("write");
        m_server.send(makeProgress(m_id, m_stage, 0, 0, false));
        return true;
    }

private:
    Server &m_server;
    qint64 m_id;
    QString m_stage;
};

bool Server::handleDiskCommand(const Request &request)
{
    const qint64 id = request.id;
    auto push = [&](const QJsonObject &event) { send(event); };

    // Step 3: the typed confirmation, before the disk is even looked at.
    const QString mismatch = checkConfirmation(request.cmd, request.args);
    if (!mismatch.isEmpty())
        return send(makeResultError(id, err::confirmationMismatch, mismatch));

    // Step 4: the target, and the data folder.
    const QJsonObject targetObject = request.args.value(QStringLiteral("target")).toObject();
    const bool realDisk =
        targetObject.value(QStringLiteral("kind")).toString() == QLatin1String("disk");
    const bool diskCommand = request.cmd == cmd::readState || request.cmd == cmd::readDeviceSheet;
    ResolvedTarget target;
    RealDisk disk;
    if (realDisk && m_system) {
        // A real disk is found again by the helper and judged (sections 7.1, 11.2),
        // with the data folder, which must not be on it.
        disk = checkRealDisk(targetObject, request.args.value(QStringLiteral("dataDir")).toString(),
                             *m_system);
        if (!disk.ok)
            return send(makeResultError(id, disk.code, disk.detail));
        target.ok = true;
        target.lockKey = disk.lockKey;
    } else if (request.cmd == cmd::readDeviceSheet) {
        // An image file has no device to ask (section 6.3).
        return send(
            makeResultError(id, err::badTarget, QStringLiteral("an image has no device sheet")));
    } else {
        target = resolveTarget(targetObject);
        if (!target.ok)
            return send(makeResultError(id, target.code, target.detail));
    }
    if (request.args.contains(QStringLiteral("dataDir"))) {
        const QString why = checkDataDir(request.args.value(QStringLiteral("dataDir")).toString());
        if (!why.isEmpty())
            return send(makeResultError(id, err::badDataDir, why));
    }
    push(makeAccepted(id));

    // Step 5: the lock, released just before the result is written, so that the
    // disk is free for whoever reads the result.
    QString why;
    bool held = false;
    std::unique_ptr<DiskLock> lock = DiskLock::acquire(m_lockFolder, target.lockKey, &why, &held);
    if (!lock) {
        push(makeResultError(id, held ? err::diskLocked : err::openFailed, why));
        return m_outputOk;
    }

    m_runningId = id;
    m_cancellable = true;
    m_cancelAsked = false;
    CommandOutcome outcome;
    if (realDisk && !diskCommand) {
        Running hooks(*this, id);
        hooks.log(QStringLiteral("info"), QStringLiteral("%1 on a real disk").arg(request.cmd));
        outcome = runLegacyCommandOnDisk(request.cmd, request.args, *m_system, disk, hooks);
    } else if (realDisk) {
        // Step 6, for reading only: the helper opens nothing else (section 11.1).
        push(makeProgress(id, QStringLiteral("open"), 0, 0, true));
        auto log = [&](const QString &level, const QString &text) {
            push(makeLog(id, level, text));
        };
        outcome = request.cmd == cmd::readState
                      ? readStateOnDisk(*m_system, disk, log,
                                        [&](const QString &stage) {
                                            push(makeProgress(id, stage, 0, 0, true));
                                        })
                      : readSheetOnDisk(*m_system, disk, log);
    } else if (request.cmd == cmd::readState) {
        // Step 6: the disk, for reading only.
        push(makeProgress(id, QStringLiteral("open"), 0, 0, true));
        installer::FileSectorDevice dev(target.path, target.sectorSize,
                                        installer::FileSectorDevice::Mode::ReadOnly);
        const installer::IoResult opened = dev.open();
        if (!opened) {
            outcome.error = err::openFailed;
            outcome.detail = opened.error;
        } else {
            push(makeLog(id, QStringLiteral("info"),
                         QStringLiteral("read-state on the image %1").arg(target.path)));
            outcome = readState(
                dev,
                [&](const QString &level, const QString &text) { push(makeLog(id, level, text)); },
                [&](const QString &stage) { push(makeProgress(id, stage, 0, 0, true)); });
        }
    } else {
        Running hooks(*this, id);
        hooks.log(QStringLiteral("info"),
                  QStringLiteral("%1 on the image %2").arg(request.cmd, target.path));
        outcome = runLegacyCommand(request.cmd, request.args, target, hooks);
    }
    m_runningId = 0;
    lock.reset();
    push(outcome.ok ? makeResultOk(id, outcome.data)
                    : makeResultError(id, outcome.error, outcome.detail, outcome.data));
    return m_outputOk;
}

} // namespace tacetos::helper
