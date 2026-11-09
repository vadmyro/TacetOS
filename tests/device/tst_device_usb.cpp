// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The USB list readers and the pure classification of the device library
// (docs/specs/detection.md sections 3.1, 4 and 12.1).

#include "detected.h"
#include "models.h"
#include "usblist.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::device;

namespace {

QString linuxRoot(const char *scenario)
{
    return QStringLiteral(TACETOS_USB_FIXTURES "/usb-linux/") + QString::fromLatin1(scenario);
}

QStringList ports(const QList<UsbDevice> &devices)
{
    QStringList out;
    for (const UsbDevice &d : devices)
        out << d.portPath;
    return out;
}

} // namespace

class TestDeviceUsb : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void linuxNormal();
    void linuxDfu();
    void linuxNoIpod();
    void linuxTwoIpods();
    void linuxPhoneFromSameVendor();
    void linuxMini();
    void linuxSkipsInterfaceFolders();
    void linuxMissingRoot();

    void windowsHardwareId_data();
    void windowsHardwareId();
    void windowsList();

    void duplicatePortKeepsTheLast();
    void emptyPortPathIsNotADuplicate();
    void idIsNormalised();
    void detectedIpodStartsEmpty();

private:
    QList<Model> m_models;
};

void TestDeviceUsb::initTestCase()
{
    const ModelList list = loadModels(QStringLiteral(TACETOS_MODELS_TOML));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    m_models = list.models;
}

