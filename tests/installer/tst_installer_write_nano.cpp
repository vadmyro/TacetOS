// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Install, update and uninstall for the 2nd generation nano
// (docs/specs/bootloader-legacy.md sections 15 and 17, tests of section 26.3 for
// this layout). The expected directory values are written again here from the
// spec; the expected image comes from the fake iPod library with the bootloader
// already present, and the two things it does not know (the hash block of the
// file and the checksum over it) are patched in.

#include "constants.h"
#include "fakepod.h"
#include "faultdevice.h"
#include "fixtures.h"
#include "operation.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

constexpr int kSector = 2048;
const QString kTarget = QStringLiteral("ipodnano2g");

QByteArray readAll(const QString &path, qint64 offset = 0, qint64 length = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return length < 0 ? f.readAll() : f.read(length);
}

QByteArray hashOf(const QString &path)
{
    return QCryptographicHash::hash(readAll(path), QCryptographicHash::Sha256);
}

void patch(const QString &path, qint64 offset, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(offset));
    QCOMPARE(f.write(bytes), qint64(bytes.size()));
}

QByteArray le32(quint32 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char((v >> (8 * i)) & 0xFF);
    return b;
}

qint64 roundUp(qint64 n)
{
    return (n + kSector - 1) / kSector * kSector;
}

quint32 byteSum(const QByteArray &data)
{
    quint32 sum = 0;
    for (char c : data)
        sum += quint8(c);
    return sum;
}

// Sectors (inside the partition) where two copies differ.
QList<int> differingSectors(const QByteArray &a, const QByteArray &b)
{
    QList<int> out;
    if (a.size() != b.size())
        return {-1};
    for (int s = 0; s * kSector < a.size(); ++s) {
        if (a.mid(s * kSector, kSector) != b.mid(s * kSector, kSector))
            out.append(s);
    }
    return out;
}

// A bootloader file for the nano: the program holds the hash block first.
QByteArray fileFor(int length, QByteArray *programOut = nullptr)
{
    const QByteArray program = fp::BootloaderFile::program(kTarget, length);
    const fp::BootTarget *t = fp::BootloaderFile::target(kTarget);
    if (programOut)
        *programOut = program;
    return fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(t->number, program),
                                        QByteArray(t->tag), program);
}

} // namespace

