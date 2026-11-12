// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Install, update and uninstall of the bootloader appended inside the OSOS image
// (docs/specs/bootloader-legacy.md sections 13, 14, 14.1 and 16, tests of section
// 26.3 for this layout). The expected images are built by the fake iPod library
// with the bootloader already present, and the expected directory values are
// written again here from the spec.

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

constexpr int kSector = 512;

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

QByteArray firmwareOf(const QString &path, const fp::FakePodResult &pod)
{
    return readAll(path, pod.firmwareStart, pod.firmwareBytes);
}

void patch(const QString &path, qint64 offset, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(offset));
    QCOMPARE(f.write(bytes), qint64(bytes.size()));
}

qint64 roundUp(qint64 n)
{
    return (n + kSector - 1) / kSector * kSector;
}

// Sector indexes (inside the partition) where two copies differ.
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

// A bootloader file for `target` with a program of `length` bytes.
QByteArray fileFor(const QString &target, int length, QByteArray *programOut = nullptr)
{
    const QByteArray program = fp::BootloaderFile::program(target, length);
    const fp::BootTarget *t = fp::BootloaderFile::target(target);
    if (programOut)
        *programOut = program;
    return fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(t->number, program),
                                        QByteArray(t->tag), program);
}

QString targetOf(const QString &modelId)
{
    return fp::BootloaderFile::targetForOsValue(fp::FakePod::model(modelId)->osValue);
}

QStringList appendedModels()
{
    QStringList ids = fp::FakePod::modelIds();
    ids.removeAll(QStringLiteral("nano-2g"));
    ids.removeAll(QStringLiteral("classic-6g"));
    return ids;
}

} // namespace

class TestWrite : public QObject
{
    Q_OBJECT

private slots:
    void installGivesTheInstalledImage_data();
    void installGivesTheInstalledImage();
    void installOnEveryCleanDirectoryHasTheExpectedEntry();
    void installWritesInTheOrderOfTheSpec();
    void installNeverWritesAppleFirmware();
    void imagesMoveFromTheEnd();
    void partitionTooSmallLeavesTheFileUnchanged();
    void updateWithALargerBootloader();
    void updateWithASmallerBootloader();
    void updateKeepsTheEntryOffset();
    void updateRemovesTheBootloaderFirst();
    void uninstallGivesTheCleanEntry_data();
    void uninstallGivesTheCleanEntry();
    void uninstallAfterInstallKeepsTheChecksum();
    void paddingAfterAppleFirmwareIsCountedAsOnDisk();
    void paddingOfTheProgramIsZero();
    void wrongStateWritesNothing();
    void aWriteThatFailsStopsTheOperation();

private:
    struct Img
    {
        QString path;
        fp::FakePodResult pod;
        QString target;
    };
    Img make(const QString &modelId, bool installed, bool rsrcAupd, const QByteArray &program = {})
    {
        Img i;
        i.path = m_dir.filePath(QStringLiteral("img%1.bin").arg(++m_count));
        fp::FakePodSpec spec;
        spec.modelId = modelId;
        spec.path = i.path;
        spec.bootloaderPresent = installed;
        spec.withRsrc = spec.withAupd = rsrcAupd;
        spec.bootloader = program;
        i.pod = fp::FakePod::build(spec);
        i.target = targetOf(modelId);
        return i;
    }
    // Runs an operation on the image through a device that logs the writes.
    OperationResult run(const Img &img, Operation op, const QByteArray &file,
                        FaultSectorDevice **watchedOut = nullptr, qint64 pieceBytes = kPieceBytes,
                        int failAt = 0)
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
        req.pieceBytes = pieceBytes;
        return runOperation(*m_watched, req);
    }
    FirmwareState stateOf(const Img &img)
    {
        FileSectorDevice dev(img.path, img.pod.sectorSize);
        if (!dev.open())
            return {};
        return readFirmware(dev, recognise(dev));
    }

    QTemporaryDir m_dir;
    int m_count = 0;
    std::unique_ptr<FileSectorDevice> m_file;
    std::unique_ptr<FaultSectorDevice> m_watched;
};

