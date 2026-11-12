// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "dfuprotocol.h"

#include "constants.h"

#include <QThread>
#include <algorithm>
#include <array>

namespace tacetos::installer {

namespace {

constexpr const char *kMessageInterrupted =
    "The transfer was interrupted. The iPod is not changed.";
constexpr const char *kMessageNotReady =
    "The iPod is not ready. Reset it and enter DFU mode again.";
constexpr const char *kMessageRefused = "The iPod refused the install file.";

std::array<quint32, 256> makeCrcTable()
{
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int bit = 0; bit < 8; ++bit)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        table[i] = c;
    }
    return table;
}

bool isWtf(quint16 product)
{
    return std::find(std::begin(kClassicWtfProductIds), std::end(kClassicWtfProductIds), product) !=
           std::end(kClassicWtfProductIds);
}

struct Status
{
    int status = 0;
    int pollMs = 0;
    int state = 0;
};

// One transfer: the steps of section 5, with the recovery of section 7.
class Transfer
{
public:
    Transfer(UsbDfuLink &link, const DfuSendOptions &options) : m_link(link), m_options(options) {}

    DfuTransferResult run(const QByteArray &image);

private:
    DfuTransferResult fail(DfuFailure failure, const char *message, const QString &detail = {});
    UsbControlResult send(quint8 type, quint8 request, quint16 value, const QByteArray &out,
                          quint16 asked);
    bool sendOut(quint8 request, quint16 value, const QByteArray &data = {});
    bool getState(int *state);
    bool getStatus(Status *status);
    void wait(int pollMs);
    // Step 3. `answered` tells a wrong state from a request that failed.
    bool checkIdle(bool *answered);
    // Steps 4.1 to 4.4: one block, and the wait until the device is done with it.
    bool sendBlock(int number, const QByteArray &block);
    DfuTransferResult recover(DfuFailure failure, const char *message, const QString &detail);

    UsbDfuLink &m_link;
    const DfuSendOptions &m_options;
    DfuTransferResult m_result;
};

UsbControlResult Transfer::send(quint8 type, quint8 request, quint16 value, const QByteArray &out,
                                quint16 asked)
{
    UsbControlRequest r;
    r.type = type;
    r.request = request;
    r.value = value;
    r.length = type == kDfuTypeIn ? asked : quint16(out.size());
    m_result.requests.append(r);
    UsbControlResult answer = m_link.control(r, out, kDfuRequestTimeoutMs);
    // The first code is the one that tells the cause: later requests of the recovery fail too.
    if (!answer.ok && m_result.access == UsbAccessError::None)
        m_result.access = answer.code;
    return answer;
}

bool Transfer::sendOut(quint8 request, quint16 value, const QByteArray &data)
{
    const UsbControlResult r = send(kDfuTypeOut, request, value, data, 0);
    if (!r.ok)
        m_result.detail = r.error;
    return r.ok;
}

bool Transfer::getState(int *state)
{
    const UsbControlResult r = send(kDfuTypeIn, kDfuGetState, 0, {}, 1);
    if (!r.ok) {
        m_result.detail = r.error;
        return false;
    }
    if (r.data.size() != 1) {
        m_result.detail = QStringLiteral("GETSTATE answered %1 bytes").arg(r.data.size());
        return false;
    }
    *state = quint8(r.data.at(0));
    return true;
}

bool Transfer::getStatus(Status *status)
{
    const UsbControlResult r = send(kDfuTypeIn, kDfuGetStatus, 0, {}, kDfuStatusBytes);
    if (!r.ok) {
        m_result.detail = r.error;
        return false;
    }
    if (r.data.size() != kDfuStatusBytes) {
        m_result.detail = QStringLiteral("GETSTATUS answered %1 bytes").arg(r.data.size());
        return false;
    }
    status->status = quint8(r.data.at(0));
    status->pollMs =
        quint8(r.data.at(1)) | (quint8(r.data.at(2)) << 8) | (quint8(r.data.at(3)) << 16);
    status->state = quint8(r.data.at(4));
    return true;
}

void Transfer::wait(int pollMs)
{
    const int ms = std::clamp(pollMs, kDfuPollMinMs, kDfuPollMaxMs);
    if (m_options.sleep)
        m_options.sleep(ms);
    else
        QThread::msleep(quint32(ms));
}

DfuTransferResult Transfer::fail(DfuFailure failure, const char *message, const QString &detail)
{
    m_result.ok = false;
    m_result.failure = failure;
    m_result.message = QString::fromLatin1(message);
    if (!detail.isEmpty())
        m_result.detail = detail;
    return m_result;
}

bool Transfer::checkIdle(bool *answered)
{
    *answered = false;
    int state = 0;
    if (!getState(&state))
        return false;
    if (state == kDfuStateError) {
        // Once only, then look again.
        if (!sendOut(kDfuClrStatus, 0) || !getState(&state))
            return false;
    }
    *answered = true;
    if (state != kDfuStateIdle) {
        m_result.detail = QStringLiteral("state %1").arg(state);
        return false;
    }
    return true;
}

// Section 7: ABORT if the device still answers, then GETSTATE, and CLRSTATUS if
// the state is dfuERROR. The answers do not change the failure that is reported.
DfuTransferResult Transfer::recover(DfuFailure failure, const char *message, const QString &detail)
{
    const QString kept = detail.isEmpty() ? m_result.detail : detail;
    if (sendOut(kDfuAbort, 0)) {
        int state = 0;
        if (getState(&state) && state == kDfuStateError)
            sendOut(kDfuClrStatus, 0);
    }
    m_link.close();
    return fail(failure, message, kept);
}

