// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// One test for each rule of docs/specs/bootloader-legacy.md section 24 ("what must
// never happen"), tests of section 26.4. Each one runs the operation on a fixture
// built to tempt the code into breaking the rule, and checks that the file is
// unchanged outside the area the operation may write.
//
// Two rules are not about the sector device and are tested with the helper: the
// "removable and on USB" part of rule 1 (the helper refuses a disk that is not,
// P4.4d and P4.5a) and rule 10, the lock on the disk during an operation (P4.4b).

#include "fixtures.h"
#include "operation.h"
#include "support.h"

#include <QTemporaryDir>
#include <QTest>
#include <functional>

using namespace tacetos::installer;
using namespace support;
namespace fp = tacetos::fakepod;

namespace {

// Passes everything to another device and calls `after` once a write has reached it.
class WatchDevice : public SectorDevice
{
public:
    WatchDevice(SectorDevice &inner, std::function<void()> after)
        : m_inner(inner), m_after(std::move(after))
    {
    }
    int sectorSize() const override { return m_inner.sectorSize(); }
    qint64 sectorCount() const override { return m_inner.sectorCount(); }
    bool isRealDisk() const override { return false; }
    IoResult readSectors(qint64 first, qint64 count, QByteArray &out) override
    {
        return m_inner.readSectors(first, count, out);
    }
    IoResult writeSectors(qint64 first, const QByteArray &data) override
    {
        const IoResult r = m_inner.writeSectors(first, data);
        if (r)
            m_after();
        return r;
    }
    IoResult flush() override { return m_inner.flush(); }

private:
    SectorDevice &m_inner;
    std::function<void()> m_after;
};

QByteArray videoFile()
{
    return fp::Fixtures::bootFile(fp::BootFileId::GoodIpodVideo);
}

OperationRequest opRequest(SectorDevice &dev, Operation op, const QString &backupDir,
                           const QByteArray &file)
{
    OperationRequest r;
    r.op = op;
    r.scan = takeScanSnapshot(dev, recognise(dev));
    r.bootloaderFile = file;
    r.backupDir = backupDir;
    r.allowRealWrites = false;
    return r;
}

FirmwareState stateOf(const QString &path, int sectorSize = kSector)
{
    FileSectorDevice dev(path, sectorSize);
    if (!dev.open())
        return {};
    return readFirmware(dev, recognise(dev));
}

bool inside(const FaultSectorDevice::WriteRecord &w, const Range &r)
{
    return w.first * kSector >= r.offset && (w.first + w.count) * kSector <= r.offset + r.length;
}

} // namespace

