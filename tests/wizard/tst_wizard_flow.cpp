// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The lists of steps of each flow (docs/specs/install-wizard.md section 2.3).

#include "flow.h"

#include <QTest>

using namespace tacetos::wizard;

Q_DECLARE_METATYPE(tacetos::wizard::FlowShape)

class TstWizardFlow : public QObject
{
    Q_OBJECT

private slots:
    void lists_data();
    void lists();
    void refusedShapes();
    void names();
};

void TstWizardFlow::lists_data()
{
    QTest::addColumn<FlowShape>("shape");
    QTest::addColumn<QString>("steps");

    const auto P = Family::Partition;
    const auto D = Family::Dfu;
    const auto I = FlowKind::Install;
    const auto U = FlowKind::Uninstall;
    const auto N = FlowKind::Update;
    const auto none = InstallPath::None;
    QTest::newRow("older, clean") << FlowShape{P, I, InstallPath::Clean, false}
                                  << "diagnose backup-full disk-mode format firmware bootloader "
                                     "verify finish";
    QTest::newRow("older, clean, Mac")
        << FlowShape{P, I, InstallPath::Clean, true}
        << "diagnose backup-full disk-mode convert firmware bootloader verify finish";
    QTest::newRow("older, keep") << FlowShape{P, I, InstallPath::Keep, false}
                                 << "diagnose backup-keep disk-mode firmware bootloader verify "
                                    "finish";
    QTest::newRow("classic, clean")
        << FlowShape{D, I, InstallPath::Clean, false}
        << "diagnose backup-full format firmware classic-bootloader verify finish";
    QTest::newRow("classic, clean, Mac")
        << FlowShape{D, I, InstallPath::Clean, true}
        << "diagnose backup-full convert firmware classic-bootloader verify finish";
    QTest::newRow("classic, keep") << FlowShape{D, I, InstallPath::Keep, false}
                                   << "diagnose backup-keep firmware classic-bootloader verify "
                                      "finish";
    // Branding section 8.3: the bootloader goes in before the old folder moves.
    QTest::newRow("classic, keep, bootloader first")
        << FlowShape{D, I, InstallPath::Keep, false, true}
        << "diagnose backup-keep classic-bootloader firmware verify finish";
    // Only Keep my music on a Classic reads it: a format leaves no old folder.
    QTest::newRow("classic, clean, bootloader first")
        << FlowShape{D, I, InstallPath::Clean, false, true}
        << "diagnose backup-full format firmware classic-bootloader verify finish";
    QTest::newRow("older, keep, bootloader first")
        << FlowShape{P, I, InstallPath::Keep, false, true}
        << "diagnose backup-keep disk-mode firmware bootloader verify finish";
    QTest::newRow("classic, uninstall, bootloader first")
        << FlowShape{D, U, InstallPath::None, false, true}
        << "diagnose backup-settings classic-remove remove-firmware verify finish";
    QTest::newRow("older, uninstall")
        << FlowShape{P, U, InstallPath::None, false}
        << "diagnose backup-settings disk-mode remove-bootloader remove-firmware verify finish";
    QTest::newRow("older, uninstall, Mac")
        << FlowShape{P, U, InstallPath::None, true}
        << "diagnose backup-settings disk-mode remove-bootloader remove-firmware verify finish";
    QTest::newRow("classic, uninstall")
        << FlowShape{D, U, InstallPath::None, false}
        << "diagnose backup-settings classic-remove remove-firmware verify finish";
    // Update section 7: the firmware only (D61).
    QTest::newRow("older, update") << FlowShape{P, N, none, false}
                                   << "download diagnose backup-settings firmware verify finish";
    QTest::newRow("classic, update") << FlowShape{D, N, none, false}
                                     << "download diagnose backup-settings firmware verify finish";
    QTest::newRow("classic, update, bootloader first")
        << FlowShape{D, N, none, false, true}
        << "download diagnose backup-settings classic-bootloader firmware verify finish";
    QTest::newRow("older, update, bootloader first")
        << FlowShape{P, N, none, false, true}
        << "download diagnose backup-settings firmware verify finish";
}

void TstWizardFlow::lists()
{
    QFETCH(FlowShape, shape);
    QFETCH(QString, steps);
    QCOMPARE(stepsFor(shape).join(QLatin1Char(' ')), steps);
}

void TstWizardFlow::refusedShapes()
{
    // Section 2.2: an install needs a path, an uninstall has none, and a Mac
    // iPod only takes the clean install.
    QVERIFY(stepsFor({Family::Partition, FlowKind::Install, InstallPath::None, false}).isEmpty());
    QVERIFY(stepsFor({Family::Dfu, FlowKind::Uninstall, InstallPath::Clean, false}).isEmpty());
    QVERIFY(stepsFor({Family::Partition, FlowKind::Install, InstallPath::Keep, true}).isEmpty());
    QVERIFY(stepsFor({Family::Dfu, FlowKind::Install, InstallPath::Keep, true}).isEmpty());
    // An update has no path, and TacetOS is never on a Mac iPod.
    QVERIFY(stepsFor({Family::Dfu, FlowKind::Update, InstallPath::Keep, false}).isEmpty());
    QVERIFY(stepsFor({Family::Partition, FlowKind::Update, InstallPath::None, true}).isEmpty());
}

void TstWizardFlow::names()
{
    for (const Family family : {Family::Partition, Family::Dfu})
        QCOMPARE(familyFromName(familyName(family)), family);
    for (const FlowKind kind : {FlowKind::Install, FlowKind::Uninstall, FlowKind::Update})
        QCOMPARE(kindFromName(kindName(kind)), kind);
    for (const InstallPath path : {InstallPath::Clean, InstallPath::Keep})
        QCOMPARE(pathFromName(pathName(path)), path);
    QVERIFY(!familyFromName(QStringLiteral("Partition")));
    QVERIFY(!pathFromName(QString()));
}

QTEST_GUILESS_MAIN(TstWizardFlow)
#include "tst_wizard_flow.moc"