bool Transfer::sendBlock(int number, const QByteArray &block)
{
    if (!sendOut(kDfuDnload, quint16(number), block))
        return false;
    Status status;
    for (int tries = 1;; ++tries) {
        if (!getStatus(&status))
            return false;
        if (status.state != kDfuStateDnBusy)
            break;
        if (tries >= kDfuBusyMaxStatusRequests) {
            m_result.detail = QStringLiteral("still busy after %1 requests").arg(tries);
            return false;
        }
        wait(status.pollMs);
    }
    if (status.state != kDfuStateDnloadIdle || status.status != 0) {
        m_result.detail =
            QStringLiteral("%1, state %2").arg(dfuStatusName(status.status)).arg(status.state);
        return false;
    }
    return true;
}

DfuTransferResult Transfer::run(const QByteArray &image)
{
    if (image.isEmpty() || image.size() > kDfuMaxImageBytes)
        return fail(DfuFailure::BadImage, "The install file has the wrong size.");

    const UsbDeviceId id = m_link.deviceId();
    if (id.vendor != kAppleVendorId || id.product != kClassicDfuProductId)
        return fail(DfuFailure::WrongDevice, "This device is not an iPod in DFU mode.");

    QString error;
    if (!m_link.open(&error)) {
        m_result.access = m_link.openError();
        return fail(DfuFailure::Open, "The iPod could not be opened.", error);
    }

    // Step 3: nothing has been sent yet, so there is nothing to abort.
    bool answered = false;
    if (!checkIdle(&answered)) {
        m_link.close();
        return fail(answered ? DfuFailure::NotReady : DfuFailure::Interrupted,
                    answered ? kMessageNotReady : kMessageInterrupted);
    }

    // Step 4.
    const QByteArray data = withDfuChecksum(image);
    int number = 0;
    for (qsizetype at = 0; at < data.size(); at += kDfuBlockBytes, ++number) {
        if (!sendBlock(number, data.mid(at, kDfuBlockBytes)))
            return recover(DfuFailure::Interrupted, kMessageInterrupted, {});
    }

    // Step 5: the empty block, and the answer to it.
    Status status;
    if (!sendOut(kDfuDnload, quint16(number)) || !getStatus(&status))
        return recover(DfuFailure::Interrupted, kMessageInterrupted, {});
    if (status.status == kDfuStatusErrFirmware || status.status == kDfuStatusErrVerify)
        return recover(DfuFailure::Refused, kMessageRefused, dfuStatusName(status.status));
    if (status.status != 0 || status.state != kDfuStateManifest)
        return recover(
            DfuFailure::Interrupted, kMessageInterrupted,
            QStringLiteral("%1, state %2").arg(dfuStatusName(status.status)).arg(status.state));

    // Step 6: the payload starts, and the request that follows usually fails.
    wait(status.pollMs);
    Status last;
    if (getStatus(&last)) {
        if (last.state == kDfuStateWaitReset && last.status == 0) {
            ++m_result.resets;
            QString ignored;
            m_link.reset(&ignored);
        } else {
            return recover(
                DfuFailure::Refused, kMessageRefused,
                QStringLiteral("%1, state %2").arg(dfuStatusName(last.status)).arg(last.state));
        }
    }

    // Step 7.
    m_link.close();
    m_result.ok = true;
    m_result.failure = DfuFailure::None;
    m_result.message =
        QStringLiteral("The install file was sent. The iPod is writing it and will restart.");
    m_result.detail.clear();
    return m_result;
}

} // namespace

DfuScan chooseDfuDevice(const QList<UsbDeviceId> &devices)
{
    DfuScan scan;
    int count = 0;
    bool wtf = false;
    for (int i = 0; i < devices.size(); ++i) {
        const UsbDeviceId &d = devices.at(i);
        if (d.vendor != kAppleVendorId)
            continue;
        if (d.product == kClassicDfuProductId) {
            if (count == 0)
                scan.index = i;
            ++count;
        } else if (isWtf(d.product)) {
            wtf = true;
        }
    }
    if (count > 1) {
        scan.status = DfuScanStatus::Several;
        scan.index = -1;
        scan.message = QStringLiteral("More than one iPod is in DFU mode. Plug only one iPod.");
    } else if (count == 1) {
        scan.status = DfuScanStatus::Found;
    } else if (wtf) {
        scan.status = DfuScanStatus::Wtf;
        scan.message = QStringLiteral("The iPod is in the wrong mode. Reset it (hold Menu and "
                                      "Select) and enter DFU mode again.");
    }
    return scan;
}

quint32 crc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = makeCrcTable();
    quint32 c = 0xFFFFFFFFu;
    for (char byte : data)
        c = table[(c ^ quint8(byte)) & 0xFF] ^ (c >> 8);
    return ~c;
}

QByteArray dfuChecksumBytes(const QByteArray &image)
{
    const quint32 v = ~crc32(image);
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char((v >> (8 * i)) & 0xFF);
    return b;
}

QByteArray withDfuChecksum(const QByteArray &image)
{
    return image + dfuChecksumBytes(image);
}

QString dfuStatusName(int status)
{
    static const char *const names[] = {
        "OK",      "errTARGET", "errFILE",    "errWRITE",      "errERASE",    "errCHECK_ERASED",
        "errPROG", "errVERIFY", "errADDRESS", "errNOTDONE",    "errFIRMWARE", "errVENDOR",
        "errUSBR", "errPOR",    "errUNKNOWN", "errSTALLEDPKT",
    };
    if (status >= 0 && status < int(sizeof names / sizeof names[0]))
        return QString::fromLatin1(names[status]);
    return QStringLiteral("status %1").arg(status);
}

DfuTransferResult sendDfuImage(UsbDfuLink &link, const QByteArray &image,
                               const DfuSendOptions &options)
{
    return Transfer(link, options).run(image);
}

} // namespace tacetos::installer
