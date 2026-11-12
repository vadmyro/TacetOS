// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "fakedfudevice.h"

#include "constants.h"

namespace tacetos::installer {

FakeDfuDevice::FakeDfuDevice(const FakeDfuScript &script)
    : m_script(script), m_state(script.startState)
{
}

bool FakeDfuDevice::open(QString *error)
{
    ++m_opens;
    if (m_script.openFails) {
        if (error)
            *error = QStringLiteral("access denied");
        return false;
    }
    return true;
}

bool FakeDfuDevice::reset(QString *error)
{
    ++m_resets;
    if (!m_gone && m_script.end != FakeDfuEnd::WaitReset) {
        m_errors << QStringLiteral("reset without state 8");
    }
    m_gone = true;
    Q_UNUSED(error)
    return true;
}

UsbControlResult FakeDfuDevice::fail(const QString &why) const
{
    UsbControlResult r;
    r.error = why;
    r.code = m_script.requestCode;
    return r;
}

UsbControlResult FakeDfuDevice::answer(const QByteArray &data) const
{
    UsbControlResult r;
    r.ok = true;
    r.data = data;
    return r;
}

QByteArray FakeDfuDevice::statusAnswer(int status, int state) const
{
    QByteArray b(kDfuStatusBytes, '\0');
    b[0] = char(status);
    b[1] = char(m_script.pollMs & 0xFF);
    b[2] = char((m_script.pollMs >> 8) & 0xFF);
    b[3] = char((m_script.pollMs >> 16) & 0xFF);
    b[4] = char(state);
    return b;
}

UsbControlResult FakeDfuDevice::control(const UsbControlRequest &request, const QByteArray &out,
                                        int timeoutMs)
{
    m_timeouts << timeoutMs;
    if (m_gone)
        return fail(QStringLiteral("no such device"));

    if (request.type == kDfuTypeOut && request.length != out.size())
        m_errors << QStringLiteral("length %1 for %2 bytes").arg(request.length).arg(out.size());

    switch (request.request) {
    case kDfuGetState:
        if (request.type != kDfuTypeIn || request.length != 1 || request.value != 0)
            m_errors << QStringLiteral("bad GETSTATE");
        return answer(QByteArray(1, char(m_state)));

    case kDfuClrStatus:
        if (request.type != kDfuTypeOut || request.length != 0 || request.value != 0)
            m_errors << QStringLiteral("bad CLRSTATUS");
        if (m_state == kDfuStateError && m_script.clearWorks) {
            m_state = kDfuStateIdle;
            m_status = 0;
        }
        return answer({});

    case kDfuAbort:
        if (request.type != kDfuTypeOut || request.length != 0 || request.value != 0)
            m_errors << QStringLiteral("bad ABORT");
        // In dfuERROR only CLRSTATUS leaves the state, as with the real ROM.
        if (m_state != kDfuStateError) {
            m_state = kDfuStateIdle;
            m_nextBlock = 0;
            m_received.clear();
        }
        return answer({});

    case kDfuDnload: {
        if (request.type != kDfuTypeOut)
            m_errors << QStringLiteral("bad DNLOAD type");
        if (m_state != kDfuStateIdle && m_state != kDfuStateDnloadIdle) {
            m_errors << QStringLiteral("DNLOAD in state %1").arg(m_state);
            return fail(QStringLiteral("stall"));
        }
        if (request.value != m_nextBlock)
            m_errors << QStringLiteral("block %1, expected %2").arg(request.value).arg(m_nextBlock);
        if (m_script.timeoutAtBlock == m_nextBlock) {
            m_script.timeoutAtBlock = -1;
            return fail(QStringLiteral("timeout"));
        }
        ++m_nextBlock;
        if (out.isEmpty()) {
            m_ended = true;
            m_state = kDfuStateManifest;
            m_status = m_script.endStatus;
            if (m_status != 0)
                m_state = kDfuStateError;
            return answer({});
        }
        if (m_ended)
            m_errors << QStringLiteral("data after the end");
        if (out.size() > kDfuBlockBytes)
            m_errors << QStringLiteral("block of %1 bytes").arg(out.size());
        m_received.append(out);
        m_state = 3;
        m_busyLeft = request.value == m_script.busyBlock ? m_script.busyAnswers : 0;
        return answer({});
    }

    case kDfuGetStatus: {
        if (request.type != kDfuTypeIn || request.length != kDfuStatusBytes || request.value != 0)
            m_errors << QStringLiteral("bad GETSTATUS");
        if (m_state == 3 || m_state == kDfuStateDnBusy) {
            if (m_busyLeft > 0) {
                --m_busyLeft;
                m_state = kDfuStateDnBusy;
            } else if (m_script.errorAtBlock == m_nextBlock - 1) {
                m_state = kDfuStateError;
                m_status = 3; // errWRITE
            } else {
                m_state = kDfuStateDnloadIdle;
            }
            return answer(statusAnswer(m_status, m_state));
        }
        if (m_ended && m_state == kDfuStateManifest) {
            if (!m_manifestSeen) {
                m_manifestSeen = true;
                return answer(statusAnswer(0, kDfuStateManifest));
            }
            switch (m_script.end) {
            case FakeDfuEnd::Gone:
                m_gone = true;
                return fail(QStringLiteral("no such device"));
            case FakeDfuEnd::WaitReset:
                m_state = kDfuStateWaitReset;
                return answer(statusAnswer(0, kDfuStateWaitReset));
            case FakeDfuEnd::Verify:
                m_state = kDfuStateError;
                m_status = kDfuStatusErrVerify;
                return answer(statusAnswer(m_status, m_state));
            case FakeDfuEnd::Stuck:
                return answer(statusAnswer(0, kDfuStateManifest));
            }
        }
        return answer(statusAnswer(m_status, m_state));
    }
    }
    m_errors << QStringLiteral("request %1").arg(request.request);
    return fail(QStringLiteral("stall"));
}

} // namespace tacetos::installer
