// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// What tacet-watch prints: a change that shows nothing new gives no line.

#include "detected.h"
#include "models.h"
#include "watchformat.h"

#include <QTest>

using namespace tacetos::device;
using namespace tacetos::watch;

namespace {

DetectedIpod mounted()
{
    DetectedIpod ipod;
    ipod.usbId = QStringLiteral("05ac:1261");
    ipod.mode = UsbMode::Normal;
    ipod.portPath = QStringLiteral("1-2");
    ipod.model = QStringLiteral("classic-6g");
    ipod.disk = DiskInfo{QStringLiteral("/dev/sdb"), 160000000000, 512};
    ipod.volume = VolumeInfo{QStringLiteral("/run/media/x/IPOD"), QStringLiteral("vfat"),
                             QStringLiteral("IPOD"), 100000000000, 50000000000};
    return ipod;
}

class TstWatchFormat : public QObject
{
    Q_OBJECT
private slots:
    void invisibleChangeGivesNoLine();
    void visibleChangeGivesLine();
    void removedForgets();
};

void TstWatchFormat::invisibleChangeGivesNoLine()
{
    const QList<Model> models;
    ChangeFilter filter;
    DetectedIpod ipod = mounted();
    filter.shown(ipod.portPath, describe(ipod, models));

    // Free space, label and disk size are not shown by the tool.
    ipod.volume->freeBytes = 1;
    ipod.volume->label = QStringLiteral("OTHER");
    ipod.disk->sizeBytes = 0;
    QVERIFY(!filter.isVisibleChange(ipod.portPath, describe(ipod, models)));
    QVERIFY(!filter.isVisibleChange(ipod.portPath, describe(ipod, models)));
}

void TstWatchFormat::visibleChangeGivesLine()
{
    const QList<Model> models;
    ChangeFilter filter;
    DetectedIpod ipod = mounted();
    filter.shown(ipod.portPath, describe(ipod, models));

    ipod.volume.reset();
    QVERIFY(filter.isVisibleChange(ipod.portPath, describe(ipod, models)));
    // Back to the first text: that is a change again.
    ipod = mounted();
    QVERIFY(filter.isVisibleChange(ipod.portPath, describe(ipod, models)));
}

void TstWatchFormat::removedForgets()
{
    const QList<Model> models;
    ChangeFilter filter;
    const DetectedIpod ipod = mounted();
    filter.shown(ipod.portPath, describe(ipod, models));
    filter.removed(ipod.portPath);
    QVERIFY(filter.isVisibleChange(ipod.portPath, describe(ipod, models)));
}

} // namespace

QTEST_GUILESS_MAIN(TstWatchFormat)
#include "tst_watch_format.moc"
