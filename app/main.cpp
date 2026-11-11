// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mainwindow.h"
#include "style/appfont.h"
#include "style/appicon.h"
#include "style/stylecontroller.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TacetOS"));
    QApplication::setApplicationVersion(QStringLiteral(TACETOS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("TacetOS"));
    // Wayland shells find the icon of the window through this name and the .desktop file.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.vadmyro.TacetOS"));
    tacetos::applyAppIcon();

#ifdef TACETOS_HAS_FONT
    tacetos::loadAppFont(QStringLiteral(":/fonts/AdwaitaSans-Regular.ttf"));
#endif
    tacetos::StyleController style;

    tacetos::MainWindow window;
    window.show();

    return app.exec();
}
