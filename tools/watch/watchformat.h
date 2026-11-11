// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "detected.h"
#include "models.h"

#include <QHash>
#include <QList>
#include <QString>

namespace tacetos::watch {

// What tacet-watch shows after the port for an iPod: model, USB id, mode,
// state, firmware line, disk node and mount point. Nothing else of the iPod is
// shown, so two iPods that differ only elsewhere give the same text.
QString describe(const device::DetectedIpod &ipod, const QList<device::Model> &models);

// Remembers the last text printed for each port. A `changed` event is only
// worth a line when the text is not the one already on screen: the tracker
// also reports changes that tacet-watch does not show (free space, label,
// sizes), and a line with no visible difference is noise.
class ChangeFilter
{
public:
    // Call for `added` (and for the first sight of an iPod).
    void shown(const QString &portPath, const QString &text);
    // True when `text` differs from what was last shown for the port. The text
    // is remembered either way.
    bool isVisibleChange(const QString &portPath, const QString &text);
    void removed(const QString &portPath);

private:
    QHash<QString, QString> last_;
};

} // namespace tacetos::watch
