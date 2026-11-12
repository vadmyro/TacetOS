// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The DFU exchanges of the iPod Classic (docs/specs/bootloader-classic.md
// sections 4 to 7 and 8.5) against a fake iPod played from a script. The
// expected results are the list of requests sent and the final message.

#include "constants.h"
#include "dfuprotocol.h"
#include "fakedfudevice.h"

#include <QTest>

using namespace tacetos::installer;

namespace {

constexpr int kMaxImage = 0x20000;

const char *const kInterrupted = "The transfer was interrupted. The iPod is not changed.";
const char *const kNotReady = "The iPod is not ready. Reset it and enter DFU mode again.";
const char *const kRefused = "The iPod refused the install file.";

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, '\0');
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 13 + (i >> 8) * 5 + seed) & 0xFF);
    return b;
}

UsbControlRequest req(quint8 type, quint8 request, quint16 value, quint16 length)
{
    UsbControlRequest r;
    r.type = type;
    r.request = request;
    r.value = value;
    r.length = length;
    return r;
}

UsbControlRequest getState()
{
    return req(0xA1, 5, 0, 1);
}
UsbControlRequest getStatus()
{
    return req(0xA1, 3, 0, 6);
}
UsbControlRequest clrStatus()
{
    return req(0x21, 4, 0, 0);
}
UsbControlRequest abortRequest()
{
    return req(0x21, 6, 0, 0);
}
UsbControlRequest dnload(int block, int length)
{
    return req(0x21, 1, quint16(block), quint16(length));
}

// The requests of a clean transfer of `dataBytes` bytes (the image and its 4
// checksum bytes), with `busyAnswers` more GETSTATUS requests after the block
// `busyBlock`, and the final GETSTATUS of step 6.
QList<UsbControlRequest> cleanTransfer(int dataBytes, int busyBlock = -1, int busyAnswers = 0)
{
    QList<UsbControlRequest> list;
    list << getState();
    int block = 0;
    for (int at = 0; at < dataBytes; at += 2048, ++block) {
        list << dnload(block, qMin(2048, dataBytes - at)) << getStatus();
        if (block == busyBlock)
            for (int i = 0; i < busyAnswers; ++i)
                list << getStatus();
    }
    list << dnload(block, 0) << getStatus() << getStatus();
    return list;
}

// A second CRC-32, bit by bit, written here and sharing nothing with the one under test.
quint32 slowCrc32(const QByteArray &data)
{
    // Reflected form: process the bits low to high, with the polynomial reversed.
    quint32 reg = 0xFFFFFFFFu;
    for (char byte : data) {
        reg ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (reg & 1)
                reg = (reg >> 1) ^ 0xEDB88320u;
            else
                reg >>= 1;
        }
    }
    return reg ^ 0xFFFFFFFFu;
}

QByteArray le32(quint32 v)
{
    QByteArray b;
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xFF));
    return b;
}

struct Run
{
    DfuTransferResult result;
    QList<int> sleeps;
};

Run run(FakeDfuDevice &device, const QByteArray &image)
{
    Run r;
    DfuSendOptions options;
    options.sleep = [&r](int ms) { r.sleeps << ms; };
    r.result = sendDfuImage(device, image, options);
    return r;
}

QString requestsText(const QList<UsbControlRequest> &list)
{
    QStringList parts;
    for (const UsbControlRequest &r : list)
        parts << QStringLiteral("%1/%2/%3/%4")
                     .arg(r.type, 0, 16)
                     .arg(r.request)
                     .arg(r.value)
                     .arg(r.length);
    return parts.join(QLatin1Char(' '));
}

#define COMPARE_REQUESTS(actual, expected) QCOMPARE(requestsText(actual), requestsText(expected))

} // namespace