void TestDeviceUsb::linuxNormal()
{
    const UsbList list = readLinuxUsbList(linuxRoot("normal"));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    // The root hub, another vendor's device and the iPod, sorted by folder name.
    QCOMPARE(list.entries.size(), 3);
    QCOMPARE(list.entries[0].portPath, QStringLiteral("1-1"));
    QCOMPARE(list.entries[0].usbId, QStringLiteral("8087:0033"));
    QVERIFY(list.entries[0].serial.isEmpty());
    QCOMPARE(list.entries[1].portPath, QStringLiteral("1-2"));
    QCOMPARE(list.entries[1].usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(list.entries[1].serial, QStringLiteral("000A2700F1E2D3C4"));
    QCOMPARE(list.entries[2].portPath, QStringLiteral("usb1"));

    const UsbClassification c = classifyUsb(list.entries, m_models);
    QCOMPARE(c.otherVendors, 2);
    QVERIFY(c.log.isEmpty());
    QCOMPARE(c.devices.size(), 1);
    QCOMPARE(c.devices[0].usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(c.devices[0].mode, UsbMode::Normal);
    QCOMPARE(c.devices[0].candidates, QStringList{QStringLiteral("classic-6g")});
    QCOMPARE(c.devices[0].portPath, QStringLiteral("1-2"));
    QCOMPARE(c.devices[0].serial, QStringLiteral("000A2700F1E2D3C4"));
}

void TestDeviceUsb::linuxDfu()
{
    const UsbClassification c = classifyUsb(readLinuxUsbList(linuxRoot("dfu")).entries, m_models);
    QCOMPARE(c.devices.size(), 1);
    QCOMPARE(c.devices[0].usbId, QStringLiteral("05ac:1223"));
    QCOMPARE(c.devices[0].mode, UsbMode::Dfu);
    QCOMPARE(c.devices[0].candidates, QStringList{QStringLiteral("classic-6g")});
    QVERIFY(c.devices[0].serial.isEmpty());
}

void TestDeviceUsb::linuxNoIpod()
{
    const UsbList list = readLinuxUsbList(linuxRoot("none"));
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    // A folder with an id that is not hexadecimal, and a plain file, are skipped.
    QCOMPARE(list.entries.size(), 2);
    const UsbClassification c = classifyUsb(list.entries, m_models);
    QVERIFY(c.devices.isEmpty());
    QCOMPARE(c.otherVendors, 2);
}

void TestDeviceUsb::linuxTwoIpods()
{
    const UsbClassification c =
        classifyUsb(readLinuxUsbList(linuxRoot("two-ipods")).entries, m_models);
    QCOMPARE(ports(c.devices), (QStringList{QStringLiteral("1-2"), QStringLiteral("1-4.1")}));
    QCOMPARE(c.devices[0].usbId, c.devices[1].usbId);
    QCOMPARE(c.devices[0].candidates, c.devices[1].candidates);
    QVERIFY(c.devices[0].serial != c.devices[1].serial);
    QCOMPARE(c.otherVendors, 3);
}

void TestDeviceUsb::linuxPhoneFromSameVendor()
{
    const UsbClassification c = classifyUsb(readLinuxUsbList(linuxRoot("phone")).entries, m_models);
    // Both are Apple. The phone id is in no model: dropped with one log line.
    QCOMPARE(ports(c.devices), QStringList{QStringLiteral("1-2")});
    QCOMPARE(c.log.size(), 1);
    QVERIFY(c.log[0].contains(QLatin1String("05ac:12a8")));
    QVERIFY(c.log[0].contains(QLatin1String("1-3")));
}

void TestDeviceUsb::linuxMini()
{
    const UsbClassification c = classifyUsb(readLinuxUsbList(linuxRoot("mini")).entries, m_models);
    QCOMPARE(c.devices.size(), 1);
    QCOMPARE(c.devices[0].mode, UsbMode::Normal);
    QCOMPARE(c.devices[0].candidates.size(), 2);
}

void TestDeviceUsb::linuxSkipsInterfaceFolders()
{
    // Interface folders have a colon in their name, which some disks refuse in a
    // fixture, so this one is built here.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());
    QVERIFY(root.mkpath(QStringLiteral("1-2:1.0")));
    QFile iface(root.filePath(QStringLiteral("1-2:1.0/bInterfaceClass")));
    QVERIFY(iface.open(QIODevice::WriteOnly));
    iface.write("08\n");
    iface.close();
    QVERIFY(root.mkpath(QStringLiteral("1-2")));
    QFile vendor(root.filePath(QStringLiteral("1-2/idVendor")));
    QVERIFY(vendor.open(QIODevice::WriteOnly));
    vendor.write("05AC\n"); // upper case is read as lower case
    vendor.close();
    QFile product(root.filePath(QStringLiteral("1-2/idProduct")));
    QVERIFY(product.open(QIODevice::WriteOnly));
    product.write("1261");
    product.close();

    const UsbList list = readLinuxUsbList(tmp.path());
    QCOMPARE(list.entries.size(), 1);
    QCOMPARE(list.entries[0].usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(list.entries[0].portPath, QStringLiteral("1-2"));
}

void TestDeviceUsb::linuxMissingRoot()
{
    const UsbList list = readLinuxUsbList(QStringLiteral("/nonexistent/usb"));
    QVERIFY(list.entries.isEmpty());
    QVERIFY(list.error.startsWith(QLatin1String("cannot read")));
}

void TestDeviceUsb::windowsHardwareId_data()
{
    QTest::addColumn<QString>("hardwareId");
    QTest::addColumn<QString>("expected");

    QTest::newRow("with revision")
        << QStringLiteral("USB\\VID_05AC&PID_1261&REV_0001") << QStringLiteral("05ac:1261");
    QTest::newRow("without revision")
        << QStringLiteral("USB\\VID_05AC&PID_1261") << QStringLiteral("05ac:1261");
    QTest::newRow("old mixed case")
        << QStringLiteral("USB\\Vid_05ac&Pid_1223&Rev_0001") << QStringLiteral("05ac:1223");
    QTest::newRow("lower case") << QStringLiteral("usb\\vid_05ac&pid_120a")
                                << QStringLiteral("05ac:120a");
    QTest::newRow("spaces around")
        << QStringLiteral(" USB\\VID_05AC&PID_1205 ") << QStringLiteral("05ac:1205");
    QTest::newRow("interface id") << QStringLiteral("USB\\VID_05AC&PID_1261&MI_00") << QString();
    QTest::newRow("compatible id")
        << QStringLiteral("USB\\Class_08&SubClass_06&Prot_50") << QString();
    QTest::newRow("other bus") << QStringLiteral("PCI\\VEN_05AC&DEV_1261") << QString();
    QTest::newRow("short id") << QStringLiteral("USB\\VID_05A&PID_1261") << QString();
    QTest::newRow("not hex") << QStringLiteral("USB\\VID_05AG&PID_1261") << QString();
    QTest::newRow("empty") << QString() << QString();
}

void TestDeviceUsb::windowsHardwareId()
{
    QFETCH(QString, hardwareId);
    QFETCH(QString, expected);
    QCOMPARE(usbIdFromWindowsHardwareId(hardwareId), expected);
}

void TestDeviceUsb::windowsList()
{
    const QString hub = QStringLiteral("PCIROOT(0)#PCI(1400)#USBROOT(0)");
    const QList<WindowsDevice> devices = {
        {{QStringLiteral("USB\\ROOT_HUB30&VID8086&PID9D2F&REV0021"),
          QStringLiteral("USB\\ROOT_HUB30")},
         hub,
         {}},
        // Normal mode: the whole-device id, then the composite interface of the same port.
        {{QStringLiteral("USB\\VID_05AC&PID_1261&REV_0001"),
          QStringLiteral("USB\\VID_05AC&PID_1261")},
         hub + QStringLiteral("#USB(2)"),
         QStringLiteral("000A2700F1E2D3C4")},
        {{QStringLiteral("USB\\VID_05AC&PID_1261&MI_00")},
         hub + QStringLiteral("#USB(2)#USB(0)"),
         {}},
        // DFU, written the old way.
        {{QStringLiteral("USB\\Vid_05ac&Pid_1223")}, hub + QStringLiteral("#USB(3)"), {}},
        {{QStringLiteral("USB\\VID_046D&PID_C52B")}, hub + QStringLiteral("#USB(4)"), {}},
    };
    const QList<UsbEntry> entries = windowsUsbEntries(devices);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].usbId, QStringLiteral("05ac:1261"));
    QCOMPARE(entries[0].portPath, hub + QStringLiteral("#USB(2)"));
    QCOMPARE(entries[0].serial, QStringLiteral("000A2700F1E2D3C4"));

    const UsbClassification c = classifyUsb(entries, m_models);
    QCOMPARE(c.otherVendors, 1);
    QCOMPARE(c.devices.size(), 2);
    QCOMPARE(c.devices[0].mode, UsbMode::Normal);
    QCOMPARE(c.devices[1].usbId, QStringLiteral("05ac:1223"));
    QCOMPARE(c.devices[1].mode, UsbMode::Dfu);
}

void TestDeviceUsb::duplicatePortKeepsTheLast()
{
    // Section 4 rule 3: during a reset the same port shows twice.
    const QList<UsbEntry> list = {
        {QStringLiteral("05ac:1261"), QStringLiteral("1-2"), QStringLiteral("AAAA")},
        {QStringLiteral("05ac:1205"), QStringLiteral("1-3"), {}},
        {QStringLiteral("05ac:1223"), QStringLiteral("1-2"), {}},
    };
    const UsbClassification c = classifyUsb(list, m_models);
    QCOMPARE(ports(c.devices), (QStringList{QStringLiteral("1-3"), QStringLiteral("1-2")}));
    QCOMPARE(c.devices[1].usbId, QStringLiteral("05ac:1223"));
    QCOMPARE(c.devices[1].mode, UsbMode::Dfu);
    QVERIFY(c.devices[1].serial.isEmpty());
    QCOMPARE(c.log.size(), 1);
    QVERIFY(c.log[0].contains(QLatin1String("1-2")));
    QVERIFY(c.log[0].contains(QLatin1String("05ac:1261")));
}

void TestDeviceUsb::emptyPortPathIsNotADuplicate()
{
    const QList<UsbEntry> list = {
        {QStringLiteral("05ac:1261"), {}, {}},
        {QStringLiteral("05ac:1261"), {}, {}},
    };
    QCOMPARE(classifyUsb(list, m_models).devices.size(), 2);
}

void TestDeviceUsb::idIsNormalised()
{
    const QList<UsbEntry> list = {{QStringLiteral(" 05AC:1261 "), QStringLiteral("1-2"), {}}};
    const UsbClassification c = classifyUsb(list, m_models);
    QCOMPARE(c.devices.size(), 1);
    QCOMPARE(c.devices[0].usbId, QStringLiteral("05ac:1261"));
}

void TestDeviceUsb::detectedIpodStartsEmpty()
{
    const DetectedIpod ipod;
    QCOMPARE(ipod.mode, UsbMode::None);
    QCOMPARE(ipod.modelSource, ModelSource::None);
    QVERIFY(!ipod.disk.has_value());
    QVERIFY(!ipod.volume.has_value());
    QCOMPARE(ipod.state, IpodState::Waiting);
    QCOMPARE(ipod.firmware.files, FirmwareFiles::Absent);
}

QTEST_GUILESS_MAIN(TestDeviceUsb)
#include "tst_device_usb.moc"
