// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "home/homeaction.h"
#include "models.h"
#include "wizard/wizardmanager.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

namespace tacetos::device {
class LiveTracker;
}
namespace tacetos::helper {
class HelperClient;
}
namespace tacetos::journal {
class Journal;
}

namespace tacetos {

class WizardWindow;

// Opens the window of the install wizard for an iPod, wired to the real sources of the system
// (docs/specs/install-wizard.md section 9 and install-screens.md section 1): the helper program
// found next to the app, one runner and one `LinuxStepFactory` per window, and the signals of
// detection. The windows are kept by a `WizardManager`.
class WizardLauncher : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        // The data folder of the app.
        QString dataDir;
        // The helper program. Empty: the one next to the executable of the app.
        QString helperProgram;
        QString appVersion;
        QList<device::Model> models;
        // The folder of the backups. Empty: the default one.
        QString backupRoot;
    };

    // `tracker` and `journal` must outlive the launcher. `journal` may be null.
    WizardLauncher(const Config &config, device::LiveTracker &tracker, journal::Journal *journal,
                   QObject *parent = nullptr);
    ~WizardLauncher() override;

    WizardManager &manager() { return *manager_; }
    helper::HelperClient &client() { return *client_; }

    // Section 1.1: opens the window of the iPod of this port for an action of the home screen,
    // or brings the one that is open to the front. Returns null when the iPod is not known.
    WizardWindow *open(const QString &port, HomeAction action);

    // Screens section 11: the writes are on only in a build that allows them, with the helper
    // found.
    bool realWrites() const;
    bool helperFound() const;

signals:
    // The progress files or the windows changed: the home screen sets its buttons again.
    void stateChanged(const QString &port);

private:
    struct Session;

    void closeSession(const QString &serial);

    Config config_;
    device::LiveTracker &tracker_;
    journal::Journal *journal_;
    std::unique_ptr<helper::HelperClient> client_;
    std::unique_ptr<WizardManager> manager_;
    // By serial.
    QHash<QString, std::shared_ptr<Session>> sessions_;
};

} // namespace tacetos
