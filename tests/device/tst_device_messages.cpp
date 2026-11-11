// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The messages of the displayed states (docs/specs/device-state.md section 1.4):
// one case per row of the two tables.

#include "messages.h"

#include <QTest>

using namespace tacetos::device;

Q_DECLARE_METATYPE(tacetos::device::ScreenKind)
Q_DECLARE_METATYPE(tacetos::device::OperationKind)
Q_DECLARE_METATYPE(tacetos::device::DiskError)
Q_DECLARE_METATYPE(tacetos::device::HostSystem)

namespace {

ScreenState stateOf(ScreenKind kind, OperationKind operation = OperationKind::Mount,
                    DiskError error = DiskError::None)
{
    ScreenState state;
    state.kind = kind;
    state.operation = operation;
    state.error = error;
    return state;
}

} // namespace

class TestMessages : public QObject
{
    Q_OBJECT

private slots:
    void table_data();
    void table();
    void failed_data();
    void failed();
    void notMountedOnOtherSystems();
    void otherStatesUseDetection();
};

void TestMessages::table_data()
{
    QTest::addColumn<ScreenKind>("kind");
    QTest::addColumn<OperationKind>("operation");
    QTest::addColumn<QString>("message");

    const auto row = [](const char *name, ScreenKind kind, OperationKind op, const char *text) {
        QTest::newRow(name) << kind << op << QString::fromLatin1(text);
    };
    row("unplugged", ScreenKind::Unplugged, OperationKind::Mount, "Connect your iPod.");
    row("rebooting", ScreenKind::Rebooting, OperationKind::Mount,
        "The iPod is restarting. Wait...");
    row("busy-mount", ScreenKind::Busy, OperationKind::Mount, "Opening the iPod...");
    row("busy-mount-ro", ScreenKind::Busy, OperationKind::MountRo,
        "Opening the iPod, for reading only...");
    row("busy-unmount", ScreenKind::Busy, OperationKind::Unmount, "Closing the iPod...");
    row("busy-eject", ScreenKind::Busy, OperationKind::Eject,
        "Getting the iPod ready to unplug...");
    row("done-mount", ScreenKind::Done, OperationKind::Mount, "The iPod is open.");
    row("done-mount-ro", ScreenKind::Done, OperationKind::MountRo, "The iPod is open.");
    row("done-unmount", ScreenKind::Done, OperationKind::Unmount, "The iPod is closed.");
    row("released", ScreenKind::Released, OperationKind::Eject, "You can unplug the iPod now.");
    row("not-mounted-linux", ScreenKind::NotMounted, OperationKind::Mount,
        "The iPod storage is there but is not open on this computer. TacetOS can open it.");
}

void TestMessages::table()
{
    QFETCH(ScreenKind, kind);
    QFETCH(OperationKind, operation);
    QFETCH(QString, message);
    QCOMPARE(screenMessage(stateOf(kind, operation), std::nullopt, {}, HostSystem::Linux), message);
}

void TestMessages::failed_data()
{
    QTest::addColumn<DiskError>("error");
    QTest::addColumn<QString>("message");

    QTest::newRow("not-authorized")
        << DiskError::NotAuthorized
        << QStringLiteral("The system did not allow it. Try again and accept the permission "
                          "request.");
    QTest::newRow("busy") << DiskError::Busy
                          << QStringLiteral(
                                 "Another program is using the iPod. Close it, then try again.");
    QTest::newRow("no-volume") << DiskError::NoVolume
                               << QStringLiteral("The iPod has no volume that can be mounted.");
    QTest::newRow("service-missing")
        << DiskError::ServiceMissing
        << QStringLiteral("The disk service is not running. Install it, then try again.");
    QTest::newRow("not-an-ipod-disk")
        << DiskError::NotAnIpodDisk
        << QStringLiteral("This disk is not an iPod that the system lets us handle.");
    QTest::newRow("anything-else-failed")
        << DiskError::Failed << QStringLiteral("The system could not do it.");
    QTest::newRow("anything-else-none")
        << DiskError::None << QStringLiteral("The system could not do it.");
}

void TestMessages::failed()
{
    QFETCH(DiskError, error);
    QFETCH(QString, message);
    const ScreenState state = stateOf(ScreenKind::Failed, OperationKind::Mount, error);
    QCOMPARE(screenMessage(state, std::nullopt, {}, HostSystem::Linux), message);
}

void TestMessages::notMountedOnOtherSystems()
{
    // The app does not mount there: the text of detection is kept.
    DetectedIpod ipod;
    ipod.state = IpodState::NoVolume;
    const ScreenState state = stateOf(ScreenKind::NotMounted);
    for (const HostSystem system : {HostSystem::Windows, HostSystem::MacOs}) {
        const QString message = screenMessage(state, ipod, {}, system);
        QCOMPARE(message, stateMessage(ipod, {}, system));
        QVERIFY(!message.contains(QStringLiteral("TacetOS can open it")));
    }
}

void TestMessages::otherStatesUseDetection()
{
    DetectedIpod ipod;
    ipod.state = IpodState::Waiting;
    QCOMPARE(screenMessage(stateOf(ScreenKind::Reading), ipod, {}, HostSystem::Linux),
             QStringLiteral("Reading the iPod..."));

    ipod.state = IpodState::Dfu;
    QCOMPARE(screenMessage(stateOf(ScreenKind::Dfu), ipod, {}, HostSystem::Linux),
             QStringLiteral("The iPod is in DFU mode."));

    // Attention carries the detection state that explains it.
    ScreenState attention = stateOf(ScreenKind::Attention);
    attention.reason = IpodState::UnsupportedModel;
    ipod.state = IpodState::UnsupportedModel;
    QCOMPARE(screenMessage(attention, ipod, {}, HostSystem::Linux),
             QStringLiteral("This iPod is not supported."));

    ipod.state = IpodState::Ready;
    ipod.model = QStringLiteral("classic-6g");
    Model classic;
    classic.id = QStringLiteral("classic-6g");
    classic.name = QStringLiteral("iPod Classic");
    const QList<Model> models = {classic};
    QCOMPARE(screenMessage(stateOf(ScreenKind::Ready), ipod, models, HostSystem::Linux),
             stateMessage(ipod, models, HostSystem::Linux));
}

QTEST_GUILESS_MAIN(TestMessages)
#include "tst_device_messages.moc"
