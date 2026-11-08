// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mainwindow.h"

#include <QTest>

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void hasTitleAndMinimumSize();
    void opens();
};

void TestMainWindow::hasTitleAndMinimumSize()
{
    tacetos::MainWindow window;
    QCOMPARE(window.windowTitle(), QStringLiteral("TacetOS"));
    QVERIFY(window.minimumWidth() >= 800);
    QVERIFY(window.minimumHeight() >= 560);
}

void TestMainWindow::opens()
{
    tacetos::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.isVisible());
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
