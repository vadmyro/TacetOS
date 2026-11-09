// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "live.h"

#include "constants.h"

#include <QSet>
#include <algorithm>

namespace tacetos::device {

namespace {

bool sameDisk(const std::optional<DiskInfo> &a, const std::optional<DiskInfo> &b)
{
    if (a.has_value() != b.has_value())
        return false;
    return !a ||
           (a->node == b->node && a->sizeBytes == b->sizeBytes && a->sectorSize == b->sectorSize);
}

bool sameVolume(const std::optional<VolumeInfo> &a, const std::optional<VolumeInfo> &b)
{
    if (a.has_value() != b.has_value())
        return false;
    return !a ||
           (a->mountPoint == b->mountPoint && a->filesystem == b->filesystem &&
            a->label == b->label && a->totalBytes == b->totalBytes && a->freeBytes == b->freeBytes);
}

bool sameIpod(const DetectedIpod &a, const DetectedIpod &b)
{
    return a.usbId == b.usbId && a.mode == b.mode && a.portPath == b.portPath &&
           a.serial == b.serial && a.candidates == b.candidates && a.model == b.model &&
           a.modelSource == b.modelSource && sameDisk(a.disk, b.disk) &&
           sameVolume(a.volume, b.volume) && a.state == b.state &&
           a.firmware.files == b.firmware.files && a.firmware.version == b.firmware.version &&
           a.firmware.otherModel == b.firmware.otherModel;
}

} // namespace

SystemClock::SystemClock()
{
    timer_.start();
}

qint64 SystemClock::nowMs() const
{
    return timer_.elapsed();
}

LiveTracker::LiveTracker(QList<Model> models, const Clock *clock, DeviceSource *source,
                         QObject *parent)
    : QObject(parent), models_(std::move(models)), clock_(clock), source_(source)
{
}

LiveTracker::~LiveTracker() = default;

QList<DetectedIpod> LiveTracker::ipods() const
{
    QList<DetectedIpod> out;
    for (const Entry &entry : entries_)
        out << entry.ipod;
    return out;
}

void LiveTracker::setPolling(bool on)
{
    polling_ = on;
    nextPollUsb_.reset();
    nextPollDisks_.reset();
}

LiveTracker::Entry *LiveTracker::find(const QString &portPath)
{
    for (Entry &entry : entries_) {
        if (entry.usb.portPath == portPath)
            return &entry;
    }
    return nullptr;
}

const LiveTracker::Entry *LiveTracker::find(const QString &portPath) const
{
    return const_cast<LiveTracker *>(this)->find(portPath);
}

void LiveTracker::usbEvent()
{
    now_ = clock_->nowMs();
    readUsbNow();
    expireAndWait();
    flush();
}

void LiveTracker::diskEvent()
{
    now_ = clock_->nowMs();
    readDisksNow();
    expireAndWait();
    flush();
}

void LiveTracker::refresh()
{
    now_ = clock_->nowMs();
    readUsbNow();
    readDisksNow();
    expireAndWait();
    flush();
}

void LiveTracker::setHeld(const QString &portPath, bool held)
{
    if (Entry *entry = find(portPath))
        entry->held = held;
}

void LiveTracker::tick()
{
    now_ = clock_->nowMs();
    if (polling_) {
        if (!nextPollUsb_ || now_ >= *nextPollUsb_) {
            readUsbNow();
            nextPollUsb_ = now_ + kPollUsbMs;
        }
        if (!nextPollDisks_ || now_ >= *nextPollDisks_) {
            readDisksNow();
            nextPollDisks_ = now_ + kPollDisksMs;
        }
    }
    expireAndWait();
    flush();
}

std::optional<qint64> LiveTracker::nextWakeMs() const
{
    std::optional<qint64> next;
    const auto consider = [&next](qint64 when) {
        if (!next || when < *next)
            next = when;
    };
    if (polling_) {
        consider(nextPollUsb_.value_or(clock_->nowMs()));
        consider(nextPollDisks_.value_or(clock_->nowMs()));
    }
    for (const Entry &entry : entries_) {
        if (entry.missingSinceMs)
            consider(*entry.missingSinceMs + kSamePortWindowMs + 1);
        else if (!entry.waitOver && entry.usb.mode != UsbMode::Dfu)
            consider(entry.firstSeenMs + kDiskWaitMs);
    }
    return next;
}

// Section 9.1: the list is what counts. A new device is added at once, with
// what the readers already see; one that is gone is dropped, or kept for the
// same port window when a flow may bring it back (section 9.4).
void LiveTracker::readUsbNow()
{
    const std::optional<QList<UsbDevice>> devices = source_->readUsb();
    if (!devices)
        return;

    for (const UsbDevice &device : *devices) {
        Entry *entry = find(device.portPath);
        if (!entry) {
            Entry fresh;
            fresh.usb = device;
            fresh.firstSeenMs = now_;
            if (device.mode != UsbMode::Dfu)
                fresh.reading = source_->readPort(device);
            entries_ << fresh;
            remerge(entries_.last());
            notices_ << Notice{Notice::Kind::Added, device.portPath, entries_.last().ipod};
            continue;
        }
        entry->missingSinceMs.reset();
        const bool otherMode = entry->usb.usbId != device.usbId;
        entry->usb = device;
        if (otherMode) {
            // Section 9.4 rule 2: the same iPod in another mode. The wait
            // starts again, and what the disk showed no longer applies.
            entry->firstSeenMs = now_;
            entry->waitOver = false;
            entry->reading = PortReading();
            if (device.mode != UsbMode::Dfu)
                entry->reading = source_->readPort(device);
        }
        // A DFU iPod has nothing new to read, and its kept fields must not be
        // lost when the memory of the normal mode runs out.
        if (otherMode || device.mode != UsbMode::Dfu)
            remerge(*entry);
    }

    QSet<QString> present;
    for (const UsbDevice &device : *devices)
        present.insert(device.portPath);
    for (qsizetype i = entries_.size() - 1; i >= 0; --i) {
        Entry &entry = entries_[i];
        if (present.contains(entry.usb.portPath) || entry.missingSinceMs)
            continue;
        if (entry.held || entry.ipod.state == IpodState::Dfu) {
            entry.missingSinceMs = now_;
        } else {
            // Section 9.3: nothing is running, so it leaves now.
            notices_ << Notice{Notice::Kind::Removed, entry.usb.portPath, DetectedIpod()};
            entries_.removeAt(i);
        }
    }
}

void LiveTracker::readDisksNow()
{
    for (Entry &entry : entries_) {
        if (entry.missingSinceMs || entry.usb.mode == UsbMode::Dfu)
            continue;
        entry.reading = source_->readPort(entry.usb);
        remerge(entry);
    }
}

void LiveTracker::expireAndWait()
{
    for (qsizetype i = entries_.size() - 1; i >= 0; --i) {
        Entry &entry = entries_[i];
        if (entry.missingSinceMs) {
            if (now_ - *entry.missingSinceMs > kSamePortWindowMs) {
                notices_ << Notice{Notice::Kind::Removed, entry.usb.portPath, DetectedIpod()};
                entries_.removeAt(i);
            }
            continue;
        }
        if (!entry.waitOver && entry.usb.mode != UsbMode::Dfu &&
            now_ - entry.firstSeenMs >= kDiskWaitMs) {
            entry.waitOver = true;
            remerge(entry);
        }
    }
    for (auto it = memory_.begin(); it != memory_.end();) {
        if (now_ - it->seenMs > kModeMemoryMs)
            it = memory_.erase(it);
        else
            ++it;
    }
}

void LiveTracker::remerge(Entry &entry)
{
    IpodEvidence evidence;
    evidence.usb = entry.usb;
    evidence.disk = entry.reading.disk;
    evidence.volume = entry.reading.volume;
    evidence.diskReading = entry.reading.diskReading;
    evidence.markers = entry.reading.markers;
    evidence.waitOver = entry.waitOver;
    const auto saved = memory_.constFind(entry.usb.portPath);
    if (saved != memory_.constEnd()) {
        PreviousIpod previous;
        previous.ipod = saved->ipod;
        previous.ageMs = now_ - saved->seenMs;
        evidence.previous = previous;
    }

    const DetectedIpod before = entry.ipod;
    entry.ipod = mergeEvidence(evidence, models_).ipod;
    remember(entry);
    // A new entry is announced by the caller, with its first value.
    if (before.portPath.isEmpty())
        return;
    if (!sameIpod(before, entry.ipod))
        notices_ << Notice{Notice::Kind::Changed, entry.usb.portPath, entry.ipod};
}

// Section 8.2 rule 5: what was seen in normal mode, and when it was last seen.
// A reading that is only "waiting" has nothing worth keeping.
void LiveTracker::remember(const Entry &entry)
{
    if (entry.usb.mode != UsbMode::Normal)
        return;
    Memory &slot = memory_[entry.usb.portPath];
    if (entry.ipod.state != IpodState::Waiting || slot.ipod.portPath.isEmpty())
        slot.ipod = entry.ipod;
    slot.seenMs = now_;
}

// One signal per iPod and kind, with the value it has now: a reading that went
// through two steps in one call is announced once.
void LiveTracker::flush()
{
    if (notices_.isEmpty())
        return;
    const QList<Notice> notices = std::exchange(notices_, {});
    QSet<QString> added;
    QSet<QString> changed;
    for (const Notice &notice : notices) {
        if (notice.kind == Notice::Kind::Removed)
            emit ipodRemoved(notice.portPath);
    }
    for (const Notice &notice : notices) {
        const Entry *entry = find(notice.portPath);
        if (notice.kind != Notice::Kind::Added || !entry)
            continue;
        added.insert(notice.portPath);
        emit ipodAdded(entry->ipod);
    }
    for (const Notice &notice : notices) {
        const Entry *entry = find(notice.portPath);
        if (notice.kind != Notice::Kind::Changed || !entry || added.contains(notice.portPath) ||
            changed.contains(notice.portPath))
            continue;
        changed.insert(notice.portPath);
        emit ipodChanged(entry->ipod);
    }
    emit listChanged();
}

} // namespace tacetos::device
