// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// tacet-watch: prints each iPod that is added, changed or removed, with the
// time to the millisecond (to measure delays). With --verbose it also prints
// the lines the detection sends to the journal. For development. It only reads, and Ctrl+C quits.

#include "detected.h"
#include "linuxsource.h"
#include "live.h"
#include "livedriver.h"
#include "messages.h"
#include "models.h"
#include "mountwatcher.h"
#ifdef TACETOS_HAVE_UDEV
#include "udevmonitor.h"
#endif

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

using namespace tacetos::device;

namespace {

int g_signalPipe[2] = {-1, -1};

// Only writes one byte: the rest happens in the event loop.
void onSignal(int)
{
    const char byte = 1;
    const ssize_t written = ::write(g_signalPipe[1], &byte, 1);
    (void)written;
}

bool installSignalHandlers()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) != 0)
        return false;
    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    return ::sigaction(SIGINT, &action, nullptr) == 0 &&
           ::sigaction(SIGTERM, &action, nullptr) == 0;
}

QString modeName(UsbMode mode)
{
    switch (mode) {
    case UsbMode::Normal:
        return QStringLiteral("normal");
    case UsbMode::Dfu:
        return QStringLiteral("dfu");
    case UsbMode::Other:
        return QStringLiteral("other");
    case UsbMode::None:
        break;
    }
    return QStringLiteral("none");
}

QString describe(const DetectedIpod &ipod, const QList<Model> &models)
{
    QStringList parts;
    parts << (ipod.model.isEmpty() ? QStringLiteral("model ?") : ipod.model);
    if (ipod.model.isEmpty() && !ipod.candidates.isEmpty())
        parts << QStringLiteral("(%1)").arg(ipod.candidates.join(QLatin1Char('|')));
    parts << ipod.usbId << modeName(ipod.mode);
    if (!ipod.serial.isEmpty())
        parts << QStringLiteral("serial %1").arg(ipod.serial);
    parts << QStringLiteral("\"%1\"").arg(stateMessage(ipod, models));
    if (ipod.volume)
        parts << QStringLiteral("\"%1\"").arg(firmwareMessage(ipod));
    if (ipod.disk)
        parts << ipod.disk->node;
    if (ipod.volume)
        parts << ipod.volume->mountPoint;
    return parts.join(QLatin1Char(' '));
}

void printLine(QTextStream &out, const QString &event, const QString &portPath, const QString &rest)
{
    out << QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")) << "  "
        << event.leftJustified(7) << "  " << portPath;
    if (!rest.isEmpty())
        out << "  " << rest;
    out << Qt::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tacet-watch"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Prints each iPod that is added, changed or removed. Read only. Ctrl+C quits."));
    parser.addHelpOption();
    QCommandLineOption modelsOption(QStringLiteral("models"),
                                    QStringLiteral("The model list to read."),
                                    QStringLiteral("file"), QStringLiteral(TACETOS_MODELS_TOML));
    parser.addOption(modelsOption);
    QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral(
            "Also print the lines of the detection log (why, and each change of state)."));
    parser.addOption(verboseOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const ModelList list = loadModels(parser.value(modelsOption));
    if (!list.error.isEmpty()) {
        err << "tacet-watch: " << list.error << Qt::endl;
        return 1;
    }
    if (!installSignalHandlers()) {
        err << "tacet-watch: cannot catch Ctrl+C" << Qt::endl;
        return 1;
    }
    QSocketNotifier signalNotifier(g_signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [&] {
        char byte;
        const ssize_t got = ::read(g_signalPipe[0], &byte, 1);
        (void)got;
        QCoreApplication::quit();
    });

    SystemClock clock;
    LinuxDeviceSource source(list.models);
    LiveTracker tracker(list.models, &clock, &source);
    LiveDriver driver(&tracker, &clock);

    QObject::connect(&tracker, &LiveTracker::ipodAdded, &app, [&](const DetectedIpod &ipod) {
        printLine(out, QStringLiteral("added"), ipod.portPath, describe(ipod, list.models));
    });
    QObject::connect(&tracker, &LiveTracker::ipodChanged, &app, [&](const DetectedIpod &ipod) {
        printLine(out, QStringLiteral("changed"), ipod.portPath, describe(ipod, list.models));
    });
    QObject::connect(&tracker, &LiveTracker::ipodRemoved, &app, [&](const QString &portPath) {
        printLine(out, QStringLiteral("removed"), portPath, QString());
    });

    if (parser.isSet(verboseOption)) {
        QObject::connect(&tracker, &LiveTracker::logged, &app,
                         [&](const QString &portPath, const QStringList &lines) {
                             for (const QString &line : lines)
                                 printLine(out, QStringLiteral("log"), portPath, line);
                         });
    }

    // The mount table alone says nothing about USB devices, so it is only
    // added next to udev. Without udev the driver polls.
    bool events = false;
#ifdef TACETOS_HAVE_UDEV
    if (UdevMonitor::attach(&driver)) {
        MountWatcher::attach(&driver);
        events = true;
    }
#endif
    out << (events ? "Watching with udev events." : "Watching by polling (no udev events).")
        << " Ctrl+C to quit." << Qt::endl;

    driver.start();
    const int code = app.exec();
    driver.stop();
    out << "Bye." << Qt::endl;
    return code;
}
