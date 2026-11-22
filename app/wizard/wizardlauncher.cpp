// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/wizardlauncher.h"

#include "backup.h"
#include "checks.h"
#include "classicdfu.h"
#include "diagnose.h"
#include "helpercalls.h"
#include "helperclient.h"
#include "journal.h"
#include "live.h"
#include "runner.h"
#include "stepfactory.h"
#include "wizard/wizardwindow.h"
#include "wizardconstants.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSysInfo>
#include <QThread>

namespace tacetos {

using namespace wizard;

struct WizardLauncher::Session
{
    // In the order they need each other; destroyed in the opposite one.
    std::unique_ptr<LinuxStepFactory> factory;
    std::unique_ptr<WizardRunner> runner;
    QPointer<WizardWindow> window;
};

namespace {

// How long the action `mount-volume` waits for detection to see the volume it mounted.
constexpr int kVolumeSeenMs = 10 * 1000;

// The `install-record.json` files of the backups of this serial (install-wizard.md section 9.2
// step 2).
QStringList installRecordsOf(const QString &backupRoot, const QString &serial)
{
    QStringList found;
    const QDir serialFolder(QDir(backupRoot.isEmpty() ? defaultBackupRoot() : backupRoot)
                                .filePath(serialFolderName(serial)));
    const QStringList dates = serialFolder.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &date : dates) {
        const QString path =
            serialFolder.filePath(date + QLatin1Char('/') + QString(kInstallRecordFile));
        if (QFileInfo(path).isFile())
            found.append(path);
    }
    return found;
}

// Every `install-record.json` of the backups, whatever the serial (D45).
QStringList allInstallRecords(const QString &backupRoot)
{
    QStringList found;
    const QDir root(backupRoot.isEmpty() ? defaultBackupRoot() : backupRoot);
    for (const QString &serial : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir serialFolder(root.filePath(serial));
        for (const QString &date : serialFolder.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path =
                serialFolder.filePath(date + QLatin1Char('/') + QString(kInstallRecordFile));
            if (QFileInfo(path).isFile())
                found.append(path);
        }
    }
    return found;
}

// Helper section 5.1: a real disk, named by what detection has seen.
QJsonObject diskTarget(const device::DetectedIpod &ipod)
{
    QJsonObject target;
    target.insert(QStringLiteral("kind"), QStringLiteral("disk"));
    target.insert(QStringLiteral("node"), ipod.disk ? ipod.disk->node : QString());
    target.insert(QStringLiteral("sizeBytes"), double(ipod.disk ? ipod.disk->sizeBytes : 0));
    target.insert(QStringLiteral("serial"), ipod.serial);
    target.insert(QStringLiteral("port"), ipod.portPath);
    return target;
}

QString resultWord(CheckResult result)
{
    switch (result) {
    case CheckResult::Pass:
        return QStringLiteral("pass");
    case CheckResult::Warn:
        return QStringLiteral("warn");
    case CheckResult::Fail:
        return QStringLiteral("fail");
    case CheckResult::Ask:
        return QStringLiteral("ask");
    case CheckResult::Skipped:
        break;
    }
    return QStringLiteral("skipped");
}

} // namespace

WizardLauncher::WizardLauncher(const Config &config, device::LiveTracker &tracker,
                               journal::Journal *journal, QObject *parent)
    : QObject(parent), config_(config), tracker_(tracker), journal_(journal),
      client_(std::make_unique<helper::HelperClient>())
{
    if (!config_.helperProgram.isEmpty())
        client_->setProgram(config_.helperProgram);
    client_->setJournal(journal_);
    helper::HelperClient *client = client_.get();
    manager_ = std::make_unique<WizardManager>(
        config_.dataDir, [client](const QString &cmd, const QJsonObject &args) {
            return callHelper(*client, cmd, args);
        });
    // Section 6.10 of the helper spec: before any other command.
    if (helperFound())
        manager_->resumePausedAtStart();
    connect(&tracker_, &device::LiveTracker::ipodAdded, this,
            [this](const device::DetectedIpod &ipod) {
                if (!manager_->proposeResume({flowSerial(ipod)}).isEmpty())
                    emit stateChanged(ipod.portPath);
            });
}

WizardLauncher::~WizardLauncher()
{
    // The runners stop first (a flow that is cut is resumed later), then the helper. The table
    // is emptied before, so the windows that go do not look for their session.
    const auto sessions = std::move(sessions_);
    sessions_.clear();
    for (const std::shared_ptr<Session> &session : sessions) {
        delete session->window.data();
        session->runner.reset();
    }
    client_->shutdown();
}

