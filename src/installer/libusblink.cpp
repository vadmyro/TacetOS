// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "libusblink.h"

#include "constants.h"

#include <algorithm>
#include <iterator>
#include <libusb.h>

namespace tacetos::installer {

namespace {

constexpr int kDfuConfiguration = 1;
constexpr int kDfuInterface = 0;

UsbAccessError mapError(int code)
{
    switch (code) {
    case LIBUSB_ERROR_ACCESS:
        return UsbAccessError::AccessDenied;
    case LIBUSB_ERROR_BUSY:
        return UsbAccessError::Busy;
    case LIBUSB_ERROR_NO_DEVICE:
        return UsbAccessError::NoDevice;
    case LIBUSB_ERROR_NOT_SUPPORTED:
        return UsbAccessError::NotSupported;
    default:
        return UsbAccessError::Other;
    }
}

QString describe(const char *what, int code)
{
    return QStringLiteral("%1 failed: %2")
        .arg(QLatin1String(what), QString::fromUtf8(libusb_strerror(code)));
}

bool isDfuOrWtf(quint16 vendor, quint16 product)
{
    if (vendor != kAppleVendorId)
        return false;
    if (product == kClassicDfuProductId)
        return true;
    return std::find(std::begin(kClassicWtfProductIds), std::end(kClassicWtfProductIds), product) !=
           std::end(kClassicWtfProductIds);
}

QString portPath(libusb_device *device)
{
    uint8_t ports[8];
    const int count = libusb_get_port_numbers(device, ports, int(sizeof ports));
    QString path = QString::number(libusb_get_bus_number(device));
    for (int i = 0; i < count; ++i)
        path += (i == 0 ? QLatin1Char('-') : QLatin1Char('.')) + QString::number(ports[i]);
    return path;
}

// The devices of the bus that pass `keep`, with the list to free after use.
template <typename Keep, typename Use>
bool walkDevices(libusb_context *context, QString *error, Keep keep, Use use)
{
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(context, &list);
    if (count < 0) {
        if (error)
            *error = describe("Listing the USB devices", int(count));
        return false;
    }
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(list[i], &descriptor) != 0)
            continue;
        if (keep(descriptor))
            use(list[i], descriptor);
    }
    libusb_free_device_list(list, 1);
    return true;
}

} // namespace

QList<UsbBusDevice> scanDfuDevices(QString *error)
{
    QList<UsbBusDevice> found;
    libusb_context *context = nullptr;
    const int init = libusb_init(&context);
    if (init != 0) {
        if (error)
            *error = describe("Starting libusb", init);
        return found;
    }
    const bool ok = walkDevices(
        context, error,
        [](const libusb_device_descriptor &d) { return isDfuOrWtf(d.idVendor, d.idProduct); },
        [&found](libusb_device *device, const libusb_device_descriptor &d) {
            UsbBusDevice entry;
            entry.id = {d.idVendor, d.idProduct};
            entry.bus = libusb_get_bus_number(device);
            entry.address = libusb_get_device_address(device);
            entry.port = portPath(device);
            found.append(entry);
        });
    libusb_exit(context);
    if (!ok)
        found.clear();
    return found;
}

LibusbDfuLink::LibusbDfuLink(const UsbBusDevice &device) : m_device(device) {}

LibusbDfuLink::~LibusbDfuLink()
{
    close();
}

bool LibusbDfuLink::open(QString *error)
{
    close();
    m_openError = UsbAccessError::Other;
    auto fail = [&](const QString &why, UsbAccessError code) {
        if (error)
            *error = why;
        m_openError = code;
        close();
        return false;
    };

    const int init = libusb_init(&m_context);
    if (init != 0) {
        m_context = nullptr;
        return fail(describe("Starting libusb", init), mapError(init));
    }

    libusb_device *target = nullptr;
    QString listError;
    // The device is found again by its bus and address, and its id is checked
    // once more: the address can be given to another device after a reset.
    if (!walkDevices(
            m_context, &listError, [](const libusb_device_descriptor &) { return true; },
            [&](libusb_device *device, const libusb_device_descriptor &d) {
                if (!target && libusb_get_bus_number(device) == m_device.bus &&
                    libusb_get_device_address(device) == m_device.address &&
                    d.idVendor == m_device.id.vendor && d.idProduct == m_device.id.product) {
                    libusb_ref_device(device);
                    target = device;
                }
            }))
        return fail(listError, UsbAccessError::Other);
    if (!target)
        return fail(QStringLiteral("The iPod is no longer on the USB bus."),
                    UsbAccessError::NoDevice);

    const int opened = libusb_open(target, &m_handle);
    libusb_unref_device(target);
    if (opened != 0) {
        m_handle = nullptr;
        return fail(describe("Opening the USB device", opened), mapError(opened));
    }

    // Section 5, step 2: a driver bound to the interface is detached when it is
    // claimed and given back when it is released. Not every system can do it.
    libusb_set_auto_detach_kernel_driver(m_handle, 1);

    int configuration = 0;
    int result = libusb_get_configuration(m_handle, &configuration);
    if (result != 0)
        return fail(describe("Reading the USB configuration", result), mapError(result));
    if (configuration != kDfuConfiguration) {
        result = libusb_set_configuration(m_handle, kDfuConfiguration);
        if (result != 0)
            return fail(describe("Selecting the USB configuration", result), mapError(result));
    }
    result = libusb_claim_interface(m_handle, kDfuInterface);
    if (result != 0)
        return fail(describe("Claiming the USB interface", result), mapError(result));
    m_claimed = true;
    m_openError = UsbAccessError::None;
    return true;
}

UsbControlResult LibusbDfuLink::control(const UsbControlRequest &request, const QByteArray &out,
                                        int timeoutMs)
{
    UsbControlResult result;
    if (!m_handle) {
        result.code = UsbAccessError::NoDevice;
        result.error = QStringLiteral("The USB device is not open.");
        return result;
    }
    const bool reads = (request.type & LIBUSB_ENDPOINT_IN) != 0;
    QByteArray buffer = reads ? QByteArray(request.length, '\0') : out;
    const int sent =
        libusb_control_transfer(m_handle, request.type, request.request, request.value,
                                kDfuInterface, reinterpret_cast<unsigned char *>(buffer.data()),
                                quint16(buffer.size()), unsigned(timeoutMs));
    if (sent < 0) {
        result.code = mapError(sent);
        result.error = describe("The USB request", sent);
        return result;
    }
    result.ok = true;
    if (reads)
        result.data = buffer.left(sent);
    return result;
}

bool LibusbDfuLink::reset(QString *error)
{
    if (!m_handle) {
        if (error)
            *error = QStringLiteral("The USB device is not open.");
        return false;
    }
    const int result = libusb_reset_device(m_handle);
    // The device usually comes back with a new handle, or not at all: that is
    // the normal end of the exchange (section 5, step 6).
    if (result != 0 && result != LIBUSB_ERROR_NOT_FOUND) {
        if (error)
            *error = describe("The USB reset", result);
        return false;
    }
    return true;
}

void LibusbDfuLink::close()
{
    if (m_handle) {
        if (m_claimed)
            libusb_release_interface(m_handle, kDfuInterface);
        libusb_close(m_handle);
    }
    m_handle = nullptr;
    m_claimed = false;
    if (m_context)
        libusb_exit(m_context);
    m_context = nullptr;
}

} // namespace tacetos::installer
