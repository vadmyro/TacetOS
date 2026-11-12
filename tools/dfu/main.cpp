// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// tacet-dfu: lists the iPods in DFU mode on the USB bus. For development. It
// reads the device descriptors only: it opens no device and sends nothing.

#include "libusblink.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QTextStream>

using namespace tacetos::installer;

namespace {

QString hex4(quint16 value)
{
    return QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0'));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tacet-dfu"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lists the iPods in DFU mode. Read only: nothing is sent to any device."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("scan"), QStringLiteral("List the devices in DFU mode.")});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (!parser.isSet(QStringLiteral("scan")) || !parser.positionalArguments().isEmpty()) {
        err << "tacet-dfu: nothing to do. The only command is --scan." << Qt::endl;
        return 2;
    }

    QString error;
    const QList<UsbBusDevice> devices = scanDfuDevices(&error);
    if (!error.isEmpty()) {
        err << "tacet-dfu: " << error << Qt::endl;
        return 1;
    }

    QList<UsbDeviceId> ids;
    for (const UsbBusDevice &d : devices) {
        ids.append(d.id);
        out << "port " << d.port << "  bus " << d.bus << " address " << d.address
            << "  05ac:" << hex4(d.id.product) << "  "
            << (d.id.product == 0x1223 ? "DFU mode" : "wrong mode (restore step)") << Qt::endl;
    }

    const DfuScan scan = chooseDfuDevice(ids);
    switch (scan.status) {
    case DfuScanStatus::Found:
        out << "One iPod in DFU mode." << Qt::endl;
        break;
    case DfuScanStatus::None:
        out << "No iPod in DFU mode." << Qt::endl;
        break;
    case DfuScanStatus::Several:
    case DfuScanStatus::Wtf:
        out << scan.message << Qt::endl;
        break;
    }
    return 0;
}
