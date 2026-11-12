// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "dfuprotocol.h"

#include <QList>
#include <QString>

// The access to a real iPod in DFU mode through libusb 1.0 (docs/specs/
// bootloader-classic.md, sections 6 and 15.2). This file is built only when
// libusb is found, and no test runs it: the tests use a fake UsbDfuLink.

struct libusb_context;
struct libusb_device_handle;

namespace tacetos::installer {

// A USB device found on the bus, as the scan reports it.
struct UsbBusDevice
{
    UsbDeviceId id;
    int bus = 0;
    int address = 0;
    // The port path, like `1-2.3`.
    QString port;
};

// Lists the devices that are in DFU mode (`05ac:1223`) or in the wrong mode
// (the WTF ids of section 2). It reads the descriptors only: it opens nothing
// and sends nothing. On a failure it returns an empty list and fills `error`.
QList<UsbBusDevice> scanDfuDevices(QString *error);

class LibusbDfuLink : public UsbDfuLink
{
public:
    explicit LibusbDfuLink(const UsbBusDevice &device);
    ~LibusbDfuLink() override;

    LibusbDfuLink(const LibusbDfuLink &) = delete;
    LibusbDfuLink &operator=(const LibusbDfuLink &) = delete;

    UsbDeviceId deviceId() const override { return m_device.id; }
    bool open(QString *error) override;
    UsbAccessError openError() const override { return m_openError; }
    UsbControlResult control(const UsbControlRequest &request, const QByteArray &out,
                             int timeoutMs) override;
    bool reset(QString *error) override;
    void close() override;

private:
    UsbBusDevice m_device;
    UsbAccessError m_openError = UsbAccessError::Other;
    libusb_context *m_context = nullptr;
    libusb_device_handle *m_handle = nullptr;
    bool m_claimed = false;
};

} // namespace tacetos::installer
