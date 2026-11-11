// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mainwindow.h"

#include "home/homescreen.h"

#include <QDesktopServices>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#ifdef Q_OS_LINUX
#include "linuxsource.h"
#include "mountwatcher.h"
#ifdef TACETOS_HAVE_UDEV
#include "udevmonitor.h"
#endif
#endif

namespace tacetos {

using namespace device;

namespace {

// The model list is embedded (app/CMakeLists.txt), so the app never reads the source tree.
const QString kModelsResource = QStringLiteral(":/data/models.toml");

// Used where the disk service does not exist: every operation fails with the same error.
class NoDiskActions : public DiskActions
{
public:
    DiskResult run(OperationKind, const QString &) override
    {
        DiskResult result;
        result.error = DiskError::ServiceMissing;
        result.detail = QStringLiteral("no disk service on this system");
        return result;
    }
};

// Section 3.1 of the state spec: the domain and the level of a line the device code sends. The
// lines are English and for people who read logs, so this only sorts them.
journal::Domain domainOfDetectionLine(const QString &port, const QString &line)
{
    if (port.isEmpty() || line.startsWith(QLatin1String("added")) ||
        line.startsWith(QLatin1String("removed")))
        return journal::Domain::Usb;
    return journal::Domain::Merge;
}

journal::Level levelOfOperationLine(const QString &line)
{
    if (line.contains(QLatin1String(": failed")))
        return journal::Level::Error;
    if (line.contains(QLatin1String("refused")) || line.contains(QLatin1String("the iPod left")))
        return journal::Level::Warning;
    return journal::Level::Info;
}

} // namespace

void openFolderInFileManager(const QString &folder)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), open_(openFolderInFileManager)
{
    setup();
    // The organization and the app are both called TacetOS, so AppDataLocation would give
    // TacetOS/TacetOS: the generic folder and one TacetOS give the single folder wanted.
    journal_->attachFile(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                         QStringLiteral("/TacetOS"));
    journal_->add(journal::Level::Info, journal::Domain::App, QString(),
                  QStringLiteral("start: version %1, %2")
                      .arg(QStringLiteral(TACETOS_VERSION), QSysInfo::prettyProductName()));

#ifdef Q_OS_LINUX
    ownClock_ = std::make_unique<SystemClock>();
    ownSource_ = std::make_unique<LinuxDeviceSource>(models_);
#ifdef TACETOS_HAVE_DBUS
    ownActions_ = std::make_unique<LinuxDiskActions>();
#else
    ownActions_ = std::make_unique<NoDiskActions>();
#endif
    ownExecutor_ = std::make_unique<ThreadExecutor>();
    wire(ownSource_.get(), ownClock_.get(), ownActions_.get(), ownExecutor_.get());

    // Like tacet-watch: the mount table only says something next to udev. Without udev the
    // driver polls.
#ifdef TACETOS_HAVE_UDEV
    if (UdevMonitor::attach(driver_.get()))
        MountWatcher::attach(driver_.get());
#endif
    driver_->start();
#else
    // Detection is not there yet on this system: a plain message, no error.
    home_->setDetectionAvailable(false);
#endif
}

MainWindow::MainWindow(DeviceSource *source, const Clock *clock, DiskActions *actions,
                       Executor *executor, OpenFolderFunction open, QWidget *parent)
    : QMainWindow(parent), open_(std::move(open))
{
    setup();
    wire(source, clock, actions, executor);
    driver_->start();
}

MainWindow::~MainWindow()
{
    if (driver_)
        driver_->stop();
}

void MainWindow::setup()
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("TacetOS"));
    setMinimumSize(800, 560);
    resize(960, 640);

    journal_ = std::make_unique<journal::Journal>();

    const ModelList list = loadModels(kModelsResource);
    if (list.error.isEmpty()) {
        models_ = list.models;
    } else {
        journal_->add(journal::Level::Error, journal::Domain::App, QString(),
                      QStringLiteral("the model list cannot be read: %1").arg(list.error));
    }

    home_ = new HomeScreen(this);
    setCentralWidget(home_);
}

void MainWindow::wire(DeviceSource *source, const Clock *clock, DiskActions *actions,
                      Executor *executor)
{
    tracker_ = std::make_unique<LiveTracker>(models_, clock, source);
    runner_ = std::make_unique<OperationRunner>(
        clock, actions, executor, [tracker = tracker_.get()](const QString &port, bool held) {
            tracker->setHeld(port, held);
        });
    model_ = std::make_unique<DeviceStateModel>(tracker_.get(), runner_.get(), clock);
    driver_ = std::make_unique<LiveDriver>(tracker_.get(), clock);
    driver_->addWakeSource(model_.get());

    connect(tracker_.get(), &LiveTracker::logged, this,
            [this](const QString &port, const QStringList &lines) {
                for (const QString &line : lines)
                    journal_->add(journal::Level::Info, domainOfDetectionLine(port, line), port,
                                  line);
            });
    connect(runner_.get(), &OperationRunner::logged, this,
            [this](const QString &port, const QStringList &lines) {
                for (const QString &line : lines)
                    journal_->add(levelOfOperationLine(line), journal::Domain::Op, port, line);
            });

    // stateChanged is always followed by listChanged, so the screen is set once per change.
    connect(model_.get(), &DeviceStateModel::listChanged, this, &MainWindow::showEntries);
    connect(runner_.get(), &OperationRunner::operationChanged, this, &MainWindow::onOperation);
    connect(home_, &HomeScreen::actionRequested, this, &MainWindow::onAction);
    connect(home_, &HomeScreen::refreshRequested, this, [this] { model_->refresh(); });
    showEntries();
}

void MainWindow::showEntries()
{
    home_->setEntries(model_->entries(), models_);
}

void MainWindow::onAction(const QString &port, HomeAction action)
{
    switch (action) {
    case HomeAction::Open: {
        const auto entry = model_->entry(port);
        if (entry && entry->ipod && entry->ipod->volume &&
            !entry->ipod->volume->mountPoint.isEmpty())
            open_(entry->ipod->volume->mountPoint);
        break;
    }
    case HomeAction::Mount:
        startAndOpen(OperationKind::Mount, port);
        break;
    case HomeAction::MountReadOnly:
        startAndOpen(OperationKind::MountRo, port);
        break;
    case HomeAction::Eject:
        model_->startOperation(OperationKind::Eject, port);
        break;
    }
}

void MainWindow::startAndOpen(OperationKind kind, const QString &port)
{
    const OperationRequest answer = model_->startOperation(kind, port);
    if (answer.accepted)
        openAfter_.insert(port, answer.id);
}

// Section 6: when the mount ends in `done` the mount point is opened; when it fails, nothing is.
void MainWindow::onOperation(const Operation &operation)
{
    if (operation.active() || openAfter_.value(operation.portPath) != operation.id)
        return;
    openAfter_.remove(operation.portPath);
    if (operation.status != OperationStatus::Done)
        return;
    QString folder = operation.result.mountPoint;
    if (folder.isEmpty()) {
        const auto entry = model_->entry(operation.portPath);
        if (entry && entry->ipod && entry->ipod->volume)
            folder = entry->ipod->volume->mountPoint;
    }
    if (!folder.isEmpty())
        open_(folder);
}

} // namespace tacetos
