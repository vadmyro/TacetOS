// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QObject>

namespace tacetos {

// Applies the light or the dark look to the whole app, and follows the colour scheme of the
// system while the app runs (docs/specs/home-screen.md section 7.1).
class StyleController : public QObject
{
    Q_OBJECT

public:
    // Needs a QGuiApplication. Applies the scheme of the system at once.
    explicit StyleController(QObject *parent = nullptr);

public slots:
    // Unknown gives the light look.
    void apply(Qt::ColorScheme scheme);
};

} // namespace tacetos
