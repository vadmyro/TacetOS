// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QJsonObject>
#include <QString>

// The disk named in a request (docs/specs/helper.md section 5.1): what is
// checked before anything is opened.

namespace tacetos::helper {

struct ResolvedTarget
{
    bool ok = false;
    // `bad-target` and the reason, when not ok.
    QString code;
    QString detail;
    bool isImage = false;
    // An image: its path with the links followed, and its sector size.
    QString path;
    int sectorSize = 0;
    // What the lock of section 8 is taken on.
    QString lockKey;
};

// The target object has passed `checkTargetShape`. An image must be a regular
// file, not under `/dev`, with a size that is a whole number of sectors and not
// zero. A real disk is refused for now: the helper reads image files only.
ResolvedTarget resolveTarget(const QJsonObject &target);

} // namespace tacetos::helper
