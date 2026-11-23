// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "diagnose.h"
#include "format.h"
#include "helpercalls.h"
#include "package.h"
#include "step.h"
#include "wizardconstants.h"

#include <QString>
#include <QStringList>

// The step `classic-bootloader`: the bootloader of the iPod Classic over DFU
// mode (docs/specs/install-wizard.md section 9).

namespace tacetos::wizard {

// Section 9.3: what `stateBefore` of `classic-dfu` is, from the install records
// of this serial (`present`, `absent` or `unknown`). `files` are the paths of
// `install-record.json` files; the last record of the serial decides.
QString classicStateBefore(const QStringList &files, const QString &serial);

// D45: the Classic gives another serial under TacetOS than under Apple's firmware,
// and the install record keeps that second one. Returns the serial of the last
// record that saw `seen` under TacetOS, or empty when none did.
QString appleSerialOf(const QStringList &files, const QString &seen);

// What the step is made from. The app gives the real ones, the tests their own.
class ClassicDfuSources
{
public:
    virtual ~ClassicDfuSources() = default;
    virtual HelperCommands &helper() const = 0;
    virtual IpodSightWatch &sight() const = 0;
    // Section 9.1: what the conditions of `bootloader-classic.md` section 3 are
    // checked from.
    virtual const DiagnoseSources &diagnose() const = 0;
    // The `bootloader_target` of the model of the iPod in `models.toml`.
    virtual QString target() const = 0;
    // The hidden folder of the firmware of this model (`firmwareFolder` of its
    // model in `models.toml`).
    virtual QString firmwareFolder() const { return QString(device::kFirmwareFolder); }
    // The mount point of the music volume, where the firmware was copied.
    virtual QString volumeRoot() const = 0;
    // Section 9.2 step 3: the data folder of the app (not the backup).
    virtual QString dataDir() const = 0;
    // The port of the iPod in detection. Empty: every iPod seen counts.
    virtual QString portPath() const { return QString(); }
    // Section 9.2 step 2: the `install-record.json` files of the Classics.
    virtual QStringList installRecords() const { return {}; }
    // Section 9.2 step 4: how long the iPod has to come back after the transfer.
    virtual qint64 returnMs() const { return kRestartWindowMs; }
};

// Section 10 step 3: the same steps run to remove the bootloader. The package
// is not looked at, and the end is the iPod back with Apple's firmware.
enum class ClassicOperation { Install, Uninstall };

// Section 9. It waits for the user (`WaitKind::ReadyForDfu`, with the `model`
// and a `reason`: empty the first time, then `disk-mode-instead` or
// `dfu-failed` with the `hint`), sends `classic-dfu` and relays its `progress`.
// The stage `dfu-detected` is given once detection shows the iPod in DFU mode.
// Then it waits for the iPod to come back. Facts: `record`, the install
// record, and `notTaken`, how many times the iPod came back unchanged. For an
// uninstall (section 10) there is no record, and the fact is `notRemoved`: how
// many times the iPod came back with TacetOS (`still-tacetos`). When there is no
// firmware folder and no install record, it first asks (`WaitKind::Offer`, with
// `offer` = `remove-anyway`), and the flow stops if the answer is no.
class ClassicDfuStep : public Step
{
public:
    explicit ClassicDfuStep(const ClassicDfuSources &sources,
                            ClassicOperation operation = ClassicOperation::Install)
        : m_sources(sources), m_operation(operation)
    {
    }
    StepOutcome run(StepContext &context) override;

private:
    std::optional<StepOutcome> conditions(StepContext &context) const;
    StepOutcome comeBack(StepContext &context, QList<IpodSight> &pending);
    bool writeRecord(StepContext &context, const PackageFiles &files) const;

    const ClassicDfuSources &m_sources;
    ClassicOperation m_operation;
    // The serial the iPod gave when it came back under TacetOS, for the record.
    QString m_returnedSerial;
};

} // namespace tacetos::wizard
