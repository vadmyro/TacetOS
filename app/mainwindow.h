// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "home/homeaction.h"
#include "journal.h"
#include "live.h"
#include "livedriver.h"
#include "models.h"
#include "operations.h"
#include "screenstate.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <functional>
#include <memory>

namespace tacetos {

class HomeScreen;

// Opens a folder in the file manager of the system. The window is given one so that a test
// never opens anything.
using OpenFolderFunction = std::function<void(const QString &folder)>;

// The real one: QDesktopServices::openUrl on the folder.
void openFolderInFileManager(const QString &folder);

// The main window: the home screen in the middle, wired to the device code
// (docs/specs/home-screen.md section 6).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // The real app: the readers of this system, the disk service, the journal file in the data
    // folder. On a system without detection the screen says so and nothing runs.
    explicit MainWindow(QWidget *parent = nullptr);

    // For the tests: what the window reads from, the clock, what is done to the disks, where it
    // runs and how a folder is opened are given, and none of them is owned. No journal file
    // and no udev or mount watcher: the test calls the tracker and the driver itself.
    MainWindow(device::DeviceSource *source, const device::Clock *clock,
               device::DiskActions *actions, device::Executor *executor, OpenFolderFunction open,
               QWidget *parent = nullptr);

    ~MainWindow() override;

    HomeScreen *homeScreen() const { return home_; }
    device::LiveTracker *tracker() const { return tracker_.get(); }
    device::LiveDriver *driver() const { return driver_.get(); }
    device::DeviceStateModel *stateModel() const { return model_.get(); }
    journal::Journal *journal() const { return journal_.get(); }

private:
    // Builds the screen and the journal, then the device part when there is a source.
    void setup();
    void wire(device::DeviceSource *source, const device::Clock *clock,
              device::DiskActions *actions, device::Executor *executor);
    void onAction(const QString &port, HomeAction action);
    void onOperation(const device::Operation &operation);
    void showEntries();
    void startAndOpen(device::OperationKind kind, const QString &port);

    HomeScreen *home_ = nullptr;
    OpenFolderFunction open_;
    QList<device::Model> models_;
    // What the real app made for itself (empty when a test gave its own), then the parts in the
    // order they need each other. They are destroyed in the opposite order.
    std::unique_ptr<device::SystemClock> ownClock_;
    std::unique_ptr<device::DeviceSource> ownSource_;
    std::unique_ptr<device::DiskActions> ownActions_;
    std::unique_ptr<device::Executor> ownExecutor_;
    std::unique_ptr<journal::Journal> journal_;
    std::unique_ptr<device::LiveTracker> tracker_;
    std::unique_ptr<device::OperationRunner> runner_;
    std::unique_ptr<device::DeviceStateModel> model_;
    std::unique_ptr<device::LiveDriver> driver_;
    // Mount and mount read only open the mount point when they end well: the id of the
    // operation that asked, by port.
    QHash<QString, quint64> openAfter_;
};

} // namespace tacetos