bool WizardLauncher::helperFound() const
{
    const QString program = config_.helperProgram.isEmpty() ? helper::HelperClient::defaultProgram()
                                                            : config_.helperProgram;
    return QFileInfo(program).isFile();
}

bool WizardLauncher::realWrites() const
{
    return installer::realWritesAllowed() && helperFound();
}

WizardWindow *WizardLauncher::open(const QString &port, HomeAction action)
{
    std::optional<device::DetectedIpod> found;
    for (const device::DetectedIpod &ipod : tracker_.ipods()) {
        if (ipod.portPath == port)
            found = ipod;
    }
    if (!found || found->serial.isEmpty())
        return nullptr;
    const device::DetectedIpod ipod = *found;
    // The helper and the mount keep the serial detection sees; the rest the one of the flow.
    const QString serial = flowSerial(ipod);

    if (manager_->show(serial))
        return manager_->windowFor(serial);
    if (action == HomeAction::ShowWizard)
        return nullptr;
    // The diagnosis comes first: the hello of the helper is there before its check.
    client_->ensureStarted();

    device::Model model;
    for (const device::Model &candidate : config_.models) {
        if (candidate.id == ipod.model)
            model = candidate;
    }
    const Family family =
        model.family == QLatin1String("classic") ? Family::Dfu : Family::Partition;

    LinuxSourcesConfig sources;
    sources.serial = serial;
    sources.model = model;
    sources.family = family;
    sources.backupRoot = config_.backupRoot;
    sources.dataDir = config_.dataDir;
    sources.appVersion = config_.appVersion;
    sources.installRecords = installRecordsOf(config_.backupRoot, serial);
    sources.portPath = ipod.portPath;

    auto session = std::make_shared<Session>();
    session->factory =
        std::make_unique<LinuxStepFactory>(sources, *client_, diskTarget(ipod), ipod.portPath);
    session->factory->connectDetection(tracker_);
    session->factory->feed(ipod);
    session->runner = std::make_unique<WizardRunner>(config_.dataDir, session->factory->factory());
    session->runner->setAppVersion(config_.appVersion);

    WizardTarget target;
    target.serial = serial;
    target.model = ipod.model;
    target.name = model.name.isEmpty() ? ipod.model : model.name;
    target.bootloaderTarget = model.bootloaderTarget;
    target.family = family;
    target.macFormat = ipod.volume && (ipod.volume->filesystem == QLatin1String("hfsplus") ||
                                       ipod.volume->filesystem == QLatin1String("hfs"));
    if (ipod.volume)
        target.volumeUsed = qMax<qint64>(0, ipod.volume->totalBytes - ipod.volume->freeBytes);
    if (helperFound() && family == Family::Partition)
        target.partitionBytes = firmwarePartitionSize(session->factory->commands());
    if (ipod.volume && !ipod.volume->mountPoint.isEmpty()) {
        const qint64 need = session->factory->diagnoseSources().backupNeedBytes(InstallPath::Keep);
        target.keepBytes = qMax<qint64>(
            0, need - backupSpaceNeeded(BackupKind::Keep, 0, 0, target.partitionBytes));
    }

    helper::HelperClient *client = client_.get();
    WizardOptions options;
    options.dataDir = config_.dataDir;
    options.backupRoot = config_.backupRoot;
    options.realWrites = realWrites();
    // Helper section 6.11: the rule of the Classic in DFU mode, asked through the helper.
    options.udevInstaller = [client] {
        return callHelper(*client, QStringLiteral("install-udev-rule"), QJsonObject()).code;
    };
    // A volume left unmounted by an eject stays so: the check offers to mount it, then
    // waits for detection to see it ready, so that the checks that run again find it.
    const std::weak_ptr<Session> weak = session;
    options.volumeMounter = [weak, serial = ipod.serial] {
        device::DiskResult mounted;
        if (const std::shared_ptr<Session> s = weak.lock())
            mounted = s->factory->mounter().mount(serial);
        if (!mounted.ok())
            return mounted.detail.isEmpty() ? device::diskErrorMessage(mounted.error)
                                            : mounted.detail;
        const QDeadlineTimer deadline(kVolumeSeenMs);
        while (!deadline.hasExpired()) {
            const std::shared_ptr<Session> s = weak.lock();
            if (!s)
                break;
            if (s->factory->view().current().state == device::IpodState::Ready)
                break;
            QThread::msleep(100);
        }
        return QString();
    };

    // What the report holds beside the facts of the window (screens section 10).
    options.reportInput = [this] {
        journal::ReportInput in;
        in.appVersion = config_.appVersion;
        in.system = QSysInfo::prettyProductName();
        in.dataFolder = config_.dataDir;
        in.home = QDir::homePath();
        in.hostName = QSysInfo::machineHostName();
        if (journal_)
            in.entries = journal_->entries();
        return in;
    };

    const WizardEntry entry = action == HomeAction::UninstallWizard ? WizardEntry::Uninstall
                              : action == HomeAction::ResumeWizard  ? WizardEntry::Resume
                                                                    : WizardEntry::Install;
    auto *window = new WizardWindow(target, options, session->runner.get(), entry);
    window->setAttribute(Qt::WA_DeleteOnClose);
    session->window = window;
    sessions_.insert(serial, session);
    manager_->adopt(window);
    connect(window, &QObject::destroyed, this, [this, serial, port] {
        closeSession(serial);
        emit stateChanged(port);
    });
    if (journal_)
        journal_->add(journal::Level::Info, journal::Domain::App, port,
                      QStringLiteral("wizard opened: model %1, real writes %2")
                          .arg(ipod.model,
                               options.realWrites ? QStringLiteral("on") : QStringLiteral("off")));
    if (journal_) {
        // What the checks found and why a step stopped, so that a report says it without the
        // screen.
        connect(session->runner.get(), &WizardRunner::checksReported, this,
                [this, port](const QList<Check> &checks) {
                    for (const Check &check : checks) {
                        if (check.result == CheckResult::Pass)
                            continue;
                        journal_->add(journal::Level::Info, journal::Domain::App, port,
                                      QStringLiteral("check %1: %2, %3")
                                          .arg(check.id, resultWord(check.result), check.message));
                    }
                    int passed = 0;
                    for (const Check &check : checks)
                        passed += check.result == CheckResult::Pass ? 1 : 0;
                    journal_->add(
                        journal::Level::Info, journal::Domain::App, port,
                        QStringLiteral("checks: %1 of %2 passed").arg(passed).arg(checks.size()));
                });
        // A step that waits again says why (a DFU try that failed, the iPod back as a disk):
        // the reason and the helper's detail, which the screen alone showed.
        connect(session->runner.get(), &WizardRunner::stepWaiting, this,
                [this, port](const QString &name, WaitKind, const QJsonObject &info) {
                    const QString reason = info.value(QStringLiteral("reason")).toString();
                    if (reason.isEmpty())
                        return;
                    const QString hint = info.value(QStringLiteral("hint")).toString();
                    const QString usb = info.value(QStringLiteral("usb")).toString();
                    journal_->add(
                        journal::Level::Warning, journal::Domain::App, port,
                        QStringLiteral("step %1 waits again: %2%3%4")
                            .arg(name, reason,
                                 hint.isEmpty() ? QString()
                                                : QStringLiteral(" (") + hint + QLatin1Char(')'),
                                 usb.isEmpty()
                                     ? QString()
                                     : QStringLiteral(" [USB: ") + usb + QLatin1Char(']')));
                });
        // Each step says when it starts and when it is done, a success included.
        connect(session->runner.get(), &WizardRunner::stepStarted, this,
                [this, port](int, const QString &name) {
                    journal_->add(journal::Level::Info, journal::Domain::App, port,
                                  QStringLiteral("step %1 started").arg(name));
                });
        connect(session->runner.get(), &WizardRunner::stepDone, this,
                [this, port](int, const QString &name) {
                    journal_->add(journal::Level::Info, journal::Domain::App, port,
                                  QStringLiteral("step %1 done").arg(name));
                });
        connect(session->runner.get(), &WizardRunner::stepFailed, this,
                [this, port](int, const QString &name, const QString &code, const QString &detail) {
                    journal_->add(journal::Level::Warning, journal::Domain::App, port,
                                  QStringLiteral("step %1 failed: %2%3")
                                      .arg(name, code,
                                           detail.isEmpty()
                                               ? QString()
                                               : QStringLiteral(" (") + detail + QLatin1Char(')')));
                });
    }
    window->show();
    emit stateChanged(port);
    return window;
}

// The window is gone: the runner and the sources go with it, in this order.
QString WizardLauncher::flowSerial(const device::DetectedIpod &ipod) const
{
    const QString apple = appleSerialOf(allInstallRecords(config_.backupRoot), ipod.serial);
    return apple.isEmpty() ? ipod.serial : apple;
}

void WizardLauncher::closeSession(const QString &serial)
{
    const auto it = sessions_.find(serial);
    if (it == sessions_.end())
        return;
    const std::shared_ptr<Session> session = it.value();
    sessions_.erase(it);
    session->runner.reset();
    session->factory.reset();
}

} // namespace tacetos