class TestWriteNano : public QObject
{
    Q_OBJECT

private slots:
    void installGivesTheInstalledImage_data();
    void installGivesTheInstalledImage();
    void installAddsTheEntryOfThePlace();
    void installIsOneDataWriteAndOneDirectoryWrite();
    void installOnADirectoryOneSectorLate();
    void installFillsTheLastSlotOfTheDirectory();
    void installOnAFullDirectoryIsRefused();
    void partitionTooSmallLeavesTheFileUnchanged();
    void paddingOfTheProgramIsZero();
    void updateWithALargerBootloader();
    void updateWithASmallerBootloader();
    void updateRemovesTheBootloaderFirst();
    void uninstallGivesTheCleanDirectory_data();
    void uninstallGivesTheCleanDirectory();
    void uninstallIsOneDirectoryWrite();
    void uninstallOnAFullDirectory();
    void wrongStateWritesNothing();
    void aWriteThatFailsStopsTheOperation();

private:
    struct Img
    {
        QString path;
        fp::FakePodResult pod;
    };
    Img make(bool installed, bool rsrcAupd = false, bool late = false,
             const QByteArray &payload = {})
    {
        Img i;
        i.path = m_dir.filePath(QStringLiteral("img%1.bin").arg(++m_count));
        fp::FakePodSpec spec;
        spec.modelId = QStringLiteral("nano-2g");
        spec.path = i.path;
        spec.bootloaderPresent = installed;
        spec.withRsrc = spec.withAupd = rsrcAupd;
        spec.directoryLate = late;
        spec.bootloader = payload;
        i.pod = fp::FakePod::build(spec);
        return i;
    }
    QByteArray firmwareOf(const Img &i) const
    {
        return readAll(i.path, i.pod.firmwareStart, i.pod.firmwareBytes);
    }
    OperationResult run(const Img &img, Operation op, const QByteArray &file,
                        FaultSectorDevice **watchedOut = nullptr, int failAt = 0)
    {
        m_file.reset(
            new FileSectorDevice(img.path, img.pod.sectorSize, FileSectorDevice::Mode::ReadWrite));
        if (!m_file->open())
            return {};
        m_watched.reset(new FaultSectorDevice(*m_file));
        if (failAt)
            m_watched->failAtWrite(failAt);
        if (watchedOut)
            *watchedOut = m_watched.get();
        OperationRequest req;
        req.op = op;
        req.scan = takeScanSnapshot(*m_watched, recognise(*m_watched));
        req.bootloaderFile = file;
        req.backupDir = m_dir.filePath(QStringLiteral("backup%1").arg(++m_count));
        req.allowRealWrites = false;
        return runOperation(*m_watched, req);
    }
    FirmwareState stateOf(const Img &img)
    {
        FileSectorDevice dev(img.path, img.pod.sectorSize);
        if (!dev.open())
            return {};
        return readFirmware(dev, recognise(dev));
    }
    // The directory sector of the image, from the start of the partition.
    qint64 directorySector(const Img &img) const { return img.pod.directoryOffset / kSector; }
    // Makes the expected installed image for `program` (hash block first): the
    // library builds it with its own hash block and its own checksum, which are
    // replaced here by the ones the spec gives.
    Img expectedInstalled(const QByteArray &program, bool rsrcAupd, bool late = false)
    {
        const Img e = make(true, rsrcAupd, late, program.mid(kHashBlockBytes));
        const FirmwareState s = stateOf(e);
        const DirectoryEntry &os = s.entryList.at(s.osIndex);
        patch(e.path, e.pod.firmwareStart + s.imageStart(os) - kHashBlockBytes,
              program.left(kHashBlockBytes));
        patch(e.path,
              e.pod.firmwareStart + e.pod.directoryOffset + s.osIndex * kDirectoryEntrySize +
                  kEntryChecksumOffset,
              le32(byteSum(program)));
        return e;
    }
    // A clean image whose directory holds `total` entries: the OSOS one, then
    // entries of an unknown tag, each in a sector of its own.
    Img withEntries(int total, bool late = false)
    {
        const Img img = make(false, false, late);
        const FirmwareState s = stateOf(img);
        const DirectoryEntry &os = s.entryList.at(0);
        QByteArray dir;
        QFile f(img.path);
        if (!f.open(QIODevice::ReadWrite))
            return {};
        const qint64 at = img.pod.firmwareStart + img.pod.directoryOffset;
        f.seek(at);
        const QByteArray first = f.read(kDirectoryEntrySize);
        dir = first;
        for (int i = 1; i < total; ++i) {
            QByteArray e = first;
            e.replace(kEntryTypeOffset, 4, "zzzz");
            const quint32 devOffset =
                os.devOffset + quint32(os.paddedBytes(kSector)) + quint32(kSector) * quint32(i - 1);
            e.replace(kEntryDevOffsetOffset, 4, le32(devOffset));
            e.replace(kEntryLengthOffset, 4, le32(0x10));
            dir += e;
        }
        f.seek(at);
        f.write(dir);
        return img;
    }

    QTemporaryDir m_dir;
    int m_count = 0;
    std::unique_ptr<FileSectorDevice> m_file;
    std::unique_ptr<FaultSectorDevice> m_watched;
};

void TestWriteNano::installGivesTheInstalledImage_data()
{
    QTest::addColumn<bool>("rsrcAupd");
    QTest::newRow("alone") << false;
    QTest::newRow("with following images") << true;
}

void TestWriteNano::installGivesTheInstalledImage()
{
    QFETCH(bool, rsrcAupd);
    QByteArray program;
    const QByteArray file = fileFor(0, &program);
    const Img before = make(false, rsrcAupd);
    const Img expected = expectedInstalled(program, rsrcAupd);
    QVERIFY(before.pod.ok && expected.pod.ok);
    const QByteArray original = firmwareOf(before);

    const OperationResult res = run(before, Operation::Install, file);
    QVERIFY2(res.ok, qPrintable(res.message));
    QVERIFY(res.wroteAnything);
    QCOMPARE(stateOf(before).state, State::Installed);
    QCOMPARE(firmwareOf(before), firmwareOf(expected));
    // Nothing moves: only the directory sector and the new data are different.
    const QList<int> diff = differingSectors(original, firmwareOf(before));
    const qint64 dataSector = (res.report.plan.dataStart) / kSector;
    for (int s : diff)
        QVERIFY2(s == directorySector(before) || s >= dataSector, "a sector moved or changed");
    // The music partition and the table are not touched.
    QCOMPARE(readAll(before.path, 0, before.pod.firmwareStart),
             readAll(expected.path, 0, expected.pod.firmwareStart));
    QCOMPARE(readAll(before.path, before.pod.musicStart, 4096),
             readAll(expected.path, expected.pod.musicStart, 4096));
}