class TestDfuProtocol : public QObject
{
    Q_OBJECT

private slots:
    void normalTransfer128K();
    void exactMultipleOfBlock_data();
    void exactMultipleOfBlock();
    void busyAnswers_data();
    void busyAnswers();
    void pollTimeoutIsClamped_data();
    void pollTimeoutIsClamped();
    void errorStateAtStart();
    void errorStateStays();
    void notIdleAtStart();
    void errorDuringBlocks();
    void errFirmwareAfterEmptyBlock();
    void stateEightSendsOneReset();
    void errVerifyAtStepSix();
    void stuckInManifest();
    void timeoutThenAbort();
    void imageSizeLimits();
    void openFails();
    void wrongDeviceIsRefused_data();
    void wrongDeviceIsRefused();
    void chooseDevice();
    void checksumFixedValues();
    void checksumAgainstSecondImplementation();
    void checksumIsAddedAtSend();
};

void TestDfuProtocol::normalTransfer128K()
{
    const QByteArray image = pattern(kMaxImage, 1);
    FakeDfuDevice device;
    const Run r = run(device, image);

    QVERIFY2(r.result.ok, qPrintable(r.result.message));
    QCOMPARE(r.result.failure, DfuFailure::None);
    QCOMPARE(r.result.message,
             QStringLiteral("The install file was sent. The iPod is writing it and will restart."));
    // 64 full blocks, one short block of the 4 checksum bytes, the empty block.
    const QList<UsbControlRequest> expected = cleanTransfer(kMaxImage + 4);
    COMPARE_REQUESTS(r.result.requests, expected);
    QCOMPARE(r.result.requests.size(), 1 + 65 * 2 + 3);
    QCOMPARE(r.result.requests.at(1 + 64 * 2), dnload(64, 4));
    QCOMPARE(r.result.requests.at(1 + 65 * 2), dnload(65, 0));
    QCOMPARE(device.received(), withDfuChecksum(image));
    QVERIFY2(device.protocolErrors().isEmpty(),
             qPrintable(device.protocolErrors().join(QLatin1Char(';'))));
    QCOMPARE(device.opens(), 1);
    QCOMPARE(device.closes(), 1);
    QCOMPARE(r.result.resets, 0);
    QCOMPARE(device.resets(), 0);
    // Only the wait of step 6, with the poll timeout the device gave.
    QCOMPARE(r.sleeps, QList<int>{5});
    for (int t : device.timeouts())
        QCOMPARE(t, 500);
}

void TestDfuProtocol::exactMultipleOfBlock_data()
{
    QTest::addColumn<int>("imageSize");
    QTest::addColumn<int>("dataBytes");
    // The data (image and checksum) is an exact multiple: the last data block is full.
    QTest::newRow("data is 3 blocks") << 3 * 2048 - 4 << 3 * 2048;
    // The image itself is a multiple: the last block holds only the checksum.
    QTest::newRow("image is 3 blocks") << 3 * 2048 << 3 * 2048 + 4;
    QTest::newRow("one byte") << 1 << 5;
}

void TestDfuProtocol::exactMultipleOfBlock()
{
    QFETCH(int, imageSize);
    QFETCH(int, dataBytes);
    FakeDfuDevice device;
    const QByteArray image = pattern(imageSize, 9);
    const Run r = run(device, image);

    QVERIFY2(r.result.ok, qPrintable(r.result.message));
    COMPARE_REQUESTS(r.result.requests, cleanTransfer(dataBytes));
    QCOMPARE(device.received(), withDfuChecksum(image));
    QVERIFY(device.protocolErrors().isEmpty());
    // The empty block never carries data, even after a full block.
    QCOMPARE(r.result.requests.at(r.result.requests.size() - 3).length, quint16(0));
}

void TestDfuProtocol::busyAnswers_data()
{
    QTest::addColumn<int>("busy");
    QTest::addColumn<bool>("works");
    QTest::newRow("1") << 1 << true;
    QTest::newRow("3") << 3 << true;
    // 20 requests for one block, the first one included (section 5, step 4.3).
    QTest::newRow("19") << 19 << true;
    QTest::newRow("20") << 20 << false;
    QTest::newRow("25") << 25 << false;
}