void TestWrite::installGivesTheInstalledImage_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<bool>("rsrcAupd");
    for (const QString &id : appendedModels()) {
        QTest::newRow(qPrintable(id + " alone")) << id << false;
        QTest::newRow(qPrintable(id + " with following images")) << id << true;
    }
}

void TestWrite::installGivesTheInstalledImage()
{
    QFETCH(QString, model);
    QFETCH(bool, rsrcAupd);
    QByteArray program;
    const QByteArray file = fileFor(targetOf(model), 0, &program);
    const Img before = make(model, false, rsrcAupd);
    const Img expected = make(model, true, rsrcAupd, program);
    QVERIFY(before.pod.ok && expected.pod.ok);

    const OperationResult res = run(before, Operation::Install, file);
    QVERIFY2(res.ok, qPrintable(res.message));
    QVERIFY(res.wroteAnything);
    QCOMPARE(stateOf(before).state, State::Installed);
    QCOMPARE(firmwareOf(before.path, before.pod), firmwareOf(expected.path, expected.pod));
    // The music partition and the table are not touched.
    QCOMPARE(readAll(before.path, 0, before.pod.firmwareStart),
             readAll(expected.path, 0, expected.pod.firmwareStart));
    QCOMPARE(readAll(before.path, before.pod.musicStart, 4096),
             readAll(expected.path, expected.pod.musicStart, 4096));
}

void TestWrite::installOnEveryCleanDirectoryHasTheExpectedEntry()
{
    for (const QString &id : appendedModels()) {
        QByteArray program;
        const QByteArray file = fileFor(targetOf(id), 0, &program);
        const Img img = make(id, false, false);
        const FirmwareState clean = stateOf(img);
        QCOMPARE(clean.state, State::Clean);
        const DirectoryEntry old = clean.entryList.at(0);
        const QByteArray fw = firmwareOf(img.path, img.pod);
        const qint64 osStart = clean.imageStart(old);
        const qint64 entryOffset = roundUp(old.length);

        const OperationResult res = run(img, Operation::Install, file);
        QVERIFY2(res.ok, qPrintable(id + ": " + res.message));
        const FirmwareState now = stateOf(img);
        QCOMPARE(now.state, State::Installed);
        const DirectoryEntry &e = now.entryList.at(0);
        QCOMPARE(qint64(e.entryOffset), entryOffset);
        QCOMPARE(qint64(e.length), entryOffset + program.size());
        QCOMPARE(e.flashAddress, quint32(0xFFFFFFFF));
        // The sum covers Apple's firmware with its padding, then the program.
        quint32 sum = 0;
        for (char c : fw.mid(int(osStart), int(entryOffset)))
            sum += quint8(c);
        for (char c : program)
            sum += quint8(c);
        QCOMPARE(e.checksum, sum);
        QCOMPARE(e.devOffset, old.devOffset);
        QCOMPARE(e.loadAddress, old.loadAddress);
        QCOMPARE(e.version, old.version);
    }
}

void TestWrite::installWritesInTheOrderOfTheSpec()
{
    const Img img = make("ipod-video", false, false);
    const FirmwareState clean = stateOf(img);
    const qint64 dataStart = img.pod.firmwareStart + clean.imageStart(clean.entryList.at(0)) +
                             roundUp(clean.entryList.at(0).length);
    FaultSectorDevice *w = nullptr;
    const OperationResult res = run(img, Operation::Install, fileFor("ipodvideo", 0), &w);
    QVERIFY2(res.ok, qPrintable(res.message));
    // Data first, then one directory sector.
    QCOMPARE(w->writeLog().size(), 2);
    QCOMPARE(w->writeLog().at(0).first * kSector, dataStart);
    QCOMPARE(w->writeLog().at(0).count, qint64(8));
    QCOMPARE(w->writeLog().at(1).first * kSector,
             img.pod.firmwareStart + img.pod.directoryOffset / kSector * kSector);
    QCOMPARE(w->writeLog().at(1).count, qint64(1));
    // Every write is flushed before the next one.
    QVERIFY(w->flushCount() >= w->writeCount());
}