void TestWriteNano::installAddsTheEntryOfThePlace()
{
    QByteArray program;
    const QByteArray file = fileFor(0, &program);
    const Img img = make(false, true);
    const FirmwareState clean = stateOf(img);
    QCOMPARE(clean.entryList.size(), 3);
    const DirectoryEntry apple = clean.entryList.at(0);
    const DirectoryEntry last = clean.entryList.last();

    const OperationResult res = run(img, Operation::Install, file);
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.entryList.size(), 4);
    // Apple's entry is the same, with the tag OSBK.
    const DirectoryEntry &osbk = now.entryList.at(0);
    QCOMPARE(osbk.kind, EntryKind::Osbk);
    QCOMPARE(osbk.devOffset, apple.devOffset);
    QCOMPARE(osbk.length, apple.length);
    QCOMPARE(osbk.checksum, apple.checksum);
    QCOMPARE(osbk.entryOffset, apple.entryOffset);
    QCOMPARE(osbk.version, apple.version);
    QCOMPARE(osbk.loadAddress, apple.loadAddress);
    QCOMPARE(osbk.flashAddress, apple.flashAddress);
    // The other entries are the same.
    for (int i = 1; i < 3; ++i) {
        QCOMPARE(now.entryList.at(i).kind, clean.entryList.at(i).kind);
        QCOMPARE(now.entryList.at(i).devOffset, clean.entryList.at(i).devOffset);
        QCOMPARE(now.entryList.at(i).length, clean.entryList.at(i).length);
    }
    // The new entry: after the last image, the hash block in front of it.
    const DirectoryEntry &boot = now.entryList.at(3);
    QCOMPARE(boot.kind, EntryKind::Osos);
    QCOMPARE(now.osIndex, 3);
    QCOMPARE(now.osbkIndex, 0);
    QCOMPARE(qint64(boot.devOffset),
             roundUp(qint64(last.devOffset) + last.length) + kHashBlockBytes);
    QCOMPARE(qint64(boot.length), program.size() - kHashBlockBytes);
    QCOMPARE(boot.checksum, byteSum(program));
    QCOMPARE(boot.entryOffset, apple.entryOffset);
    QCOMPARE(boot.loadAddress, apple.loadAddress);
    QCOMPARE(boot.version, apple.version);
    QCOMPARE(boot.imageNumber, apple.imageNumber);
    // The hash block and the program are on the disk, `kHashBlockBytes` before devOffset.
    const QByteArray onDisk = readAll(
        img.path, img.pod.firmwareStart + now.imageStart(boot) - kHashBlockBytes, program.size());
    QCOMPARE(onDisk, program);
}

void TestWriteNano::installIsOneDataWriteAndOneDirectoryWrite()
{
    const Img img = make(false, true);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry &last = clean.entryList.last();
    const qint64 dataStart =
        img.pod.firmwareStart + clean.imageArea + roundUp(qint64(last.devOffset) + last.length);
    FaultSectorDevice *w = nullptr;
    QByteArray program;
    const OperationResult res = run(img, Operation::Install, fileFor(0, &program), &w);
    QVERIFY2(res.ok, qPrintable(res.message));
    // Data first, then one directory sector holding both changes.
    QCOMPARE(w->writeLog().size(), 2);
    QCOMPARE(w->writeLog().at(0).first * kSector, dataStart);
    QCOMPARE(w->writeLog().at(0).count, roundUp(program.size()) / kSector);
    QCOMPARE(w->writeLog().at(1).first * kSector,
             img.pod.firmwareStart + directorySector(img) * kSector);
    QCOMPARE(w->writeLog().at(1).count, qint64(1));
    QVERIFY(w->flushCount() >= w->writeCount());
}

void TestWriteNano::installOnADirectoryOneSectorLate()
{
    QByteArray program;
    const QByteArray file = fileFor(0, &program);
    const Img img = make(false, true, true);
    const Img expected = expectedInstalled(program, true, true);
    QVERIFY(img.pod.directoryOffset >= kSector);
    const OperationResult res = run(img, Operation::Install, file);
    QVERIFY2(res.ok, qPrintable(res.message));
    QCOMPARE(stateOf(img).state, State::Installed);
    QCOMPARE(firmwareOf(img), firmwareOf(expected));
}

