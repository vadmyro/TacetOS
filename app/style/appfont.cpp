// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "style/appfont.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

namespace tacetos {

QFont fontFor(TextStyle style)
{
    QFont font = QGuiApplication::font();
    switch (style) {
    case TextStyle::Large:
        font.setPixelSize(22);
        font.setWeight(QFont::DemiBold);
        break;
    case TextStyle::Title:
        font.setPixelSize(17);
        font.setWeight(QFont::DemiBold);
        break;
    case TextStyle::Body:
        font.setPixelSize(14);
        font.setWeight(QFont::Normal);
        break;
    case TextStyle::Button:
        font.setPixelSize(14);
        font.setWeight(QFont::Medium);
        break;
    case TextStyle::Secondary:
        font.setPixelSize(13);
        font.setWeight(QFont::Normal);
        break;
    }
    return font;
}

bool loadAppFont(const QString &path)
{
    // Checked first: the resource and file paths both answer here, and the database stays quiet.
    if (!QFile::exists(path))
        return false;
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return false;
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
        return false;
    QFont font = QGuiApplication::font();
    font.setFamily(families.first());
    QGuiApplication::setFont(font);
    return true;
}

} // namespace tacetos
