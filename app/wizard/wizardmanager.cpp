// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/wizardmanager.h"

#include "helperconstants.h"
#include "progress.h"

#include <QDir>
#include <QFile>

namespace tacetos {

WizardManager::WizardManager(const QString &dataDir, HelperCall call, QObject *parent)
    : QObject(parent), m_dataDir(dataDir), m_call(std::move(call))
{
}

bool WizardManager::hasProgress(const QString &serial) const
{
    return wizard::ProgressStore(m_dataDir).exists(serial);
}

bool WizardManager::resumePausedAtStart()
{
    if (!m_call || !QFile::exists(QDir(m_dataDir).filePath(QString(helper::kPausedStateFile))))
        return false;
    QJsonObject args;
    args.insert(QStringLiteral("dataDir"), m_dataDir);
    return m_call(QString(helper::cmd::resumePaused), args).ok();
}

QStringList WizardManager::proposeResume(const QStringList &serials)
{
    QStringList found;
    for (const QString &serial : serials) {
        if (!serial.isEmpty() && hasProgress(serial) && !windowFor(serial)) {
            found.append(serial);
            emit resumeProposed(serial);
        }
    }
    return found;
}

WizardWindow *WizardManager::windowFor(const QString &serial) const
{
    return m_windows.value(serial).data();
}

bool WizardManager::adopt(WizardWindow *window)
{
    const QString serial = window->target().serial;
    if (windowFor(serial))
        return false;
    m_windows.insert(serial, window);
    // A widget sends `destroyed` before its QPointer is cleared: the window is forgotten here,
    // before the slots connected after this one (the card) ask for it.
    connect(window, &QObject::destroyed, this, [this, serial] { m_windows.remove(serial); });
    return true;
}

bool WizardManager::show(const QString &serial)
{
    WizardWindow *window = windowFor(serial);
    if (!window)
        return false;
    window->show();
    window->raise();
    window->activateWindow();
    return true;
}

QString WizardManager::quitRefusal() const
{
    for (const QPointer<WizardWindow> &window : m_windows) {
        if (window && !window->quitRefusal().isEmpty())
            return window->quitRefusal();
    }
    return QString();
}

} // namespace tacetos