void TestWriteNano::installFillsTheLastSlotOfTheDirectory()
{
    // 9 entries: the new one is the 10th, which section 7.2 still reads.
    for (bool late : {false, true}) {
        const Img img = withEntries(9, late);
        QCOMPARE(stateOf(img).entryList.size(), 9);
        QByteArray program;
        const OperationResult res = run(img, Operation::Install, fileFor(0, &program));
        QVERIFY2(res.ok, qPrintable(res.message));
        const FirmwareState now = stateOf(img);
        QCOMPARE(now.state, State::Installed);
        QCOMPARE(now.entryList.size(), 10);
        QCOMPARE(now.osIndex, 9);
        QCOMPARE(now.osbkIndex, 0);
        for (int i = 1; i < 9; ++i)
            QCOMPARE(int(now.entryList.at(i).kind), int(EntryKind::Other));
    }
}

void TestWriteNano::installOnAFullDirectoryIsRefused()
{
    for (bool late : {false, true}) {
        const Img img = withEntries(10, late);
        QCOMPARE(stateOf(img).entryList.size(), 10);
        QCOMPARE(stateOf(img).state, State::Clean);
        const QByteArray before = hashOf(img.path);
        FaultSectorDevice *w = nullptr;
        const OperationResult res = run(img, Operation::Install, fileFor(0), &w);
        QVERIFY(!res.ok);
        QVERIFY(!res.wroteAnything);
        QCOMPARE(res.report.failingCheck, 2);
        QCOMPARE(res.message,
                 QStringLiteral("check 2: the directory has no room for one more entry"));
        QCOMPARE(w->writeCount(), 0);
        QCOMPARE(hashOf(img.path), before);
    }
}

void TestWriteNano::partitionTooSmallLeavesTheFileUnchanged()
{
    const Img img = make(false, true);
    const QByteArray before = hashOf(img.path);
    FaultSectorDevice *w = nullptr;
    // A program as large as the whole partition: the end of the data falls outside.
    const OperationResult res =
        run(img, Operation::Install, fileFor(int(img.pod.firmwareBytes)), &w);
    QVERIFY(!res.ok);
    QVERIFY(!res.wroteAnything);
    QCOMPARE(res.report.failingCheck, 5);
    QCOMPARE(res.message, QStringLiteral("check 5: not enough room in the firmware partition"));
    QCOMPARE(w->writeCount(), 0);
    QCOMPARE(hashOf(img.path), before);
}

void TestWriteNano::paddingOfTheProgramIsZero()
{
    // The place of the new image holds leftovers: the padding must not keep them.
    const Img img = make(false, false);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry &last = clean.entryList.last();
    const qint64 area =
        img.pod.firmwareStart + clean.imageArea + roundUp(qint64(last.devOffset) + last.length);
    patch(img.path, area, QByteArray(6 * kSector, '\xAA'));
    QByteArray program;
    const OperationResult res = run(img, Operation::Install, fileFor(3000, &program));
    QVERIFY2(res.ok, qPrintable(res.message));
    const qint64 padded = roundUp(program.size());
    QVERIFY(padded > program.size());
    QCOMPARE(readAll(img.path, area, program.size()), program);
    QCOMPARE(readAll(img.path, area + program.size(), padded - program.size()),
             QByteArray(int(padded - program.size()), '\0'));
}

void TestWriteNano::updateWithALargerBootloader()
{
    QByteArray first, second;
    const Img img = make(false, true);
    QVERIFY2(run(img, Operation::Install, fileFor(3000, &first)).ok, "install");
    const FirmwareState installed = stateOf(img);
    const QByteArray apple = readAll(img.path, img.pod.firmwareStart, installed.imageArea);

    const OperationResult res = run(img, Operation::Update, fileFor(9000, &second));
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Installed);
    QCOMPARE(now.entryList.size(), installed.entryList.size());
    // The place is the one of the current OSOS entry, and Apple's entry is back to OSBK.
    QCOMPARE(now.osIndex, installed.osIndex);
    QCOMPARE(now.osbkIndex, 0);
    const DirectoryEntry &boot = now.entryList.at(now.osIndex);
    QCOMPARE(boot.devOffset, installed.entryList.at(installed.osIndex).devOffset);
    QCOMPARE(qint64(boot.length), second.size() - kHashBlockBytes);
    QCOMPARE(boot.checksum, byteSum(second));
    QCOMPARE(readAll(img.path, img.pod.firmwareStart + now.imageStart(boot) - kHashBlockBytes,
                     second.size()),
             second);
    const qint64 end = roundUp(second.size());
    QCOMPARE(readAll(img.path,
                     img.pod.firmwareStart + now.imageStart(boot) - kHashBlockBytes + second.size(),
                     end - second.size()),
             QByteArray(int(end - second.size()), '\0'));
    // Apple's own entry has not moved.
    QCOMPARE(now.entryList.at(0).devOffset, installed.entryList.at(0).devOffset);
    QCOMPARE(now.entryList.at(0).checksum, installed.entryList.at(0).checksum);
}

