// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "helpercalls.h"
#include "wizard/wizardwindow.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>

namespace tacetos {

// What the app does about the wizard outside a window (docs/specs/install-screens.md sections
// 1.1 and 2.2, install-wizard.md section 3.3): one window per iPod, the progress files that
// are waiting, and the programs a crash left paused.
class WizardManager : public QObject
{
    Q_OBJECT

public:
    // Sends one command to the helper, for `resume-paused`. Empty: nothing is sent.
    using HelperCall =
        std::function<wizard::HelperAnswer(const QString &cmd, const QJsonObject &args)>;

    WizardManager(const QString &dataDir, HelperCall call, QObject *parent = nullptr);

    bool hasProgress(const QString &serial) const;

    // Helper section 6.10: sends `resume-paused` when its state file exists, and nothing
    // otherwise. To be called at the start of the app, before any other command.
    bool resumePausedAtStart();

    // Section 2.2: for each of these serials that has a progress file, says so with
    // `resumeProposed`. Returns the serials.
    QStringList proposeResume(const QStringList &serials);

    // The window of a serial, if one is open.
    WizardWindow *windowFor(const QString &serial) const;
    // Registers a window: opening again for the same serial brings it to the front instead.
    // Returns false when one is open already.
    bool adopt(WizardWindow *window);
    // Brings the window of the serial to the front.
    bool show(const QString &serial);

    // Screens section 1.3: why the app cannot quit now, empty when it can.
    QString quitRefusal() const;

signals:
    void resumeProposed(const QString &serial);

private:
    QString m_dataDir;
    HelperCall m_call;
    QHash<QString, QPointer<WizardWindow>> m_windows;
};

} // namespace tacetos
