// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "dfuprotocol.h"

#include <QByteArray>
#include <QList>
#include <QString>

// The iPod's side of docs/specs/bootloader-classic.md section 5, played from a
// script, behind the UsbDfuLink of the helper. It also checks what it is sent:
// block numbers, block sizes, and what each request asks for.

namespace tacetos::installer {

// What the iPod does after the empty block and the first GETSTATUS.
enum class FakeDfuEnd {
    Gone,      // the payload runs: the next request fails (the normal case)
    WaitReset, // state 8, and the device is gone after the reset
    Verify,    // status errVERIFY at step 6
    Stuck,     // state 7 again
};

struct FakeDfuScript
{
    UsbDeviceId id{0x05AC, 0x1223};
    bool openFails = false;
    // What the access layer says for a failed open, and for a request that fails.
    UsbAccessError openCode = UsbAccessError::AccessDenied;
    UsbAccessError requestCode = UsbAccessError::None;
    // The state the device is in when opened: 2, or 10 for dfuERROR.
    int startState = 2;
    // Whether CLRSTATUS brings the device back to dfuIDLE.
    bool clearWorks = true;
    // The DNLOAD of this block is answered by `busyAnswers` times dfuDNBUSY.
    int busyBlock = 0;
    int busyAnswers = 0;
    int pollMs = 5;
    // The request of this DNLOAD fails (a timeout); -1 for never. The device
    // answers again afterwards, as after a stalled endpoint.
    int timeoutAtBlock = -1;
    // The status after this block is dfuERROR with errWRITE; -1 for never.
    int errorAtBlock = -1;
    // The status after the empty block: 0, or 10 (errFIRMWARE).
    int endStatus = 0;
    FakeDfuEnd end = FakeDfuEnd::Gone;
};

class FakeDfuDevice : public UsbDfuLink
{
public:
    explicit FakeDfuDevice(const FakeDfuScript &script = {});

    UsbDeviceId deviceId() const override { return m_script.id; }
    bool open(QString *error) override;
    UsbAccessError openError() const override { return m_script.openCode; }
    UsbControlResult control(const UsbControlRequest &request, const QByteArray &out,
                             int timeoutMs) override;
    bool reset(QString *error) override;
    void close() override { ++m_closes; }

    // What the device received in DNLOAD, block after block.
    QByteArray received() const { return m_received; }
    // Mistakes of the host that a real ROM would not accept; empty when none.
    QStringList protocolErrors() const { return m_errors; }
    int opens() const { return m_opens; }
    int closes() const { return m_closes; }
    int resets() const { return m_resets; }
    // The timeouts the host passed to `control`.
    QList<int> timeouts() const { return m_timeouts; }

private:
    UsbControlResult fail(const QString &why) const;
    UsbControlResult answer(const QByteArray &data) const;
    QByteArray statusAnswer(int status, int state) const;

    FakeDfuScript m_script;
    int m_state;
    int m_status = 0;
    bool m_gone = false;
    bool m_manifestSeen = false;
    int m_nextBlock = 0;
    int m_busyLeft = 0;
    bool m_ended = false;
    QByteArray m_received;
    QStringList m_errors;
    QList<int> m_timeouts;
    int m_opens = 0;
    int m_closes = 0;
    int m_resets = 0;
};

} // namespace tacetos::installer