class TestRules : public QObject
{
    Q_OBJECT

private slots:
    void rule1_recognisedAndCheckedInTheSameOperation();
    void rule2_noRealDiskInADevBuild();
    void rule3_onlyTheAllowedZone();
    void rule4_appleFirmwareIsNeverChanged();
    void rule5_theDirectoryNeverPointsAtBytesNotWritten();
    void rule6_noInstallOnAMac();
    void rule7_noEraseWithoutTheTypedName();
    void rule8_aBackupIsKeptAfterAFailure();
    void rule9_noGuessing();
    void rule11_unpluggedAndPluggedBack();

private:
    QString m_backups;
    QTemporaryDir m_dir;
    int m_n = 0;
    QString name() { return m_dir.filePath(QStringLiteral("img%1.bin").arg(++m_n)); }
    QString backups() { return m_dir.filePath(QStringLiteral("backups%1").arg(++m_n)); }
    Img fixture(fp::FixtureId id)
    {
        Img i;
        i.path = name();
        const fp::FixtureBuild b = fp::Fixtures::build(id, i.path);
        i.info = b.pod;
        i.ok = b.ok;
        return i;
    }
    // Runs `what` on a fault device (no fault) over the image, and checks that it
    // wrote nothing and left the file as it was.
    void refusedWithoutWriting(const Img &img, int sectorSize,
                               const std::function<bool(FaultSectorDevice &)> &what)
    {
        QVERIFY(img.ok);
        const QByteArray before = hashOutside(img.path);
        FileSectorDevice file(img.path, sectorSize, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        QVERIFY(!what(dev));
        QCOMPARE(dev.writeCount(), 0);
        QCOMPARE(hashOutside(img.path), before);
    }
};

void TestRules::rule1_recognisedAndCheckedInTheSameOperation()
{
    // No banner: not recognised, so nothing runs, whatever is asked.
    const Img noBanner = fixture(fp::FixtureId::DosNoBanner);
    refusedWithoutWriting(noBanner, kSector, [&](FaultSectorDevice &d) {
        return runOperation(d, opRequest(d, Operation::Install, backups(), videoFile())).ok;
    });
    FormatRequest f;
    f.ipodName = f.typedName = "iPod";
    f.slotUnmounted = true;
    refusedWithoutWriting(noBanner, kSector,
                          [&](FaultSectorDevice &d) { return formatMusicPartition(d, f).ok; });
    refusedWithoutWriting(noBanner, kSector, [&](FaultSectorDevice &d) {
        return convertMacToFat32(d, convertRequest(d, backups())).ok;
    });
    // A disk that is not the one of the scan (check 1), and a disk that fails check 4.
    const auto changed = fp::Fixtures::info(fp::FixtureId::Check1DiskChanged);
    QVERIFY(changed.expected.scannedAs.has_value());
    const Img scanned = fixture(*changed.expected.scannedAs);
    FileSectorDevice scanFile(scanned.path, kSector);
    QVERIFY(scanFile.open());
    const ScanSnapshot scan = takeScanSnapshot(scanFile, recognise(scanFile));
    QVERIFY(scan.valid);
    refusedWithoutWriting(
        fixture(fp::FixtureId::Check1DiskChanged), kSector, [&](FaultSectorDevice &d) {
            OperationRequest r = opRequest(d, Operation::Install, backups(), videoFile());
            r.scan = scan;
            const OperationResult res = runOperation(d, r);
            return res.ok || res.report.failingCheck != 1;
        });
    refusedWithoutWriting(
        fixture(fp::FixtureId::Check4WrongByte), kSector, [&](FaultSectorDevice &d) {
            return runOperation(d, opRequest(d, Operation::Install, backups(),
                                             fp::Fixtures::bootFile(fp::BootFileId::WrongByte)))
                .ok;
        });
}

void TestRules::rule2_noRealDiskInADevBuild()
{
    const Img clean = makeImg(m_dir.path(), "r2a.img", "ipod-video", fp::TableKind::Dos);
    const Img installed = makeImg(m_dir.path(), "r2b.img", "ipod-video", fp::TableKind::Dos, true);
    const Img mac = makeImg(m_dir.path(), "r2c.img", "ipod-video", fp::TableKind::Apm);
    auto real = [](FaultSectorDevice &d) { d.setRealDisk(true); };
    refusedWithoutWriting(clean, kSector, [&](FaultSectorDevice &d) {
        real(d);
        return runOperation(d, opRequest(d, Operation::Install, backups(), videoFile())).ok;
    });
    for (const Operation op : {Operation::Update, Operation::Uninstall})
        refusedWithoutWriting(installed, kSector, [&](FaultSectorDevice &d) {
            real(d);
            return runOperation(d, opRequest(d, op, backups(), videoFile())).ok;
        });
    FormatRequest f;
    f.ipodName = f.typedName = "iPod";
    f.slotUnmounted = true;
    f.allowRealWrites = false;
    refusedWithoutWriting(clean, kSector, [&](FaultSectorDevice &d) {
        real(d);
        return formatMusicPartition(d, f).ok;
    });
    refusedWithoutWriting(mac, kSector, [&](FaultSectorDevice &d) {
        real(d);
        return convertMacToFat32(d, convertRequest(d, backups())).ok;
    });
}

void TestRules::rule3_onlyTheAllowedZone()
{
    // Install, update and uninstall write inside the firmware partition only.
    const Img img = makeImg(m_dir.path(), "r3.img", "ipod-video", fp::TableKind::Dos, false, true);
    QVERIFY(img.ok);
    const QByteArray outside = hashOutside(img.path, {img.firmware()});
    for (const Operation op : {Operation::Install, Operation::Update, Operation::Uninstall}) {
        FileSectorDevice file(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        const OperationResult res = runOperation(dev, opRequest(dev, op, backups(), videoFile()));
        QVERIFY2(res.ok, qPrintable(res.message));
        QVERIFY(!dev.writeLog().isEmpty());
        for (const auto &w : dev.writeLog())
            QVERIFY2(inside(w, img.firmware()), qPrintable(QString::number(w.first)));
        QCOMPARE(hashOutside(img.path, {img.firmware()}), outside);
    }
    // Format: slot 2 only.
    const Img win = makeImg(m_dir.path(), "r3b.img", "ipod-video", fp::TableKind::Dos);
    const QByteArray outsideMusic = hashOutside(win.path, {win.music()});
    {
        FileSectorDevice file(win.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        FormatRequest f;
        f.ipodName = f.typedName = "iPod";
        f.slotUnmounted = true;
        QVERIFY(formatMusicPartition(dev, f).ok);
        for (const auto &w : dev.writeLog())
            QVERIFY(inside(w, win.music()));
        QCOMPARE(hashOutside(win.path, {win.music()}), outsideMusic);
    }
    // Conversion: sector 0 and slot 2, nothing else.
    const Img mac = makeImg(m_dir.path(), "r3c.img", "ipod-video", fp::TableKind::Apm);
    const QByteArray outsideBoth = hashOutside(mac.path, {{0, kSector}, mac.music()});
    {
        FileSectorDevice file(mac.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(0);
        QVERIFY(convertMacToFat32(dev, convertRequest(dev, backups())).ok);
        for (const auto &w : dev.writeLog())
            QVERIFY(inside(w, mac.music()) || (w.first == 0 && w.count == 1));
        QCOMPARE(hashOutside(mac.path, {{0, kSector}, mac.music()}), outsideBoth);
    }
    // Tempting fixtures: a music partition that starts inside the firmware
    // partition (format and conversion), and an install with no room.
    const Img overlap = makeImg(m_dir.path(), "r3d.img", "ipod-video", fp::TableKind::Dos);
    {
        QFile f(overlap.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray first(4, '\0');
        const quint32 at = quint32(overlap.info.firmwareStart / kSector + 1);
        for (int i = 0; i < 4; ++i)
            first[i] = char((at >> (8 * i)) & 0xFF);
        f.seek(0x1BE + 16 + 8);
        f.write(first);
    }
    FormatRequest f;
    f.ipodName = f.typedName = "iPod";
    f.slotUnmounted = true;
    refusedWithoutWriting(overlap, kSector,
                          [&](FaultSectorDevice &d) { return formatMusicPartition(d, f).ok; });
    const Img macOverlap = makeImg(m_dir.path(), "r3e.img", "ipod-video", fp::TableKind::Apm);
    {
        QFile f2(macOverlap.path);
        QVERIFY(f2.open(QIODevice::ReadWrite));
        QByteArray first(4, '\0');
        put32be(first, 0, quint32(macOverlap.info.firmwareStart / kSector + 1));
        f2.seek(1536 + 8);
        f2.write(first);
    }
    refusedWithoutWriting(macOverlap, kSector, [&](FaultSectorDevice &d) {
        return convertMacToFat32(d, convertRequest(d, backups())).ok;
    });
    refusedWithoutWriting(fixture(fp::FixtureId::Check5NoRoom), kSector, [&](FaultSectorDevice &d) {
        const auto file = fp::Fixtures::info(fp::FixtureId::Check5NoRoom).bootFile;
        return runOperation(d, opRequest(d, Operation::Install, backups(),
                                         file ? fp::Fixtures::bootFile(*file) : videoFile()))
            .ok;
    });
}

void TestRules::rule4_appleFirmwareIsNeverChanged()
{
    struct Case
    {
        const char *model;
        fp::BootFileId file;
    };
    for (const Case &c : {Case{"ipod-video", fp::BootFileId::GoodIpodVideo},
                          Case{"nano-2g", fp::BootFileId::GoodNano2g}}) {
        const Img img = makeImg(m_dir.path(), QStringLiteral("r4%1.img").arg(c.model), c.model,
                                fp::TableKind::Dos, false, true);
        QVERIFY(img.ok || QString(c.model) == "nano-2g");
        const QString path = img.ok ? img.path : m_dir.filePath("r4nano.img");
        fp::FakePodResult info = img.info;
        int sectorSize = kSector;
        if (!img.ok) {
            fp::FakePodSpec spec;
            spec.modelId = c.model;
            spec.path = path;
            spec.withRsrc = spec.withAupd = true;
            info = fp::FakePod::build(spec);
            QVERIFY(info.ok);
            sectorSize = info.sectorSize;
        }
        const FirmwareState clean = stateOf(path, sectorSize);
        QCOMPARE(clean.state, State::Clean);
        const DirectoryEntry os = clean.entryList.at(clean.osIndex);
        const qint64 start = info.firmwareStart + clean.imageStart(os);
        const QByteArray apple = readAll(path, start, os.length);
        QCOMPARE(apple.size(), int(os.length));
        QList<Operation> ops = {Operation::Install};
        if (QString(c.model) != "nano-2g")
            ops << Operation::Update << Operation::Uninstall;
        for (const Operation op : ops) {
            FileSectorDevice file(path, sectorSize, FileSectorDevice::Mode::ReadWrite);
            QVERIFY(file.open());
            const OperationResult res =
                runOperation(file, opRequest(file, op, backups(), fp::Fixtures::bootFile(c.file)));
            QVERIFY2(res.ok, qPrintable(res.message));
            QCOMPARE(readAll(path, start, os.length), apple);
        }
    }
}

void TestRules::rule5_theDirectoryNeverPointsAtBytesNotWritten()
{
    struct Case
    {
        const char *model;
        bool installed;
        Operation op;
        fp::BootFileId file;
    };
    const Case cases[] = {
        {"ipod-video", false, Operation::Install, fp::BootFileId::GoodIpodVideo},
        {"ipod-video", true, Operation::Update, fp::BootFileId::GoodIpodVideo},
        {"ipod-video", true, Operation::Uninstall, fp::BootFileId::GoodIpodVideo},
        {"nano-2g", false, Operation::Install, fp::BootFileId::GoodNano2g},
    };
    for (const Case &c : cases) {
        fp::FakePodSpec spec;
        spec.modelId = c.model;
        spec.bootloaderPresent = c.installed;
        spec.withRsrc = spec.withAupd = true;
        spec.path = name();
        const fp::FakePodResult info = fp::FakePod::build(spec);
        QVERIFY(info.ok);
        const QString finalPath = name();
        const QString workPath = name();
        QVERIFY(QFile::copy(spec.path, finalPath) && QFile::copy(spec.path, workPath));
        {
            FileSectorDevice f(finalPath, info.sectorSize, FileSectorDevice::Mode::ReadWrite);
            QVERIFY(f.open());
            QVERIFY(
                runOperation(f, opRequest(f, c.op, backups(), fp::Fixtures::bootFile(c.file))).ok);
        }
        const FirmwareState startState = stateOf(workPath);
        auto known = [&](const DirectoryEntry &e) {
            for (const DirectoryEntry &o : startState.entryList)
                if (o.devOffset == e.devOffset && o.length == e.length && o.kind == e.kind)
                    return true;
            return false;
        };
        FileSectorDevice work(workPath, info.sectorSize, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(work.open());
        int checks = 0;
        QStringList problems;
        WatchDevice watch(work, [&]() {
            ++checks;
            FileSectorDevice peek(workPath, info.sectorSize);
            if (!peek.open())
                return;
            const Recognition rec = recognise(peek);
            if (rec.verdict != Verdict::Recognised) {
                problems << QStringLiteral("write %1: not recognised").arg(checks);
                return;
            }
            const FirmwareState s = readFirmware(peek, rec);
            int osos = 0;
            for (const DirectoryEntry &e : s.entryList) {
                osos += e.kind == EntryKind::Osos;
                if (known(e))
                    continue;
                // A new or changed entry: its bytes are already what they will end as.
                const qint64 at = info.firmwareStart + s.imageStart(e);
                if (readAll(workPath, at, e.length) != readAll(finalPath, at, e.length))
                    problems << QStringLiteral("write %1: an entry points at unwritten bytes")
                                    .arg(checks);
            }
            if (osos != 1)
                problems << QStringLiteral("write %1: %2 OSOS entries").arg(checks).arg(osos);
        });
        const OperationResult res =
            runOperation(watch, opRequest(watch, c.op, backups(), fp::Fixtures::bootFile(c.file)));
        QVERIFY2(res.ok, qPrintable(res.message));
        QVERIFY(checks >= 1);
        QVERIFY2(problems.isEmpty(), qPrintable(QString(c.model) + ": " + problems.join("; ")));
    }
}

void TestRules::rule6_noInstallOnAMac()
{
    const Img mac = makeImg(m_dir.path(), "r6a.img", "ipod-video", fp::TableKind::Apm);
    const Img macInstalled =
        makeImg(m_dir.path(), "r6b.img", "ipod-video", fp::TableKind::Apm, true);
    refusedWithoutWriting(mac, kSector, [&](FaultSectorDevice &d) {
        const OperationResult r =
            runOperation(d, opRequest(d, Operation::Install, backups(), videoFile()));
        return r.ok || !r.message.contains("Mac");
    });
    for (const Operation op : {Operation::Update, Operation::Uninstall})
        refusedWithoutWriting(macInstalled, kSector, [&](FaultSectorDevice &d) {
            return runOperation(d, opRequest(d, op, backups(), videoFile())).ok;
        });
    // Formatting the HFS+ partition outside the conversion.
    FormatRequest f;
    f.ipodName = f.typedName = "iPod";
    f.slotUnmounted = true;
    refusedWithoutWriting(mac, kSector,
                          [&](FaultSectorDevice &d) { return formatMusicPartition(d, f).ok; });
}

void TestRules::rule7_noEraseWithoutTheTypedName()
{
    const Img win = makeImg(m_dir.path(), "r7a.img", "ipod-video", fp::TableKind::Dos);
    const Img mac = makeImg(m_dir.path(), "r7b.img", "ipod-video", fp::TableKind::Apm);
    for (const QString &typed : {QString(), QStringLiteral("ipod"), QStringLiteral("iPod 2")}) {
        FormatRequest f;
        f.ipodName = "iPod";
        f.typedName = typed;
        f.slotUnmounted = true;
        refusedWithoutWriting(win, kSector,
                              [&](FaultSectorDevice &d) { return formatMusicPartition(d, f).ok; });
        refusedWithoutWriting(mac, kSector, [&](FaultSectorDevice &d) {
            ConvertRequest c = convertRequest(d, backups());
            c.ipodName = "iPod";
            c.typedName = typed;
            return convertMacToFat32(d, c).ok;
        });
    }
    // Neither a request left as it is by default.
    refusedWithoutWriting(win, kSector, [&](FaultSectorDevice &d) {
        FormatRequest f;
        f.slotUnmounted = true;
        return formatMusicPartition(d, f).ok;
    });
}

void TestRules::rule8_aBackupIsKeptAfterAFailure()
{
    for (int n = 1; n <= 5; ++n) {
        const Img img = makeImg(m_dir.path(), QStringLiteral("r8%1.img").arg(n), "ipod-video",
                                fp::TableKind::Dos, false, true);
        const QByteArray firmware =
            readAll(img.path, img.info.firmwareStart, img.info.firmwareBytes);
        FileSectorDevice file(img.path, kSector, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice dev(file);
        dev.failAtWrite(n);
        const OperationResult res =
            runOperation(dev, opRequest(dev, Operation::Install, backups(), videoFile()));
        if (res.ok) {
            // The install has fewer than n writes: every earlier write was cut.
            QVERIFY(n > 2);
            break;
        }
        QVERIFY(res.wroteAnything);
        // The disk stays gone, so the restore fails as well: the backup is all
        // that is left, and it is still there, as it was.
        QVERIFY2(QFile::exists(res.report.backupPath), qPrintable(QString::number(n)));
        QCOMPARE(readAll(res.report.backupPath), firmware);
    }
    // The conversion keeps both copies after a failed write.
    const Img mac = makeImg(m_dir.path(), "r8m.img", "ipod-video", fp::TableKind::Apm);
    FileSectorDevice file(mac.path, kSector, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice dev(file);
    dev.failAtWrite(1);
    const ConvertResult res = convertMacToFat32(dev, convertRequest(dev, backups()));
    QVERIFY(!res.ok);
    QVERIFY(QFile::exists(res.backupPath) && QFile::exists(res.mapBackupPath));
}

void TestRules::rule9_noGuessing()
{
    // Every fixture that is not a clean reading: an unknown model, a table or a
    // directory the spec does not describe. Nothing is installed, formatted or
    // converted on it.
    int seen = 0;
    for (const fp::FixtureInfo &info : fp::Fixtures::all()) {
        const fp::Expected &e = info.expected;
        if (e.verdict == fp::Verdict::Recognised && e.state != fp::State::Inconsistent)
            continue;
        ++seen;
        const Img img = fixture(info.id);
        QVERIFY2(img.ok, info.name);
        const int sectorSize = e.reportedSectorSize ? e.reportedSectorSize : img.info.sectorSize;
        const QString label = QString::fromLatin1(info.name);
        const std::function<bool(FaultSectorDevice &)> ops[] = {
            [&](FaultSectorDevice &d) {
                return runOperation(d, opRequest(d, Operation::Install, backups(), videoFile())).ok;
            },
            [&](FaultSectorDevice &d) {
                FormatRequest f;
                f.ipodName = f.typedName = "iPod";
                f.slotUnmounted = true;
                return formatMusicPartition(d, f).ok;
            },
            [&](FaultSectorDevice &d) {
                return convertMacToFat32(d, convertRequest(d, backups())).ok;
            },
        };
        for (const auto &op : ops)
            refusedWithoutWriting(img, sectorSize, op);
        if (QTest::currentTestFailed()) {
            qWarning("fixture %s", qPrintable(label));
            return;
        }
    }
    QVERIFY(seen >= 10);
}

void TestRules::rule11_unpluggedAndPluggedBack()
{
    // The same name and the same model, but the disk is not the scanned one any more:
    // a byte of the first firmware sector, or of sector 0, is not what the scan saw.
    for (const bool mac : {false, true}) {
        const Img img = makeImg(m_dir.path(), mac ? "r11m.img" : "r11w.img", "ipod-video",
                                mac ? fp::TableKind::Apm : fp::TableKind::Dos);
        QVERIFY(img.ok);
        FileSectorDevice scanDev(img.path, kSector);
        QVERIFY(scanDev.open());
        const ScanSnapshot scan = takeScanSnapshot(scanDev, recognise(scanDev));
        QVERIFY(scan.valid);
        QFile f(img.path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(mac ? 200 : img.info.firmwareStart + 400);
        f.write("\x5A");
        f.close();
        refusedWithoutWriting(img, kSector, [&](FaultSectorDevice &d) {
            if (mac) {
                ConvertRequest c = convertRequest(d, backups());
                c.scan = scan;
                return convertMacToFat32(d, c).ok;
            }
            OperationRequest r = opRequest(d, Operation::Install, backups(), videoFile());
            r.scan = scan;
            const OperationResult res = runOperation(d, r);
            return res.ok || res.report.failingCheck != 1;
        });
    }
}

QTEST_GUILESS_MAIN(TestRules)
#include "tst_installer_rules.moc"
