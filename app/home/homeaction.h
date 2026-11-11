// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QMetaType>
#include <QString>
#include <QStyle>
#include <QWidget>

namespace tacetos {

// What a button of a card asks for (docs/specs/home-screen.md section 6).
enum class HomeAction {
    // Open the mount point of a `ready` iPod. Not an operation.
    Open,
    // Open on `not-mounted`: starts the `mount` operation.
    Mount,
    // Open read only: starts `mount-ro`.
    MountReadOnly,
    Eject,
};

// Sets the "role" property that the style sheet looks at (style/palette.h) and
// applies the style again, so a role that changes while the widget is shown
// takes effect.
inline void setRole(QWidget *widget, const char *role)
{
    widget->setProperty("role", QString::fromLatin1(role));
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace tacetos

Q_DECLARE_METATYPE(tacetos::HomeAction)
