// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TacetOS"));
    QApplication::setApplicationVersion(QStringLiteral(TACETOS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("TacetOS"));

    tacetos::MainWindow window;
    window.show();

    return app.exec();
}
