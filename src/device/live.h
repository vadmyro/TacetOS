// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "merge.h"
#include "models.h"
#include "usblist.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>

// Staying live (docs/specs/detection.md section 9): the list of detected iPods,
// kept up to date by events and by readings. Nothing here reads a system or
// sleeps: the clock and the readers are given by the caller, and the passing
// of time is driven by tick().

namespace tacetos::device {

// A clock in milliseconds. Only differences matter.
class Clock
{
public:
    virtual ~Clock() = default;
    virtual qint64 nowMs() const = 0;
};

// The real clock, from a monotonic timer.
class SystemClock : public Clock
{
public:
    SystemClock();
    qint64 nowMs() const override;

private:
    QElapsedTimer timer_;
};

// What the readers of layers 2 and 3 found for one USB device.
struct PortReading
{
    std::optional<DiskInfo> disk;
    std::optional<VolumeInfo> volume;
    DiskReading diskReading;
    MarkerReading markers;
};

// Where the readings come from. A real system gives one, a test gives a
// scripted one.
class DeviceSource
{
public:
    virtual ~DeviceSource() = default;
    // The Apple devices of a known id (sections 3 and 4). Empty when the list
    // could not be read: the list on screen is then left as it is.
    virtual std::optional<QList<UsbDevice>> readUsb() = 0;
    // Layers 2 and 3 for one device in normal mode (never asked for DFU).
    virtual PortReading readPort(const UsbDevice &device) = 0;
};

// The list of detected iPods. Every public call that reads, or lets time pass,
// ends by sending the signals for what changed, in that order: removed, added,
// changed, then listChanged once.
class LiveTracker : public QObject
{
    Q_OBJECT

public:
    // `clock` and `source` must outlive the tracker.
    LiveTracker(QList<Model> models, const Clock *clock, DeviceSource *source,
                QObject *parent = nullptr);
    ~LiveTracker() override;

    // The detected iPods, in the order they appeared.
    QList<DetectedIpod> ipods() const;

    // Section 9.1: where no event source exists, tick() reads the USB list
    // every kPollUsbMs and the disks every kPollDisksMs.
    void setPolling(bool on);

    // An event is only a hint to read now (section 9.1).
    void usbEvent();
    void diskEvent();

    // Section 9.5: one full reading of every layer.
    void refresh();

    // Section 9.4 rule 1: an operation of the app runs on this iPod, or the
    // user was asked to hold buttons. Its disappearance is then not shown
    // until the same port window has passed.
    void setHeld(const QString &portPath, bool held);

    // Lets time pass: polls when due, ends the disk wait, drops the iPods
    // that did not come back within the same port window.
    void tick();

    // The time of the next tick that would change something, from the clock's
    // own scale. Empty when nothing is waiting.
    std::optional<qint64> nextWakeMs() const;

signals:
    void ipodAdded(const tacetos::device::DetectedIpod &ipod);
    // Only sent when something in the iPod is different.
    void ipodChanged(const tacetos::device::DetectedIpod &ipod);
    void ipodRemoved(const QString &portPath);
    void listChanged();

private:
    struct Entry
    {
        UsbDevice usb;
        PortReading reading;
        DetectedIpod ipod;
        // When this device (in this mode) appeared; the disk wait counts from here.
        qint64 firstSeenMs = 0;
        bool waitOver = false;
        bool held = false;
        // Set while the device is gone but still shown (section 9.4).
        std::optional<qint64> missingSinceMs;
    };

    struct Notice
    {
        enum class Kind { Removed, Added, Changed };
        Kind kind = Kind::Changed;
        QString portPath;
        DetectedIpod ipod;
    };

    // The last normal mode reading of a port, for section 8.2 rule 5.
    struct Memory
    {
        DetectedIpod ipod;
        qint64 seenMs = 0;
    };

    void readUsbNow();
    void readDisksNow();
    void expireAndWait();
    void remerge(Entry &entry);
    void remember(const Entry &entry);
    Entry *find(const QString &portPath);
    const Entry *find(const QString &portPath) const;
    void flush();

    QList<Model> models_;
    const Clock *clock_;
    DeviceSource *source_;
    bool polling_ = false;
    std::optional<qint64> nextPollUsb_;
    std::optional<qint64> nextPollDisks_;
    QList<Entry> entries_;
    QHash<QString, Memory> memory_;
    QList<Notice> notices_;
    qint64 now_ = 0;
};

} // namespace tacetos::device
