// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "dfuimage.h"
#include "dfuprotocol.h"
#include "filefault.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

// What happens around the DFU transfer of the iPod Classic
// (docs/specs/bootloader-classic.md, sections 14.2, 15.1 and 16): the eject, the
// programs that must be paused, the wait for the buttons to be released, the
// state after the restart and the retries. Nothing here talks to the system: it
// all goes through ClassicSystemLayer, and the tests play it with a fake one.

namespace tacetos::installer {

enum class HostSystem { Linux, Windows, MacOs };

struct UsbDeviceInfo
{
    UsbDeviceId id;
    // Where the device is plugged. A reset of the iPod gives it a new address.
    int bus = 0;
    int address = 0;

    bool sameSocket(const UsbDeviceInfo &o) const { return bus == o.bus && address == o.address; }
};

struct SystemProcess
{
    qint64 id = 0;
    QString name;

    bool operator==(const SystemProcess &o) const { return id == o.id && name == o.name; }
};

// Everything the flow asks of the system. A real one exists per system.
class ClassicSystemLayer
{
public:
    virtual ~ClassicSystemLayer() = default;

    virtual HostSystem hostSystem() const = 0;
    virtual QList<UsbDeviceInfo> listUsbDevices() = 0;
    // The link to one device found by `listUsbDevices`. It is not opened yet.
    virtual std::unique_ptr<UsbDfuLink> makeLink(const UsbDeviceInfo &device) = 0;
    // Section 15.1, step 1: unmounts every partition of the iPod, through the helper.
    virtual bool unmountIpod(QString *error) = 0;
    // Whether the system still lists the iPod as a disk.
    virtual bool ipodListed() = 0;
    // The programs of Apple that may hold the iPod (sections 15.3 and 15.4).
    virtual QList<SystemProcess> findAppleProcesses() = 0;
    virtual bool pauseProcess(const SystemProcess &process, QString *error) = 0;
    virtual bool resumeProcess(const SystemProcess &process, QString *error) = 0;
};

struct AroundOptions
{
    // How the flow waits. Tests replace it by a recorder.
    std::function<void(int milliseconds)> sleep;
    // Asked between the steps and at every tick of a wait; true stops the flow.
    std::function<bool()> cancelled;
    // A line for the user: the countdown, "Close the programs using the iPod".
    std::function<void(const QString &)> notify;
};

// Section 15.1, steps 1 and 2.
struct EjectResult
{
    bool ok = false;
    bool cancelled = false;
    QString message;
    // The number of `unmountIpod` calls.
    int attempts = 0;
};

// Unmounts, trying again every 3 seconds, then waits until the system no longer
// lists the iPod. The step is never skipped.
EjectResult ejectIpod(ClassicSystemLayer &layer, const AroundOptions &options = {});

// Step 2: about 18 seconds on a model with a hard disk, nothing on a flash model.
int spinDownWaitMs(bool hasHardDisk);

struct PauseResult
{
    bool ok = false;
    QString message;
    QStringList names;
};

// Step 3 and the end of section 15.1: the programs paused for the transfer. They
// are listed in a state file before the first one is paused, and resumed by
// `resumeAll`, which the destructor calls: success, failure and cancel all end
// there. After a crash, `resumeLeftover` does it at the next start.
class PausedProcesses
{
public:
    PausedProcesses(ClassicSystemLayer &layer, QString stateFile, FileFault fault = {});
    ~PausedProcesses();
    PausedProcesses(const PausedProcesses &) = delete;
    PausedProcesses &operator=(const PausedProcesses &) = delete;