void TestWrite::installNeverWritesAppleFirmware()
{
    for (bool follow : {false, true}) {
        const Img img = make("ipod-video", false, follow);
        const FirmwareState clean = stateOf(img);
        const qint64 osStart = img.pod.firmwareStart + clean.imageStart(clean.entryList.at(0));
        const qint64 appleEnd = osStart + roundUp(clean.entryList.at(0).length);
        FaultSectorDevice *w = nullptr;
        const OperationResult res = run(img, Operation::Install, fileFor("ipodvideo", 0), &w);
        QVERIFY2(res.ok, qPrintable(res.message));
        for (const auto &rec : w->writeLog()) {
            const qint64 from = rec.first * kSector;
            const qint64 to = from + rec.count * kSector;
            QVERIFY2(to <= osStart || from >= appleEnd, "a write touched Apple's firmware");
        }
    }
}

void TestWrite::imagesMoveFromTheEnd()
{
    QByteArray program;
    const QByteArray file = fileFor("ipodvideo", 0, &program);
    const Img img = make("ipod-video", false, true);
    const Img expected = make("ipod-video", true, true, program);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry rsrc = clean.entryList.at(1);
    const qint64 blockEnd =
        clean.imageStart(clean.entryList.at(2)) + clean.entryList.at(2).paddedBytes(kSector);
    const qint64 blockStart = clean.imageStart(rsrc);

    FaultSectorDevice *w = nullptr;
    // Pieces of two sectors: the copy overlaps its own source and runs backwards.
    const OperationResult res = run(img, Operation::Install, file, &w, 2 * kSector);
    QVERIFY2(res.ok, qPrintable(res.message));
    const qint64 delta = res.report.plan.moveDelta;
    QCOMPARE(delta, roundUp(roundUp(clean.entryList.at(0).length) + program.size() +
                            clean.imageStart(clean.entryList.at(0)) - blockStart));
    QCOMPARE(res.report.plan.moveStart, blockStart);
    QCOMPARE(res.report.plan.moveEnd, blockEnd);
    // The moved block: the pieces, then the directory, the program, the directory.
    const int pieces = int((blockEnd - blockStart + 2 * kSector - 1) / (2 * kSector));
    QCOMPARE(w->writeLog().size(), pieces + 3);
    for (int i = 1; i < pieces; ++i)
        QVERIFY(w->writeLog().at(i).first < w->writeLog().at(i - 1).first);
    QCOMPARE(firmwareOf(img.path, img.pod), firmwareOf(expected.path, expected.pod));
    const FirmwareState now = stateOf(img);
    QCOMPARE(qint64(now.entryList.at(1).devOffset), qint64(rsrc.devOffset) + delta);
    QCOMPARE(qint64(now.entryList.at(2).devOffset),
             qint64(clean.entryList.at(2).devOffset) + delta);
}

void TestWrite::partitionTooSmallLeavesTheFileUnchanged()
{
    const QString path = m_dir.filePath(QStringLiteral("small.img"));
    const fp::FixtureBuild b = fp::Fixtures::build(fp::FixtureId::Check5NoRoom, path);
    QVERIFY(b.ok);
    const QByteArray before = hashOf(path);
    Img img;
    img.path = path;
    img.pod = b.pod.ok ? b.pod : fp::FakePodResult();
    FaultSectorDevice *w = nullptr;
    const OperationResult res = run(img, Operation::Install, fileFor("ipodvideo", 0), &w);
    QVERIFY(!res.ok);
    QCOMPARE(res.message, QStringLiteral("check 5: not enough room in the firmware partition"));
    QCOMPARE(res.report.failingCheck, 5);
    QVERIFY(!res.wroteAnything);
    QCOMPARE(w->writeCount(), 0);
    QCOMPARE(hashOf(path), before);
}

