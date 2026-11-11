// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/homescreen.h"
#include "mainwindow.h"
#include "style/appicon.h"

#include <QTest>

using namespace tacetos;
using namespace tacetos::device;

namespace {

// A world with no iPod, so the window never reads the real /sys or calls the disk service.
class EmptyWorld : public DeviceSource
{
public:
    std::optional<QList<UsbDevice>> readUsb() override { return QList<UsbDevice>(); }
    PortReading readPort(const UsbDevice &) override { return PortReading(); }
};

class FixedClock : public Clock
{
public:
    qint64 nowMs() const override { return 0; }
};

class NoActions : public DiskActions
{
public:
    DiskResult run(OperationKind, const QString &) override { return DiskResult(); }
};

class NoExecutor : public Executor
{
public:
    bool submit(Job) override { return false; }
};

struct Parts
{
    EmptyWorld world;
    FixedClock clock;
    NoActions actions;
    NoExecutor executor;

    MainWindow *makeWindow() { return new MainWindow(&world, &clock, &actions, &executor, {}); }
};

} // namespace

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void hasTitleAndMinimumSize();
    void opens();
    void hasAppIcon();
};

void TestMainWindow::hasTitleAndMinimumSize()
{
    Parts parts;
    std::unique_ptr<MainWindow> window(parts.makeWindow());
    QCOMPARE(window->windowTitle(), QStringLiteral("TacetOS"));
    QVERIFY(window->minimumWidth() >= 800);
    QVERIFY(window->minimumHeight() >= 560);
}

void TestMainWindow::opens()
{
    Parts parts;
    std::unique_ptr<MainWindow> window(parts.makeWindow());
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    QVERIFY(window->isVisible());
    QCOMPARE(window->centralWidget(), static_cast<QWidget *>(window->homeScreen()));
}

void TestMainWindow::hasAppIcon()
{
    tacetos::applyAppIcon();
    QVERIFY(!qApp->windowIcon().isNull());
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