void TestWriteNano::updateWithASmallerBootloader()
{
    QByteArray first, second;
    const Img img = make(false, false);
    QVERIFY2(run(img, Operation::Install, fileFor(9000, &first)).ok, "install");
    const FirmwareState installed = stateOf(img);
    const OperationResult res = run(img, Operation::Update, fileFor(2500, &second));
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Installed);
    const DirectoryEntry &boot = now.entryList.at(now.osIndex);
    QCOMPARE(boot.devOffset, installed.entryList.at(installed.osIndex).devOffset);
    QCOMPARE(qint64(boot.length), second.size() - kHashBlockBytes);
    QCOMPARE(boot.checksum, byteSum(second));
    const qint64 at = img.pod.firmwareStart + now.imageStart(boot) - kHashBlockBytes;
    QCOMPARE(readAll(img.path, at, second.size()), second);
    QCOMPARE(readAll(img.path, at + second.size(), roundUp(second.size()) - second.size()),
             QByteArray(int(roundUp(second.size()) - second.size()), '\0'));
}

void TestWriteNano::updateRemovesTheBootloaderFirst()
{
    QByteArray program;
    const Img img = make(true, true);
    const FirmwareState installed = stateOf(img);
    FaultSectorDevice *w = nullptr;
    const OperationResult res = run(img, Operation::Update, fileFor(0, &program), &w);
    QVERIFY2(res.ok, qPrintable(res.message));
    // The directory as after an uninstall, the data, the directory with both changes.
    QCOMPARE(w->writeLog().size(), 3);
    const qint64 dirSector = img.pod.firmwareStart / kSector + directorySector(img);
    QCOMPARE(w->writeLog().at(0).first, dirSector);
    QCOMPARE(w->writeLog().at(1).first * kSector,
             img.pod.firmwareStart + installed.imageStart(installed.entryList.last()) -
                 kHashBlockBytes);
    QCOMPARE(w->writeLog().at(2).first, dirSector);
    QCOMPARE(w->writeLog().at(0).count, qint64(1));
    QCOMPARE(w->writeLog().at(2).count, qint64(1));

    // Cut after the first write: the iPod is clean, Apple's entry is OSOS again.
    const Img cut = make(true, true);
    const OperationResult failed = run(cut, Operation::Update, fileFor(0), nullptr, 2);
    QVERIFY(!failed.ok);
    QCOMPARE(int(failed.failedStep), int(WriteStep::WriteProgram));
    const FirmwareState s = stateOf(cut);
    QCOMPARE(s.state, State::Clean);
    QCOMPARE(s.entryList.at(0).kind, EntryKind::Osos);
    QCOMPARE(s.osIndex, 0);
}

void TestWriteNano::uninstallGivesTheCleanDirectory_data()
{
    QTest::addColumn<bool>("rsrcAupd");
    QTest::addColumn<bool>("late");
    QTest::newRow("alone") << false << false;
    QTest::newRow("with following images") << true << false;
    QTest::newRow("late directory") << true << true;
}