void TestDfuProtocol::busyAnswers()
{
    QFETCH(int, busy);
    QFETCH(bool, works);
    FakeDfuScript script;
    script.busyBlock = 2;
    script.busyAnswers = busy;
    script.pollMs = 7;
    FakeDfuDevice device(script);
    const QByteArray image = pattern(10000, 3);
    const Run r = run(device, image);

    if (works) {
        QVERIFY2(r.result.ok, qPrintable(r.result.message));
        COMPARE_REQUESTS(r.result.requests, cleanTransfer(10004, 2, busy));
        QList<int> sleeps(busy, 7);
        sleeps << 7;
        QCOMPARE(r.sleeps, sleeps);
        return;
    }
    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Interrupted);
    QCOMPARE(r.result.message, QString::fromLatin1(kInterrupted));
    // GETSTATE, blocks 0 and 1, block 2 with 20 GETSTATUS in all, then the recovery.
    QList<UsbControlRequest> expected;
    expected << getState() << dnload(0, 2048) << getStatus() << dnload(1, 2048) << getStatus()
             << dnload(2, 2048);
    for (int i = 0; i < 20; ++i)
        expected << getStatus();
    expected << abortRequest() << getState();
    COMPARE_REQUESTS(r.result.requests, expected);
    QCOMPARE(r.sleeps.size(), 19);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::pollTimeoutIsClamped_data()
{
    QTest::addColumn<int>("pollMs");
    QTest::addColumn<int>("waited");
    QTest::newRow("zero is one") << 0 << 1;
    QTest::newRow("300 uses both bytes") << 300 << 300;
    QTest::newRow("5000") << 5000 << 5000;
    QTest::newRow("too long") << 0xFFFFFF << 5000;
}

void TestDfuProtocol::pollTimeoutIsClamped()
{
    QFETCH(int, pollMs);
    QFETCH(int, waited);
    FakeDfuScript script;
    script.pollMs = pollMs;
    script.busyAnswers = 1;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(100, 4));
    QVERIFY2(r.result.ok, qPrintable(r.result.message));
    // The busy answer of block 0, and the wait of step 6.
    QCOMPARE(r.sleeps, (QList<int>{waited, waited}));
}

void TestDfuProtocol::errorStateAtStart()
{
    FakeDfuScript script;
    script.startState = 10;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 5));

    QVERIFY2(r.result.ok, qPrintable(r.result.message));
    QList<UsbControlRequest> expected;
    expected << getState() << clrStatus();
    expected << cleanTransfer(3004);
    // cleanTransfer starts with its own GETSTATE: the one after CLRSTATUS.
    COMPARE_REQUESTS(r.result.requests, expected);
}

void TestDfuProtocol::errorStateStays()
{
    FakeDfuScript script;
    script.startState = 10;
    script.clearWorks = false;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 5));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::NotReady);
    QCOMPARE(r.result.message, QString::fromLatin1(kNotReady));
    // One CLRSTATUS only, nothing sent after, not even ABORT: the iPod is untouched.
    COMPARE_REQUESTS(r.result.requests,
                     (QList<UsbControlRequest>{getState(), clrStatus(), getState()}));
    QCOMPARE(device.received().size(), 0);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::notIdleAtStart()
{
    FakeDfuScript script;
    script.startState = 5;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 5));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::NotReady);
    QCOMPARE(r.result.message, QString::fromLatin1(kNotReady));
    COMPARE_REQUESTS(r.result.requests, (QList<UsbControlRequest>{getState()}));
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::errorDuringBlocks()
{
    FakeDfuScript script;
    script.errorAtBlock = 1;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(10000, 6));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Interrupted);
    QCOMPARE(r.result.message, QString::fromLatin1(kInterrupted));
    QVERIFY(r.result.detail.contains(QLatin1String("errWRITE")));
    // The device is in dfuERROR: ABORT, GETSTATE, then CLRSTATUS (section 7).
    QList<UsbControlRequest> expected;
    expected << getState() << dnload(0, 2048) << getStatus() << dnload(1, 2048) << getStatus()
             << abortRequest() << getState() << clrStatus();
    COMPARE_REQUESTS(r.result.requests, expected);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::errFirmwareAfterEmptyBlock()
{
    FakeDfuScript script;
    script.endStatus = 10;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 7));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Refused);
    QCOMPARE(r.result.message, QString::fromLatin1(kRefused));
    QCOMPARE(r.result.detail, QStringLiteral("errFIRMWARE"));
    QList<UsbControlRequest> expected;
    expected << getState() << dnload(0, 2048) << getStatus() << dnload(1, 956) << getStatus()
             << dnload(2, 0) << getStatus() << abortRequest() << getState() << clrStatus();
    COMPARE_REQUESTS(r.result.requests, expected);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::stateEightSendsOneReset()
{
    FakeDfuScript script;
    script.end = FakeDfuEnd::WaitReset;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 8));

    QVERIFY2(r.result.ok, qPrintable(r.result.message));
    COMPARE_REQUESTS(r.result.requests, cleanTransfer(3004));
    QCOMPARE(r.result.resets, 1);
    QCOMPARE(device.resets(), 1);
    QCOMPARE(device.closes(), 1);
    QVERIFY(device.protocolErrors().isEmpty());
}

