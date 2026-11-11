// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

namespace tacetos {

// The colours of the app, one field per role of docs/specs/home-screen.md section 7.
// They are the only colours of the app: no colour is written anywhere else in the code.
struct Palette
{
    QColor window;
    QColor card;
    QColor text;
    QColor textSecondary;
    QColor border;
    QColor accent; // never for text
    QColor danger;
    QColor button;
    QColor buttonText;
    QColor buttonHover;

    static Palette light();
    static Palette dark();

    QPalette toQPalette() const;
};

// The style sheet of the app, built from the roles of the palette only.
// Widgets pick their look with a dynamic property "role":
//   QFrame      card, header, banner-done, banner-failed, dot-accent, dot-secondary, dot-danger
//   QPushButton primary, secondary (the default look of a button is the secondary one)
//   QLabel      secondary, danger
// and with the object names spaceBar and busyBar for the two progress bars.
QString styleSheet(const Palette &palette);

} // namespace tacetos
