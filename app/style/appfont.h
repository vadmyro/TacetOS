// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QFont>
#include <QString>

namespace tacetos {

// The text styles of docs/specs/home-screen.md section 8.
enum class TextStyle {
    Large,     // empty state, 22 px, 600
    Title,     // name of the iPod, "TacetOS" in the header, 17 px, 600
    Body,      // state, banner, 14 px, 400
    Button,    // buttons, 14 px, 500
    Secondary, // second lines, 13 px, 400
};

// The font of a style: the font of the app, with the size in pixels and the weight of the style.
QFont fontFor(TextStyle style);

// Adds the font file to the app and makes its family the font of the app. Returns false, and
// changes nothing, when the file is missing or unreadable: the app then keeps the font of the
// system.
bool loadAppFont(const QString &path);

} // namespace tacetos