void TestWrite::updateWithALargerBootloader()
{
    for (bool follow : {false, true}) {
        QByteArray oldProgram, newProgram;
        fileFor("ipodvideo", 3000, &oldProgram);
        const QByteArray file = fileFor("ipodvideo", 9000, &newProgram);
        const Img img = make("ipod-video", true, follow, oldProgram);
        const Img expected = make("ipod-video", true, follow, newProgram);
        FaultSectorDevice *w = nullptr;
        const OperationResult res = run(img, Operation::Update, file, &w);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(stateOf(img).state, State::Installed);
        QCOMPARE(firmwareOf(img.path, img.pod), firmwareOf(expected.path, expected.pod));
        QCOMPARE(res.report.plan.moveDelta > 0, follow);
        // Removal, (the move and its directory), program, directory.
        QCOMPARE(w->writeLog().size(), follow ? 5 : 3);
    }
}

void TestWrite::updateWithASmallerBootloader()
{
    for (bool follow : {false, true}) {
        QByteArray oldProgram, newProgram;
        fileFor("ipodvideo", 9000, &oldProgram);
        const QByteArray file = fileFor("ipodvideo", 1000, &newProgram);
        const Img img = make("ipod-video", true, follow, oldProgram);
        const QByteArray before = firmwareOf(img.path, img.pod);
        const FirmwareState old = stateOf(img);
        const DirectoryEntry os = old.entryList.at(0);

        const OperationResult res = run(img, Operation::Update, file);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(res.report.plan.moveDelta, qint64(0));
        const FirmwareState now = stateOf(img);
        QCOMPARE(now.state, State::Installed);
        QCOMPARE(now.entryList.at(0).entryOffset, os.entryOffset);
        QCOMPARE(qint64(now.entryList.at(0).length), qint64(os.entryOffset) + 1000);
        // The following images stay where they were.
        for (int i = 1; i < now.entryList.size(); ++i)
            QCOMPARE(now.entryList.at(i).devOffset, old.entryList.at(i).devOffset);
        // Only the program and the directory sector changed; the rest of the old
        // bootloader stays, unreferenced.
        const QByteArray after = firmwareOf(img.path, img.pod);
        const int dirSector = int(img.pod.directoryOffset / kSector);
        const int first = int((old.imageStart(os) + os.entryOffset) / kSector);
        const int last = first + int(roundUp(1000) / kSector) - 1;
        for (int s : differingSectors(before, after))
            QVERIFY2(s == dirSector || (s >= first && s <= last), qPrintable(QString::number(s)));
        QCOMPARE(after.mid(first * kSector, 1000), newProgram);
        QCOMPARE(after.mid(first * kSector + 1000, roundUp(1000) - 1000),
                 QByteArray(int(roundUp(1000) - 1000), '\0'));
        quint32 sum = 0;
        for (char c : after.mid(int(old.imageStart(os)), int(os.entryOffset) + 1000))
            sum += quint8(c);
        QCOMPARE(now.entryList.at(0).checksum, sum);
    }
}

void TestWrite::updateKeepsTheEntryOffset()
{
    QByteArray oldProgram;
    fileFor("ipod4g", 3000, &oldProgram);
    const Img img = make("ipod-4g", true, false, oldProgram);
    const quint32 offset = stateOf(img).entryList.at(0).entryOffset;
    QVERIFY(offset != 0);
    for (int length : {500, 6000, 3000}) {
        const OperationResult res = run(img, Operation::Update, fileFor("ipod4g", length));
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(stateOf(img).entryList.at(0).entryOffset, offset);
        QCOMPARE(qint64(stateOf(img).entryList.at(0).length), qint64(offset) + length);
    }
}

