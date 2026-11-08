// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mainwindow.h"

namespace tacetos {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("TacetOS"));
    setMinimumSize(800, 560);
    resize(960, 640);
}

} // namespace tacetos
