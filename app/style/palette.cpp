// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "style/palette.h"

namespace tacetos {

Palette Palette::light()
{
    Palette p;
    p.window = QColor(QStringLiteral("#F6F5F2"));
    p.card = QColor(QStringLiteral("#FFFFFF"));
    p.text = QColor(QStringLiteral("#1C1F24"));
    p.textSecondary = QColor(QStringLiteral("#5C6068"));
    p.border = QColor(QStringLiteral("#E3E1DC"));
    p.accent = QColor(QStringLiteral("#FF5A4E"));
    p.danger = QColor(QStringLiteral("#C2362B"));
    p.button = QColor(QStringLiteral("#1C1F24"));
    p.buttonText = QColor(QStringLiteral("#F6F5F2"));
    p.buttonHover = QColor(QStringLiteral("#33373E"));
    return p;
}

Palette Palette::dark()
{
    Palette p;
    p.window = QColor(QStringLiteral("#1C1F24"));
    p.card = QColor(QStringLiteral("#262A31"));
    p.text = QColor(QStringLiteral("#F6F5F2"));
    p.textSecondary = QColor(QStringLiteral("#A9ADB4"));
    p.border = QColor(QStringLiteral("#353A42"));
    p.accent = QColor(QStringLiteral("#FF5A4E"));
    p.danger = QColor(QStringLiteral("#FF7A70"));
    p.button = QColor(QStringLiteral("#F6F5F2"));
    p.buttonText = QColor(QStringLiteral("#1C1F24"));
    p.buttonHover = QColor(QStringLiteral("#E3E1DC"));
    return p;
}

QPalette Palette::toQPalette() const
{
    QPalette pal;
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, card);
    pal.setColor(QPalette::AlternateBase, window);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::PlaceholderText, textSecondary);
    pal.setColor(QPalette::Button, card);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::ToolTipBase, card);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, textSecondary);
    pal.setColor(QPalette::Disabled, QPalette::Text, textSecondary);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, textSecondary);
    return pal;
}

QString styleSheet(const Palette &p)
{
    const QString css = QStringLiteral(R"(
QMainWindow, QScrollArea {
    background: %1;
    border: none;
}
QLabel {
    color: %3;
    background: transparent;
}
QLabel[role="secondary"] {
    color: %4;
}
QLabel[role="danger"] {
    color: %7;
}
QFrame[role="header"] {
    background: %1;
    border: none;
    border-bottom: 1px solid %5;
}
QFrame[role="card"] {
    background: %2;
    border: 1px solid %5;
    border-radius: 12px;
}
QFrame[role="banner-done"], QFrame[role="banner-failed"] {
    background: %1;
    border: none;
    border-left: 3px solid %6;
}
QFrame[role="banner-failed"] {
    border-left-color: %7;
}
QFrame[role="banner-done"], QFrame[role="banner-failed"] {
    border-top-left-radius: 11px;
    border-top-right-radius: 11px;
}
QFrame[role="dot-accent"], QFrame[role="dot-secondary"], QFrame[role="dot-danger"] {
    border: none;
    border-radius: 4px;
}
QFrame[role="dot-accent"] {
    background: %6;
}
QFrame[role="dot-secondary"] {
    background: %4;
}
QFrame[role="dot-danger"] {
    background: %7;
}
QPushButton {
    background: transparent;
    color: %3;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 6px 16px;
}
QPushButton:hover {
    background: %1;
}
QPushButton[role="primary"] {
    background: %8;
    color: %9;
    border: 1px solid %8;
}
QPushButton[role="primary"]:hover {
    background: %10;
    border-color: %10;
}
QPushButton:disabled, QPushButton[role="primary"]:disabled {
    background: transparent;
    color: %4;
    border: 1px solid %5;
}
QPushButton:focus {
    border: 2px solid %6;
    padding: 5px 15px;
}
QProgressBar#spaceBar, QProgressBar#busyBar {
    background: %5;
    border: none;
    border-radius: 3px;
    max-height: 6px;
    text-align: center;
}
QProgressBar#spaceBar::chunk, QProgressBar#busyBar::chunk {
    background: %6;
    border-radius: 3px;
}
)");
    return css.arg(p.window.name(),        // %1
                   p.card.name(),          // %2
                   p.text.name(),          // %3
                   p.textSecondary.name(), // %4
                   p.border.name(),        // %5
                   p.accent.name(),        // %6
                   p.danger.name(),        // %7
                   p.button.name(),        // %8
                   p.buttonText.name(),    // %9
                   p.buttonHover.name());  // %10
}

} // namespace tacetos