void TestWrite::updateRemovesTheBootloaderFirst()
{
    // The first write of an update is the directory sector with the clean entry.
    QByteArray oldProgram;
    fileFor("ipodvideo", 3000, &oldProgram);
    const Img img = make("ipod-video", true, false, oldProgram);
    const FirmwareState old = stateOf(img);
    FaultSectorDevice *w = nullptr;
    const OperationResult res =
        run(img, Operation::Update, fileFor("ipodvideo", 3000), &w, kPieceBytes, 2);
    QVERIFY(!res.ok);
    QCOMPARE(int(res.failedStep), int(WriteStep::WriteProgram));
    QVERIFY(res.wroteAnything);
    // The iPod is clean: Apple's firmware only, no entry offset.
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Clean);
    QCOMPARE(now.entryList.at(0).entryOffset, quint32(0));
    QCOMPARE(now.entryList.at(0).length, old.entryList.at(0).entryOffset);
}

void TestWrite::uninstallGivesTheCleanEntry_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<bool>("rsrcAupd");
    for (const QString &id : appendedModels()) {
        QTest::newRow(qPrintable(id + " alone")) << id << false;
        QTest::newRow(qPrintable(id + " with following images")) << id << true;
    }
}

void TestWrite::uninstallGivesTheCleanEntry()
{
    QFETCH(QString, model);
    QFETCH(bool, rsrcAupd);
    const Img img = make(model, true, rsrcAupd);
    const FirmwareState old = stateOf(img);
    QCOMPARE(old.state, State::Installed);
    const QByteArray before = firmwareOf(img.path, img.pod);
    const DirectoryEntry os = old.entryList.at(0);

    const OperationResult res = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Clean);
    const DirectoryEntry &e = now.entryList.at(0);
    QCOMPARE(e.length, os.entryOffset);
    QCOMPARE(e.entryOffset, quint32(0));
    QCOMPARE(e.flashAddress, os.flashAddress);
    quint32 sum = 0;
    for (char c : before.mid(int(old.imageStart(os)), int(os.entryOffset)))
        sum += quint8(c);
    QCOMPARE(e.checksum, sum);
    // Only the directory sector changed: the bootloader's bytes stay, and the
    // images do not move back.
    const QList<int> diff = differingSectors(before, firmwareOf(img.path, img.pod));
    QCOMPARE(diff, QList<int>{int(img.pod.directoryOffset / kSector)});
    QCOMPARE(now.entryList.size(), old.entryList.size());
    for (int i = 1; i < now.entryList.size(); ++i)
        QCOMPARE(now.entryList.at(i).devOffset, old.entryList.at(i).devOffset);
}

void TestWrite::uninstallAfterInstallKeepsTheChecksum()
{
    const Img img = make("ipod-video", false, true);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry os = clean.entryList.at(0);
    QVERIFY2(run(img, Operation::Install, fileFor("ipodvideo", 0)).ok, "install");
    const OperationResult res = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(res.ok, qPrintable(res.message));
    const FirmwareState now = stateOf(img);
    QCOMPARE(now.state, State::Clean);
    // The length grows to the padded size; the zero padding does not change the sum.
    QCOMPARE(qint64(now.entryList.at(0).length), roundUp(os.length));
    QCOMPARE(now.entryList.at(0).checksum, os.checksum);
}