void TestDfuProtocol::errVerifyAtStepSix()
{
    FakeDfuScript script;
    script.end = FakeDfuEnd::Verify;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 8));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Refused);
    QCOMPARE(r.result.message, QString::fromLatin1(kRefused));
    QCOMPARE(r.result.detail, QStringLiteral("errVERIFY, state 10"));
    QCOMPARE(r.result.resets, 0);
    QCOMPARE(device.resets(), 0);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::stuckInManifest()
{
    FakeDfuScript script;
    script.end = FakeDfuEnd::Stuck;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 8));

    // "Anything else" at step 6: the ROM did not start the payload.
    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Refused);
    QCOMPARE(device.resets(), 0);
    QCOMPARE(device.closes(), 1);
}

void TestDfuProtocol::timeoutThenAbort()
{
    FakeDfuScript script;
    script.timeoutAtBlock = 10;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(kMaxImage, 2));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Interrupted);
    QCOMPARE(r.result.message, QString::fromLatin1(kInterrupted));
    QCOMPARE(r.result.detail, QStringLiteral("timeout"));
    QList<UsbControlRequest> expected;
    expected << getState();
    for (int block = 0; block < 10; ++block)
        expected << dnload(block, 2048) << getStatus();
    expected << dnload(10, 2048) << abortRequest() << getState();
    COMPARE_REQUESTS(r.result.requests, expected);
    QCOMPARE(device.closes(), 1);
    QCOMPARE(device.opens(), 1);
}

void TestDfuProtocol::imageSizeLimits()
{
    FakeDfuDevice device;
    DfuTransferResult r = sendDfuImage(device, QByteArray());
    QCOMPARE(r.failure, DfuFailure::BadImage);
    r = sendDfuImage(device, QByteArray(kMaxImage + 1, 'x'));
    QCOMPARE(r.failure, DfuFailure::BadImage);
    QVERIFY(r.requests.isEmpty());
    QCOMPARE(device.opens(), 0);
}

void TestDfuProtocol::openFails()
{
    FakeDfuScript script;
    script.openFails = true;
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(100, 1));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::Open);
    QCOMPARE(r.result.detail, QStringLiteral("access denied"));
    QVERIFY(r.result.requests.isEmpty());
}

void TestDfuProtocol::wrongDeviceIsRefused_data()
{
    QTest::addColumn<int>("product");
    QTest::addColumn<int>("vendor");
    // The iPod in normal mode, given to the DFU code by mistake.
    QTest::newRow("05ac:1261") << 0x1261 << 0x05AC;
    QTest::newRow("05ac:1241 WTF") << 0x1241 << 0x05AC;
    QTest::newRow("1223 of another vendor") << 0x1223 << 0x1234;
}

void TestDfuProtocol::wrongDeviceIsRefused()
{
    QFETCH(int, product);
    QFETCH(int, vendor);
    FakeDfuScript script;
    script.id.product = quint16(product);
    script.id.vendor = quint16(vendor);
    FakeDfuDevice device(script);
    const Run r = run(device, pattern(3000, 1));

    QVERIFY(!r.result.ok);
    QCOMPARE(r.result.failure, DfuFailure::WrongDevice);
    // Not opened, not asked anything.
    QVERIFY(r.result.requests.isEmpty());
    QCOMPARE(device.opens(), 0);
    QCOMPARE(device.received().size(), 0);
}

