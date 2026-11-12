// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Writes a fake iPod disk image for trying things by hand:
//   tacet-fakepod <id> <file> [--mac] [--bootloader] [--rsrc] [--aupd] [--late-directory]

#include "fakepod.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace tacetos::fakepod;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const QStringList args = app.arguments().mid(1);

    FakePodSpec spec;
    QStringList positional;
    for (const QString &a : args) {
        if (a == QLatin1String("--mac"))
            spec.table = TableKind::Apm;
        else if (a == QLatin1String("--bootloader"))
            spec.bootloaderPresent = true;
        else if (a == QLatin1String("--rsrc"))
            spec.withRsrc = true;
        else if (a == QLatin1String("--aupd"))
            spec.withAupd = true;
        else if (a == QLatin1String("--late-directory"))
            spec.directoryLate = true;
        else
            positional << a;
    }
    if (positional.size() != 2) {
        err << "usage: tacet-fakepod <id> <file> [--mac] [--bootloader] [--rsrc] [--aupd] "
               "[--late-directory]\n"
            << "ids: " << FakePod::modelIds().join(QLatin1Char(' ')) << "\n";
        return 2;
    }
    spec.modelId = positional.at(0);
    spec.path = positional.at(1);

    const FakePodResult r = FakePod::build(spec);
    if (!r.ok) {
        err << "tacet-fakepod: " << r.error << "\n";
        return 1;
    }
    return 0;
}
