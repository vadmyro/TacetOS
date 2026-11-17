// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "helpercalls.h"
#include "step.h"
#include "wizardconstants.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>
#include <functional>
#include <optional>

// The steps `format` and `convert` of a clean install
// (docs/specs/install-wizard.md section 6).

namespace tacetos::wizard {

// Section 6.2: the label of the new file system: the old one when it is a valid
// FAT32 label once turned to uppercase, else `IPOD`.
QString newVolumeLabel(const QString &oldLabel);

// Section 6.3: the music volume as detection shows it.
struct MusicVolume
{
    QString mountPoint;
    QString filesystem;
    QString label;
};

// Section 6.3 step 3: FAT32, and nothing in it but what the systems make on a
// volume they mount.
bool isEmptyFat32(const MusicVolume &volume);

// Section 6.3 step 3: the music volume coming back after a format.
class VolumeWatch
{
public:
    virtual ~VolumeWatch() = default;
    // What was seen of the iPod of `serial` no longer counts: its volume is
    // about to go away.
    virtual void forget(const QString &serial) = 0;
    // Asks the disk service to mount the music volume of the iPod of `serial`.
    virtual void mount(const QString &serial) = 0;
    // Waits until detection shows the iPod of `serial` ready with a mounted
    // volume, for at most `ms`. Empty when it did not come, or once `stop`
    // says so.
    virtual std::optional<MusicVolume> waitReady(const QString &serial, qint64 ms,
                                                 const std::function<bool()> &stop) = 0;
};

// The real one: fed by the signals of detection in the thread of the screen,
// waited for in the thread of the runner. The mounter is given by the app; it
// is called in the thread of the runner.
class DetectionVolumeWatch : public QObject, public VolumeWatch
{
    Q_OBJECT

public:
    using Mounter = std::function<void(const QString &serial)>;
    explicit DetectionVolumeWatch(Mounter mounter, QObject *parent = nullptr);

    void forget(const QString &serial) override;
    void mount(const QString &serial) override;
    std::optional<MusicVolume> waitReady(const QString &serial, qint64 ms,
                                         const std::function<bool()> &stop) override;

public slots:
    // Connected to `LiveTracker::ipodAdded` and `LiveTracker::ipodChanged`.
    void seen(const tacetos::device::DetectedIpod &ipod);

private:
    Mounter m_mounter;
    QMutex m_mutex;
    QWaitCondition m_changed;
    // By serial: the volume of each iPod shown ready.
    QHash<QString, MusicVolume> m_ready;
};

// What the steps are made from. The app gives the real ones, the tests their own.
class FormatSources
{
public:
    virtual ~FormatSources() = default;
    virtual HelperCommands &helper() const = 0;
    virtual VolumeWatch &volumes() const = 0;
    // Section 6.2: the label of the music volume as the system gives it, empty
    // when there is none.
    virtual QString volumeLabel() const = 0;
    // Section 6.2: the name of the model in `models.toml` ("iPod Classic").
    virtual QString modelName() const = 0;
    virtual qint64 volumeReturnMs() const { return kVolumeReturnMs; }
};

// The steps `format` (`convert` false) and `convert`. They wait for the typed
// name (`WaitKind::TypedName`, with `ipodName`, `files` and `bytes` from the
// manifest of the backup), send the command of the helper, then wait for the
// volume to come back. On an iPod Classic, `format` empties the volume through
// the file system instead (section 6.4).
class FormatStep : public Step
{
public:
    FormatStep(bool convert, const FormatSources &sources) : m_convert(convert), m_sources(sources)
    {
    }
    StepOutcome run(StepContext &context) override;

private:
    std::optional<StepOutcome> resumed(StepContext &context);
    StepOutcome volumeBack(StepContext &context);
    StepOutcome emptyByFiles(StepContext &context, const QString &name, const QString &typed);

    bool m_convert;
    const FormatSources &m_sources;
};

} // namespace tacetos::wizard
