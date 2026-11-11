// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "merge.h"

#include "constants.h"
#include "firmware.h"
#include "volume.h"

namespace tacetos::device {

namespace {

QStringList intersect(const QStringList &a, const QStringList &b)
{
    QStringList out;
    for (const QString &id : a) {
        if (b.contains(id))
            out << id;
    }
    return out;
}

// Rule 5: the iPod was seen in normal mode on this port a short while ago.
bool remembersNormalMode(const IpodEvidence &ev, const UsbDevice &usb)
{
    if (!ev.previous)
        return false;
    const PreviousIpod &prev = *ev.previous;
    return prev.ipod.mode == UsbMode::Normal && prev.ipod.portPath == usb.portPath &&
           prev.ageMs >= 0 && prev.ageMs <= kModeMemoryMs;
}

} // namespace

MergeResult mergeEvidence(const IpodEvidence &ev, const QList<Model> &models)
{
    MergeResult result;
    DetectedIpod &out = result.ipod;
    QStringList &log = result.log;

    if (ev.usb) {
        out.usbId = ev.usb->usbId;
        out.mode = ev.usb->mode;
        out.portPath = ev.usb->portPath;
        out.serial = ev.usb->serial;
        out.candidates = ev.usb->candidates;
        if (ev.usb->mode == UsbMode::Normal)
            out.running = runningFirmware(ev.usb->manufacturer, ev.usb->product);
    }

    // Rule 5: in DFU mode there is no disk. The fields of the normal mode are
    // kept when the same port was seen in the last minute, and only the
    // candidates are known otherwise.
    if (ev.usb && ev.usb->mode == UsbMode::Dfu) {
        out.state = IpodState::Dfu;
        if (remembersNormalMode(ev, *ev.usb)) {
            const DetectedIpod &prev = ev.previous->ipod;
            out.model = prev.model;
            out.modelSource = prev.modelSource;
            out.variant = prev.variant;
            out.hintModel = prev.hintModel;
            out.volume = prev.volume;
            out.firmware = prev.firmware;
            out.firmwareLine = prev.firmwareLine;
            out.running = prev.running;
            log << QStringLiteral("dfu: fields of the normal mode kept (model %1)").arg(prev.model);
        } else {
            log << QStringLiteral("dfu: no normal mode seen on this port, candidates only");
        }
        return result;
    }

    // A volume that vanished during the read is no volume (section 7.1 rule 6).
    std::optional<VolumeInfo> volume = ev.volume;
    MarkerReading markers = ev.markers;
    if (markers.volumeGone) {
        volume.reset();
        markers = MarkerReading();
    }
    // Nothing is read from a filesystem of the "anything else" row (section 6.2).
    const bool unsupportedFs =
        volume && !volume->filesystem.isEmpty() &&
        classifyFilesystem(volume->filesystem) == FilesystemKind::Unsupported;
    if (unsupportedFs)
        markers = MarkerReading();

    out.disk = ev.disk;
    out.volume = volume;

    // The model that the disk reading names, if any.
    const DiskReading &reading = ev.diskReading;
    const QStringList readingModels = reading.status == DiskReadStatus::Ok
                                          ? modelsForTarget(models, reading.target)
                                          : QStringList();

    // Rule 3: a disk reading outside the candidates.
    const QStringList readingInCandidates = intersect(readingModels, out.candidates);
    const bool conflict =
        !readingModels.isEmpty() && !out.candidates.isEmpty() && readingInCandidates.isEmpty();
    if (conflict) {
        log << QStringLiteral("conflict: usb %1 can be %2, the disk says %3")
                   .arg(out.usbId, out.candidates.join(QLatin1Char(',')),
                        readingModels.join(QLatin1Char(',')));
    }

    // Rules 1 and 2. Rule 1 needs exactly one candidate and a normal mode: an
    // id that is in the lists of two modes is asked about (section 3.2).
    const QStringList infoInCandidates = intersect(markers.modelsFromInfo, out.candidates);
    if (ev.usb && out.candidates.size() == 1 && out.mode == UsbMode::Normal) {
        out.model = out.candidates.first();
        out.modelSource = ModelSource::Usb;
    } else if (ev.usb) {
        if (!readingInCandidates.isEmpty()) {
            out.model = readingInCandidates.first();
            out.modelSource = ModelSource::Disk;
        } else if (infoInCandidates.size() == 1) {
            out.model = infoInCandidates.first();
            out.modelSource = ModelSource::InfoFile;
        }
    } else if (markers.modelsFromInfo.size() == 1) {
        // A folder picked by hand has no USB id: only the info file can say.
        out.model = markers.modelsFromInfo.first();
        out.modelSource = ModelSource::InfoFile;
    }

    // Section 8.1: the user's choice always wins, within what is possible.
    if (!ev.userModel.isEmpty() &&
        (out.candidates.isEmpty() || out.candidates.contains(ev.userModel))) {
        out.model = ev.userModel;
        out.modelSource = ModelSource::User;
    }

    // Rule 4 and the firmware of section 7.4.
    out.firmware = markers.firmware;
    if (!out.model.isEmpty() && !markers.modelsFromInfo.isEmpty() &&
        !markers.modelsFromInfo.contains(out.model)) {
        out.firmware.otherModel = true;
        log << QStringLiteral("other-model: the info file is for %1, the iPod is %2")
                   .arg(markers.modelsFromInfo.join(QLatin1Char(',')), out.model);
    }

    // Section 7.6: the line for the firmware.
    out.firmwareLine = identifyFirmware(out.firmware, markers.appleModelFields, out.running, &log);

    // Section 8.4: the exact iPod, from the variants of the decided model only.
    const qint64 diskBytes = ev.disk ? ev.disk->sizeBytes : 0;
    if (!out.model.isEmpty()) {
        const QString number = markers.appleModelFields.value(QString(kSysInfoModelKey));
        for (const Model &m : models) {
            if (m.id == out.model) {
                out.variant = identifyVariant(m, number, diskBytes);
                log << out.variant.log;
            } else if (!number.isEmpty()) {
                // A clue that points to another model is logged and ignored.
                for (const Variant &v : m.variants) {
                    if (v.appleModels.contains(appleModelNumber(number)))
                        log << QStringLiteral("variant: model number %1 is of %2, the iPod is %3")
                                   .arg(appleModelNumber(number), m.id, out.model);
                }
            }
        }
    } else {
        out.hintModel = modelHintFromCapacity(models, out.candidates, diskBytes);
        if (!out.hintModel.isEmpty())
            log << QStringLiteral("hint: a disk of %1 bytes is usually %2")
                       .arg(diskBytes)
                       .arg(out.hintModel);
    }

    // Section 8.3, first matching row. See the notes under the table of the
    // spec for the three places where "matches" needed a precise meaning.
    const bool unsupportedModel = reading.status == DiskReadStatus::UnknownGeneration ||
                                  (reading.status == DiskReadStatus::Ok && readingModels.isEmpty());
    const bool macFormat = reading.macFormat || (volume && classifyFilesystem(volume->filesystem) ==
                                                               FilesystemKind::HfsPlus);
    const bool waiting = ev.usb && !ev.waitOver && !volume;
    const bool ambiguous = out.model.isEmpty() && (ev.usb || volume) && !waiting;

    if (conflict)
        out.state = IpodState::Conflict;
    else if (unsupportedModel)
        out.state = IpodState::UnsupportedModel;
    else if (reading.status == DiskReadStatus::PermissionDenied)
        out.state = IpodState::Permission;
    else if (ambiguous)
        out.state = IpodState::Ambiguous;
    else if (waiting)
        out.state = IpodState::Waiting;
    else if (ev.usb && !ev.disk)
        out.state = IpodState::NoDisk;
    else if (macFormat)
        out.state = IpodState::MacFormat;
    else if (!volume)
        out.state = IpodState::NoVolume;
    else if (unsupportedFs)
        out.state = IpodState::UnsupportedFilesystem;
    else
        out.state = IpodState::Ready;

    return result;
}

} // namespace tacetos::device