void TestDfuProtocol::chooseDevice()
{
    const UsbDeviceId dfu{0x05AC, 0x1223};
    const UsbDeviceId normal{0x05AC, 0x1261};
    const UsbDeviceId other{0x046D, 0xC52B};

    DfuScan scan = chooseDfuDevice({});
    QCOMPARE(scan.status, DfuScanStatus::None);

    // Only an iPod in normal mode, or another vendor's 1223: keep waiting.
    scan = chooseDfuDevice({normal, other, UsbDeviceId{0x1223, 0x05AC}});
    QCOMPARE(scan.status, DfuScanStatus::None);

    scan = chooseDfuDevice({other, normal, dfu});
    QCOMPARE(scan.status, DfuScanStatus::Found);
    QCOMPARE(scan.index, 2);

    scan = chooseDfuDevice({dfu, other, dfu});
    QCOMPARE(scan.status, DfuScanStatus::Several);
    QCOMPARE(scan.index, -1);
    QCOMPARE(scan.message,
             QStringLiteral("More than one iPod is in DFU mode. Plug only one iPod."));

    for (quint16 wtf : {0x1241, 0x1245, 0x1247, 0x1250}) {
        scan = chooseDfuDevice({other, UsbDeviceId{0x05AC, wtf}});
        QCOMPARE(scan.status, DfuScanStatus::Wtf);
        QVERIFY(scan.message.contains(QLatin1String("wrong mode")));
        QVERIFY(scan.message.contains(QLatin1String("Menu and Select")));
    }
}

void TestDfuProtocol::checksumFixedValues()
{
    // The usual CRC-32 of "123456789" is 0xCBF43926 and of four zero bytes
    // 0x2144DF1C; the 4 bytes sent are their bitwise NOT, little-endian.
    QCOMPARE(crc32(QByteArray("123456789")), 0xCBF43926u);
    QCOMPARE(dfuChecksumBytes(QByteArray("123456789")), QByteArray("\xD9\xC6\x0B\x34", 4));
    QCOMPARE(dfuChecksumBytes(QByteArray(4, '\0')), QByteArray("\xE3\x20\xBB\xDE", 4));
    QCOMPARE(dfuChecksumBytes(QByteArray()), QByteArray("\xFF\xFF\xFF\xFF", 4));
}

void TestDfuProtocol::checksumAgainstSecondImplementation()
{
    const int sizes[] = {1, 2, 3, 15, 16, 17, 255, 256, 2047, 2048, 2049, 5400, 0x30C, kMaxImage};
    int seed = 0;
    for (int size : sizes) {
        const QByteArray image = pattern(size, ++seed);
        QCOMPARE(crc32(image), slowCrc32(image));
        QCOMPARE(dfuChecksumBytes(image), le32(~slowCrc32(image)));
        QCOMPARE(withDfuChecksum(image), image + le32(~slowCrc32(image)));
        QCOMPARE(withDfuChecksum(image).size(), size + 4);
    }
    // All 256 byte values, and a long run of 0xFF.
    QByteArray all;
    for (int i = 0; i < 256; ++i)
        all.append(char(i));
    QCOMPARE(crc32(all), slowCrc32(all));
    QCOMPARE(crc32(QByteArray(100000, '\xFF')), slowCrc32(QByteArray(100000, '\xFF')));
}

void TestDfuProtocol::checksumIsAddedAtSend()
{
    // The image given to the sender has no checksum; the device receives it
    // after the image, in the last data block, and not as a block of its own.
    const QByteArray image = pattern(5000, 12);
    FakeDfuDevice device;
    const Run r = run(device, image);
    QVERIFY(r.result.ok);
    QCOMPARE(device.received().left(5000), image);
    QCOMPARE(device.received().mid(5000), le32(~slowCrc32(image)));
}

QTEST_GUILESS_MAIN(TestDfuProtocol)
#include "tst_installer_dfuprotocol.moc"