void TestWriteNano::uninstallGivesTheCleanDirectory()
{
    QFETCH(bool, rsrcAupd);
    QFETCH(bool, late);
    const Img img = make(true, rsrcAupd, late);
    const Img clean = make(false, rsrcAupd, late);
    const QByteArray original = firmwareOf(img);
    const OperationResult res = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(res.ok, qPrintable(res.message));
    QVERIFY(res.wroteAnything);
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Clean);
    const FirmwareState want = stateOf(clean);
    QCOMPARE(now.entryList.size(), want.entryList.size());
    for (int i = 0; i < now.entryList.size(); ++i) {
        QCOMPARE(int(now.entryList.at(i).kind), int(want.entryList.at(i).kind));
        QCOMPARE(now.entryList.at(i).devOffset, want.entryList.at(i).devOffset);
        QCOMPARE(now.entryList.at(i).length, want.entryList.at(i).length);
        QCOMPARE(now.entryList.at(i).checksum, want.entryList.at(i).checksum);
    }
    // The 40 bytes of the removed entry are zero, and the directory sector is the
    // only sector that changed: the bootloader stays after the last image.
    const QByteArray after = firmwareOf(img);
    QCOMPARE(differingSectors(original, after), QList<int>{int(directorySector(img))});
    const qint64 removed = img.pod.directoryOffset + 40 * (now.entryList.size());
    QCOMPARE(after.mid(int(removed), 40), QByteArray(40, '\0'));
    QCOMPARE(readAll(img.path, 0, img.pod.firmwareStart),
             readAll(clean.path, 0, clean.pod.firmwareStart));
}

void TestWriteNano::uninstallIsOneDirectoryWrite()
{
    const Img img = make(true, true);
    FaultSectorDevice *w = nullptr;
    const OperationResult res = run(img, Operation::Uninstall, QByteArray(), &w);
    QVERIFY2(res.ok, qPrintable(res.message));
    QCOMPARE(w->writeLog().size(), 1);
    QCOMPARE(w->writeLog().at(0).first * kSector,
             img.pod.firmwareStart + directorySector(img) * kSector);
    QCOMPARE(w->writeLog().at(0).count, qint64(1));
    QVERIFY(w->flushCount() >= 1);
}

void TestWriteNano::uninstallOnAFullDirectory()
{
    // An install that filled the 10th slot, then the uninstall that frees it.
    const Img img = withEntries(9);
    QVERIFY2(run(img, Operation::Install, fileFor(0)).ok, "install");
    QCOMPARE(stateOf(img).entryList.size(), 10);
    const OperationResult res = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Clean);
    QCOMPARE(now.entryList.size(), 9);
}

void TestWriteNano::wrongStateWritesNothing()
{
    struct Case
    {
        bool installed;
        Operation op;
    };
    const Case cases[] = {
        {false, Operation::Update}, {false, Operation::Uninstall}, {true, Operation::Install}};
    for (const Case &c : cases) {
        const Img img = make(c.installed);
        const QByteArray before = hashOf(img.path);
        FaultSectorDevice *w = nullptr;
        const OperationResult res = run(img, c.op, fileFor(0), &w);
        QVERIFY(!res.ok);
        QVERIFY(!res.wroteAnything);
        QCOMPARE(w->writeCount(), 0);
        QCOMPARE(hashOf(img.path), before);
    }
}

void TestWriteNano::aWriteThatFailsStopsTheOperation()
{
    // Install: the data, then the directory.
    const WriteStep install[] = {WriteStep::WriteProgram, WriteStep::WriteDirectory};
    for (int n = 1; n <= 2; ++n) {
        const Img img = make(false, true);
        const OperationResult res = run(img, Operation::Install, fileFor(0), nullptr, n);
        QVERIFY(!res.ok);
        QVERIFY(res.wroteAnything);
        QCOMPARE(int(res.failedStep), int(install[n - 1]));
        QVERIFY(res.message.contains(QStringLiteral("the disk is gone")));
        // The directory does not point at the new bytes.
        if (n == 1)
            QCOMPARE(stateOf(img).state, State::Clean);
    }
    // Update: the removal, the data, the directory.
    const WriteStep update[] = {WriteStep::RemoveBootloader, WriteStep::WriteProgram,
                                WriteStep::WriteDirectory};
    for (int n = 1; n <= 3; ++n) {
        const Img img = make(true, true);
        const OperationResult res = run(img, Operation::Update, fileFor(0), nullptr, n);
        QVERIFY(!res.ok);
        QCOMPARE(int(res.failedStep), int(update[n - 1]));
        if (n == 2)
            QCOMPARE(stateOf(img).state, State::Clean);
    }
    // Uninstall: the directory.
    const Img img = make(true, true);
    const OperationResult res = run(img, Operation::Uninstall, QByteArray(), nullptr, 1);
    QVERIFY(!res.ok);
    QCOMPARE(int(res.failedStep), int(WriteStep::WriteDirectory));
}

QTEST_GUILESS_MAIN(TestWriteNano)
#include "tst_installer_write_nano.moc"
