// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// tacet-disk: mounts, unmounts or ejects the plugged iPod through the disk
// service. For development. It only ever acts on a disk found under an Apple
// USB device, and the service is asked to check it again.

#include "detected.h"
#include "linuxdisks.h"
#include "linuxsource.h"
#include "live.h"
#include "models.h"
#include "udisks.h"
#include "udisksbackend.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QList>
#include <QStringList>
#include <QTextStream>
#include <memory>

using namespace tacetos::device;

namespace {

QString describe(const DetectedIpod &ipod)
{
    QStringList parts;
    parts << ipod.portPath << (ipod.model.isEmpty() ? QStringLiteral("model ?") : ipod.model)
          << ipod.usbId;
    if (!ipod.serial.isEmpty())
        parts << QStringLiteral("serial %1").arg(ipod.serial);
    return parts.join(QLatin1Char(' '));
}

int fail(const QString &message)
{
    QTextStream(stderr) << "tacet-disk: " << message << Qt::endl;
    return 1;
}

// Prints the failure of a disk call: the short text, then the detail of the
// service on the next line.
int failResult(const DiskResult &result)
{
    QTextStream err(stderr);
    err << "tacet-disk: " << diskErrorMessage(result.error) << Qt::endl;
    if (!result.detail.isEmpty())
        err << "  " << result.detail << Qt::endl;
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tacet-disk"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Mounts, unmounts or ejects the plugged iPod through the disk service (udisks2)."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("action"),
                                 QStringLiteral("mount, unmount or eject."));
    QCommandLineOption readOnlyOption(QStringLiteral("ro"),
                                      QStringLiteral("With mount: mount read only."));
    QCommandLineOption portOption(QStringLiteral("port"),
                                  QStringLiteral("The USB port of the iPod, for example 1-2. "
                                                 "Required when several iPods are plugged in."),
                                  QStringLiteral("port"));
    QCommandLineOption modelsOption(QStringLiteral("models"),
                                    QStringLiteral("The model list to read."),
                                    QStringLiteral("file"), QStringLiteral(TACETOS_MODELS_TOML));
    parser.addOptions({readOnlyOption, portOption, modelsOption});
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 1)
        return fail(QStringLiteral("give one action: mount, unmount or eject (see --help)"));
    const QString action = args.first();
    if (action != QLatin1String("mount") && action != QLatin1String("unmount") &&
        action != QLatin1String("eject"))
        return fail(QStringLiteral("unknown action \"%1\" (mount, unmount or eject)").arg(action));
    if (parser.isSet(readOnlyOption) && action != QLatin1String("mount"))
        return fail(QStringLiteral("--ro only goes with mount"));

    const ModelList list = loadModels(parser.value(modelsOption));
    if (!list.error.isEmpty())
        return fail(list.error);

    // One reading of every layer, then pick the iPod.
    SystemClock clock;
    LinuxDeviceSource source(list.models);
    LiveTracker tracker(list.models, &clock, &source);
    tracker.refresh();

    QList<DetectedIpod> candidates;
    for (const DetectedIpod &ipod : tracker.ipods()) {
        if (ipod.mode == UsbMode::Normal)
            candidates << ipod;
    }
    if (candidates.isEmpty())
        return fail(QStringLiteral("no iPod in normal mode is plugged in"));

    const DetectedIpod *chosen = nullptr;
    if (parser.isSet(portOption)) {
        const QString port = parser.value(portOption);
        for (const DetectedIpod &ipod : candidates) {
            if (ipod.portPath == port)
                chosen = &ipod;
        }
        if (!chosen) {
            QStringList ports;
            for (const DetectedIpod &ipod : candidates)
                ports << ipod.portPath;
            return fail(QStringLiteral("no iPod on port \"%1\" (found: %2)")
                            .arg(port, ports.join(QStringLiteral(", "))));
        }
    } else if (candidates.size() == 1) {
        chosen = &candidates.first();
    } else {
        QTextStream err(stderr);
        err << "tacet-disk: " << candidates.size()
            << " iPods are plugged in, give --port <port>:" << Qt::endl;
        for (const DetectedIpod &ipod : candidates)
            err << "  " << describe(ipod) << Qt::endl;
        return 1;
    }

    // The disk comes from the same walk the detection uses: only a disk under
    // an Apple USB device is ever returned.
    const LinuxDiskList disks = readLinuxDisks();
    if (!disks.error.isEmpty())
        return fail(disks.error);
    UsbDevice device;
    device.usbId = chosen->usbId;
    device.mode = chosen->mode;
    device.portPath = chosen->portPath;
    const std::optional<LinuxDisk> disk = findDiskForUsb(disks.disks, device);
    if (!disk)
        return fail(QStringLiteral("the iPod on port %1 has no disk yet").arg(chosen->portPath));

    QTextStream out(stdout);
    out << "iPod: " << describe(*chosen) << " (" << disk->node << ")" << Qt::endl;

    DiskControl control(std::make_unique<UdisksDBusBackend>());
    if (action == QLatin1String("mount")) {
        const DiskResult result = control.mount(*disk, parser.isSet(readOnlyOption));
        if (!result.ok())
            return failResult(result);
        out << (result.alreadyMounted ? "Already mounted: " : "Mounted: ") << result.mountPoint
            << Qt::endl;
    } else if (action == QLatin1String("unmount")) {
        const DiskResult result = control.unmount(*disk);
        if (!result.ok())
            return failResult(result);
        out << "Unmounted " << result.unmounted << " volume(s)." << Qt::endl;
    } else {
        const DiskResult result = control.eject(*disk);
        if (!result.ok())
            return failResult(result);
        out << "Unmounted " << result.unmounted << " volume(s). "
            << (result.released ? "The iPod can be unplugged."
                                : "Nothing more to do, the iPod can be unplugged.")
            << Qt::endl;
    }
    return 0;
}
