// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

// The DFU exchanges with the iPod Classic (docs/specs/bootloader-classic.md,
// sections 4 to 7 and 8.5). Nothing here talks to a real USB stack: the access
// is behind UsbDfuLink, and the tests play the iPod with a fake one.

namespace tacetos::installer {

struct UsbDeviceId
{
    quint16 vendor = 0;
    quint16 product = 0;
};

// The setup packet of a control request, without the index (always 0, the
// interface). `length` is the size of the data that follows, or the size asked
// from the device for a request that reads.
struct UsbControlRequest
{
    quint8 type = 0;
    quint8 request = 0;
    quint16 value = 0;
    quint16 length = 0;

    bool operator==(const UsbControlRequest &o) const
    {
        return type == o.type && request == o.request && value == o.value && length == o.length;
    }
};

// What the access layer of a system reports (docs/specs/bootloader-classic.md,
// section 15). Each system maps its own codes to these.
enum class UsbAccessError {
    None,
    AccessDenied,     // no permission on the device
    Busy,             // another program holds the interface
    NoDevice,         // the cable was pulled, or the iPod reset
    NotSupported,     // Windows: no usable driver
    NotFoundAtConfig, // Windows: an old driver, the configuration cannot be selected
    ExclusiveAccess,  // macOS: another process has the device open
    Other,
};

struct UsbControlResult
{
    bool ok = false;
    // Set by the access layer when `ok` is false and the code is known.
    UsbAccessError code = UsbAccessError::None;
    // What the device sent, for a request that reads.
    QByteArray data;
    // In English, for the log, when `ok` is false.
    QString error;
};

// The access to one USB device, as small as the helper needs it (section 17):
// open, control transfer, reset, close. A real implementation exists per system.
class UsbDfuLink
{
public:
    virtual ~UsbDfuLink() = default;

    // The id of the device this link is for. The transfer checks it first.
    virtual UsbDeviceId deviceId() const = 0;
    // Section 5, step 2: configuration 1, the driver detached if needed, interface 0 claimed.
    virtual bool open(QString *error) = 0;
    // Why the last `open` failed.
    virtual UsbAccessError openError() const { return UsbAccessError::Other; }
    // `out` is sent after the setup packet of a request that writes; it is empty
    // for a request that reads, and the answer is in the result.
    virtual UsbControlResult control(const UsbControlRequest &request, const QByteArray &out,
                                     int timeoutMs) = 0;
    // A USB reset of the device (section 5, step 6).
    virtual bool reset(QString *error) = 0;
    // Step 7: interface released, driver given back. Safe to call on a gone device.
    virtual void close() = 0;
};

enum class DfuScanStatus {
    Found,   // exactly one iPod in DFU mode
    None,    // none yet: keep waiting
    Several, // more than one: refuse
    Wtf,     // an iPod in the wrong mode (section 2)
};

struct DfuScan
{
    DfuScanStatus status = DfuScanStatus::None;
    // The position in the list given, when `status` is `Found`.
    int index = -1;
    // In English, for `Several` and `Wtf`.
    QString message;
};

// Section 5, step 1: keeps the devices `05ac:1223` and looks at nothing else.
DfuScan chooseDfuDevice(const QList<UsbDeviceId> &devices);

// Section 8.5: the CRC-32 of zip and Ethernet (polynomial 0xEDB88320).
quint32 crc32(const QByteArray &data);
// The 4 bytes appended after the image: the bitwise NOT of `crc32`, little-endian.
QByteArray dfuChecksumBytes(const QByteArray &image);
// The image followed by its 4 checksum bytes: what is cut into blocks.
QByteArray withDfuChecksum(const QByteArray &image);

// The name of a status code of section 4.2, for the log.
QString dfuStatusName(int status);

enum class DfuFailure {
    None,
    BadImage,    // empty, or larger than the ROM accepts: nothing was opened
    WrongDevice, // not `05ac:1223`: nothing was opened
    Open,
    NotReady,    // step 3
    Interrupted, // steps 3 to 6: a request failed, or an unexpected answer
    Refused,     // errFIRMWARE or errVERIFY, or a refusal after the check
};

struct DfuTransferResult
{
    bool ok = false;
    DfuFailure failure = DfuFailure::None;
    // The final message, in English (section 7).
    QString message;
    // For the log: the status name or the error of the access layer.
    QString detail;
    // The code of the access layer for the failure, when it gave one.
    UsbAccessError access = UsbAccessError::None;
    // Every control request, in the order they were sent.
    QList<UsbControlRequest> requests;
    int resets = 0;
};

struct DfuSendOptions
{
    // How the transfer waits (the poll timeouts). Tests replace it by a recorder.
    std::function<void(int milliseconds)> sleep;
};

// Sections 5 and 7: sends `image` (without the checksum, which is added here) to
// the device of `link`. On a failure after the first block, the recovery of
// section 7 is done (ABORT, GETSTATE, CLRSTATUS), and the link is always closed.
DfuTransferResult sendDfuImage(UsbDfuLink &link, const QByteArray &image,
                               const DfuSendOptions &options = {});

} // namespace tacetos::installer