void TestWrite::paddingAfterAppleFirmwareIsCountedAsOnDisk()
{
    const Img img = make("ipod-video", false, false);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry os = clean.entryList.at(0);
    const qint64 osStart = clean.imageStart(os);
    const qint64 padStart = img.pod.firmwareStart + osStart + os.length;
    const int padLength = int(roundUp(os.length) - os.length);
    QVERIFY(padLength > 0);
    patch(img.path, padStart, QByteArray(padLength, '\xAA'));
    const QByteArray apple =
        firmwareOf(img.path, img.pod).mid(int(osStart), int(roundUp(os.length)));

    QByteArray program;
    const QByteArray file = fileFor("ipodvideo", 0, &program);
    OperationResult res = run(img, Operation::Install, file);
    QVERIFY2(res.ok, qPrintable(res.message));
    // Not zeroed, and counted in the sum.
    QCOMPARE(firmwareOf(img.path, img.pod).mid(int(osStart), apple.size()), apple);
    quint32 sum = 0;
    for (char c : apple + program)
        sum += quint8(c);
    QCOMPARE(stateOf(img).entryList.at(0).checksum, sum);

    // The uninstall counts it too (section 16 step 3).
    res = run(img, Operation::Uninstall, QByteArray());
    QVERIFY2(res.ok, qPrintable(res.message));
    quint32 appleSum = 0;
    for (char c : apple)
        appleSum += quint8(c);
    QCOMPARE(stateOf(img).entryList.at(0).checksum, appleSum);
    QVERIFY(stateOf(img).entryList.at(0).checksum != os.checksum);
}

void TestWrite::paddingOfTheProgramIsZero()
{
    // Leftovers in the free space after Apple's firmware must not reach the new
    // padding (section 13 rule 6).
    const Img img = make("ipod-video", false, false);
    const FirmwareState clean = stateOf(img);
    const DirectoryEntry os = clean.entryList.at(0);
    const qint64 dataStart = img.pod.firmwareStart + clean.imageStart(os) + roundUp(os.length);
    patch(img.path, dataStart, QByteArray(16 * kSector, '\xEE'));
    const OperationResult res = run(img, Operation::Install, fileFor("ipodvideo", 4000));
    QVERIFY2(res.ok, qPrintable(res.message));
    const QByteArray area = readAll(img.path, dataStart, 16 * kSector);
    QCOMPARE(area.mid(4000, 8 * kSector - 4000), QByteArray(8 * kSector - 4000, '\0'));
    QCOMPARE(area.mid(8 * kSector), QByteArray(8 * kSector, '\xEE'));
}

void TestWrite::wrongStateWritesNothing()
{
    const Img clean = make("ipod-video", false, false);
    const Img installed = make("ipod-video", true, false);
    const QByteArray cleanBefore = hashOf(clean.path);
    const QByteArray installedBefore = hashOf(installed.path);
    FaultSectorDevice *w = nullptr;
    OperationResult res = run(clean, Operation::Uninstall, QByteArray(), &w);
    QVERIFY(!res.ok && !res.wroteAnything);
    QCOMPARE(w->writeCount(), 0);
    res = run(clean, Operation::Update, fileFor("ipodvideo", 0), &w);
    QVERIFY(!res.ok);
    QCOMPARE(w->writeCount(), 0);
    res = run(installed, Operation::Install, fileFor("ipodvideo", 0), &w);
    QVERIFY(!res.ok);
    QCOMPARE(w->writeCount(), 0);
    QCOMPARE(hashOf(clean.path), cleanBefore);
    QCOMPARE(hashOf(installed.path), installedBefore);
}

void TestWrite::aWriteThatFailsStopsTheOperation()
{
    // An install that moves images: move, directory, program, directory.
    const WriteStep steps[] = {WriteStep::MoveImages, WriteStep::MoveDirectory,
                               WriteStep::WriteProgram, WriteStep::WriteDirectory};
    for (int n = 1; n <= 4; ++n) {
        const Img img = make("ipod-video", false, true);
        const OperationResult res =
            run(img, Operation::Install, fileFor("ipodvideo", 0), nullptr, kPieceBytes, n);
        QVERIFY(!res.ok);
        QCOMPARE(int(res.failedStep), int(steps[n - 1]));
        QVERIFY(res.wroteAnything);
        QVERIFY(res.message.contains(QStringLiteral("the disk is gone")));
        // A failure before the last directory write leaves a clean iPod.
        if (n >= 2 && n <= 3)
            QCOMPARE(stateOf(img).state, State::Clean);
    }
}

QTEST_GUILESS_MAIN(TestWrite)
#include "tst_installer_write.moc"