    PauseResult pauseAll();
    // Resumes what this object paused and deletes the state file. Safe to call twice.
    void resumeAll();
    QList<SystemProcess> paused() const { return m_paused; }

private:
    ClassicSystemLayer &m_layer;
    QString m_stateFile;
    FileFault m_fault;
    QList<SystemProcess> m_paused;
};

struct ResumeResult
{
    // The processes resumed.
    QList<SystemProcess> resumed;
    // Listed in the file but not running any more, or running under another name:
    // nothing is sent to them.
    QList<SystemProcess> gone;
    // Still paused because the system refused: they stay in the file.
    QList<SystemProcess> failed;
};

// The next start after a crash: resumes the processes of a state file left behind,
// before anything else. A process that is not listed under the same id and name
// is left alone, because the id may belong to another program now.
ResumeResult resumeLeftover(ClassicSystemLayer &layer, const QString &stateFile);

// Section 16.1.
struct DfuWaitResult
{
    bool ok = false;
    bool cancelled = false;
    QString message;
    // The iPod to send to, with the address it had at the end of the wait.
    UsbDeviceInfo device;
    // The resets seen during the wait.
    int resets = 0;
};

// Step 1 of section 5 with the wait of section 16.1: waits for `05ac:1223`,
// says to release the buttons, counts 10 seconds, and checks that the iPod is still
// at the same address. A reset starts the countdown again; the third one in a row stops.
DfuWaitResult waitForQuietDfu(ClassicSystemLayer &layer, const AroundOptions &options = {});

enum class ReturnedMode {
    Normal, // `05ac:1261`
    Dfu,    // `05ac:1223` again, after the device had gone
    Wtf,
    Nothing, // nothing in 60 seconds
};

// Step 8 of section 5: watches USB for the iPod to come back.
ReturnedMode watchAfterTransfer(ClassicSystemLayer &layer, const AroundOptions &options = {});

// Section 16.2. The state is read by the detection, after the restart.
enum class BootloaderState { Present, Absent, Unknown };
enum class ClassicOperation { Install, Uninstall };

enum class AfterAction {
    Done,
    OfferRetryOnce, // changed nothing, as before: shows the tones
    ShowCheckByHand,
    ShowRestore, // does not come back: reset, DFU, Apple's software
    ButtonsHeld, // back in DFU mode: not an error of the install
    ResetWrongMode,
};

struct AfterTransfer
{
    AfterAction action = AfterAction::ShowRestore;
    QString message;
    // The record of the install: written after an install, deleted after an uninstall.
    bool writeRecord = false;
    bool deleteRecord = false;
    // The iPod is as expected and nothing is left to do or to try.
    bool success = false;
};

// The table of section 16.2. `before` is the state seen before the transfer, and
// `now` the state read after the restart (only used when `mode` is `Normal`).
AfterTransfer classifyAfterTransfer(ClassicOperation operation, BootloaderState before,
                                    ReturnedMode mode, BootloaderState now);

// Section 16.3.
enum class ClassicFailureKind {
    BeforePayload, // a failure of the USB steps: the iPod is untouched
    OneTone,
    TwoTones,
    ThreeTones,
    NoRestart,
};

struct RetryAdvice
{
    // The app offers "try again" once more.
    bool mayRetry = false;
    // The app still offers to install (false after a three-tone error or no restart).
    bool mayInstall = true;
    // Only the restore by Apple's software is shown.
    bool showRestore = false;
    bool askToReport = false;
    QString message;
};

class ClassicRetryPolicy
{
public:
    RetryAdvice record(ClassicFailureKind kind);
    void recordSuccess() { m_oneTone = 0; }
    bool mayInstall() const { return m_mayInstall; }

private:
    int m_oneTone = 0;
    bool m_mayInstall = true;
};

// Section 14.2: what the helper runs on an uninstaller image just before sending
// it. The image is the 0x30C bytes of the prefix and the uninstaller program of
// `payloadLength` bytes, nothing else, and never more than 16 KiB of program; so an
// installer image given by mistake is refused.
DfuSendCheck checkUninstallerImageReadyToSend(const QByteArray &image, qint64 payloadLength);

// Sections 7 and 15: the message for a code of the access layer, and the way out
// for the system. `others` are the names of the programs found.
struct UsbAccessMessage
{
    QString message;
    QString hint;
};
UsbAccessMessage describeAccessError(UsbAccessError code, HostSystem system,
                                     const QStringList &others = {});

// One whole run of a transfer.
struct ClassicDfuJob
{
    ClassicOperation operation = ClassicOperation::Install;
    // Without the checksum, as `sendDfuImage` takes it.
    QByteArray image;
    // The length of the program of the same parts: the installer's for an install,
    // the uninstaller's for an uninstall.
    qint64 payloadLength = 0;
    bool hasHardDisk = true;
    // Where the paused programs are noted (section 15.1).
    QString stateFile;
    // Tests only: makes the write of the state file fail (a full folder).
    FileFault stateFault;
    BootloaderState stateBefore = BootloaderState::Unknown;
    // Reads the state after the restart (the detection). Unknown when empty.
    std::function<BootloaderState()> readState;
    DfuSendOptions send;
};

enum class ClassicStage { Check, Eject, Pause, WaitDevice, Transfer, Watch, Done };

struct ClassicDfuOutcome
{
    bool ok = false;
    bool cancelled = false;
    ClassicStage stage = ClassicStage::Check;
    QString message;
    // The way out, when there is one.
    QString hint;
    DfuTransferResult transfer;
    ReturnedMode returned = ReturnedMode::Nothing;
    AfterTransfer after;
};

// The sequence of sections 5 and 15.1: checks the image (rule 5 for an install),
// ejects, waits for the disk to stop, pauses Apple's programs, waits for the iPod
// in DFU mode, sends, resumes what was paused in every case, and reads what the
// iPod does after. Nothing is sent when a step before fails.
ClassicDfuOutcome runClassicDfu(ClassicSystemLayer &layer, const ClassicDfuJob &job,
                                const AroundOptions &options = {});

} // namespace tacetos::installer
