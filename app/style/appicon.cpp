// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "style/appicon.h"

#include <QGuiApplication>
#include <QIcon>

namespace tacetos {

void applyAppIcon()
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/logo/icon.svg"));
    icon.addFile(QStringLiteral(":/logo/icon-512.png"), QSize(512, 512));
    QGuiApplication::setWindowIcon(icon);
}

} // namespace tacetos
