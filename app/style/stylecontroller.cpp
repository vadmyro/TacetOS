// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "style/stylecontroller.h"

#include "style/palette.h"

#include <QApplication>
#include <QStyleHints>

namespace tacetos {

StyleController::StyleController(QObject *parent) : QObject(parent)
{
    QStyleHints *hints = QGuiApplication::styleHints();
    connect(hints, &QStyleHints::colorSchemeChanged, this, &StyleController::apply);
    apply(hints->colorScheme());
}

void StyleController::apply(Qt::ColorScheme scheme)
{
    const Palette palette = scheme == Qt::ColorScheme::Dark ? Palette::dark() : Palette::light();
    QGuiApplication::setPalette(palette.toQPalette());
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        app->setStyleSheet(styleSheet(palette));
}

} // namespace tacetos
