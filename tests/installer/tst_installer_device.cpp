// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The SectorDevice of the installer: the disk image file, the whole-sector
// rules of docs/specs/bootloader-legacy.md section 13.1, and the device that
// fails at the n-th write.

#include "faultdevice.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;

namespace {

// A file of `sectors` sectors of `size` bytes: sector n is filled with the byte n.
QString makeImage(const QTemporaryDir &dir, int size, int sectors)
{
    const QString path = dir.filePath(QStringLiteral("disk.img"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    for (int i = 0; i < sectors; ++i)
        f.write(QByteArray(size, char(i)));
    return path;
}

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestDevice : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); }

    void fileDeviceReports();
    void fileDeviceReadsWholeSectors();
    void fileDeviceRefusesOutsideTheDisk();
    void fileDeviceWritesWholeSectorsOnly();
    void readOnlyDeviceNeverWrites();
    void missingFileDoesNotOpen();
    void readBytesCutsTheSectors();
    void readBytesRefusesOutside();

    void faultDeviceIsTransparent();
    void faultDeviceFailsAtTheNthWrite();
    void faultDeviceStaysFailedOrNot();
    void faultDeviceTornWrite();
    void faultDeviceRealDiskFlag();

private:
    QTemporaryDir m_dir;
};

void TestDevice::fileDeviceReports()
{
    const QString p = makeImage(m_dir, 2048, 10);
    FileSectorDevice d(p, 2048);
    QVERIFY(d.open());
    QCOMPARE(d.sectorSize(), 2048);
    QCOMPARE(d.sectorCount(), qint64(10));
    QCOMPARE(d.sizeBytes(), qint64(20480));
    QVERIFY(!d.isRealDisk());
}

void TestDevice::fileDeviceReadsWholeSectors()
{
    const QString p = makeImage(m_dir, 512, 8);
    FileSectorDevice d(p, 512);
    QVERIFY(d.open());
    QByteArray out;
    QVERIFY(d.readSectors(2, 3, out));
    QCOMPARE(out.size(), 1536);
    QCOMPARE(out.left(512), QByteArray(512, char(2)));
    QCOMPARE(out.mid(1024), QByteArray(512, char(4)));
    // The last sector, and an empty read.
    QVERIFY(d.readSectors(7, 1, out));
    QCOMPARE(out, QByteArray(512, char(7)));
    QVERIFY(d.readSectors(8, 0, out));
    QVERIFY(out.isEmpty());
}

void TestDevice::fileDeviceRefusesOutsideTheDisk()
{
    const QString p = makeImage(m_dir, 512, 8);
    FileSectorDevice d(p, 512, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(d.open());
    QByteArray out;
    QVERIFY(!d.readSectors(7, 2, out));
    QVERIFY(out.isEmpty());
    QVERIFY(!d.readSectors(-1, 1, out));
    QVERIFY(!d.readSectors(8, 1, out));
    QVERIFY(!d.writeSectors(7, QByteArray(1024, 'x')));
    QVERIFY(!d.writeSectors(8, QByteArray(512, 'x')));
    QCOMPARE(fileBytes(p).size(), 4096);
}

void TestDevice::fileDeviceWritesWholeSectorsOnly()
{
    const QString p = makeImage(m_dir, 512, 4);
    FileSectorDevice d(p, 512, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(d.open());
    // A partial sector is refused, and nothing is written.
    const QByteArray before = fileBytes(p);
    QVERIFY(!d.writeSectors(1, QByteArray(100, 'x')));
    QCOMPARE(fileBytes(p), before);
    QVERIFY(d.writeSectors(1, QByteArray(1024, 'x')));
    QVERIFY(d.flush());
    const QByteArray after = fileBytes(p);
    QCOMPARE(after.left(512), QByteArray(512, char(0)));
    QCOMPARE(after.mid(512, 1024), QByteArray(1024, 'x'));
    QCOMPARE(after.mid(1536), QByteArray(512, char(3)));
    QCOMPARE(after.size(), before.size());
}

void TestDevice::readOnlyDeviceNeverWrites()
{
    const QString p = makeImage(m_dir, 512, 4);
    const QByteArray before = fileBytes(p);
    FileSectorDevice d(p, 512);
    QVERIFY(d.open());
    const IoResult r = d.writeSectors(0, QByteArray(512, 'x'));
    QVERIFY(!r);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(d.flush());
    QCOMPARE(fileBytes(p), before);
}

void TestDevice::missingFileDoesNotOpen()
{
    FileSectorDevice d(m_dir.filePath(QStringLiteral("nothing.img")), 512);
    QVERIFY(!d.open());
    QCOMPARE(d.sectorCount(), qint64(0));
    QByteArray out;
    QVERIFY(!d.readSectors(0, 1, out));
    FileSectorDevice zero(makeImage(m_dir, 512, 1), 0);
    QVERIFY(!zero.open());
}

void TestDevice::readBytesCutsTheSectors()
{
    const QString p = makeImage(m_dir, 512, 8);
    FileSectorDevice d(p, 512);
    QVERIFY(d.open());
    QByteArray out;
    // Across the border of sectors 1 and 2.
    QVERIFY(readBytes(d, 512 + 510, 4, out));
    QCOMPARE(out, QByteArray("\1\1\2\2", 4));
    QVERIFY(readBytes(d, 3 * 512, 512, out));
    QCOMPARE(out, QByteArray(512, char(3)));
    QVERIFY(readBytes(d, 100, 0, out));
    QVERIFY(out.isEmpty());
    // The very last byte of the disk.
    QVERIFY(readBytes(d, 8 * 512 - 1, 1, out));
    QCOMPARE(out, QByteArray(1, char(7)));
}

void TestDevice::readBytesRefusesOutside()
{
    const QString p = makeImage(m_dir, 512, 2);
    FileSectorDevice d(p, 512);
    QVERIFY(d.open());
    QByteArray out;
    QVERIFY(!readBytes(d, 1023, 2, out));
    QVERIFY(!readBytes(d, -1, 2, out));
    QVERIFY(!readBytes(d, 0, 1025, out));
    QVERIFY(out.isEmpty());
}

void TestDevice::faultDeviceIsTransparent()
{
    const QString p = makeImage(m_dir, 512, 6);
    FileSectorDevice file(p, 512, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice d(file);
    QCOMPARE(d.sectorSize(), 512);
    QCOMPARE(d.sectorCount(), qint64(6));
    QVERIFY(!d.isRealDisk());
    QVERIFY(d.writeSectors(1, QByteArray(512, 'a')));
    QVERIFY(d.writeSectors(2, QByteArray(1024, 'b')));
    QVERIFY(d.flush());
    QByteArray out;
    QVERIFY(d.readSectors(1, 3, out));
    QCOMPARE(out, QByteArray(512, 'a') + QByteArray(1024, 'b'));
    QCOMPARE(d.writeCount(), 2);
    QCOMPARE(d.flushCount(), 1);
    QVERIFY(!d.hasFailed());
    QCOMPARE(d.writeLog().size(), 2);
    QCOMPARE(d.writeLog().at(1).first, qint64(2));
    QCOMPARE(d.writeLog().at(1).count, qint64(2));
}

void TestDevice::faultDeviceFailsAtTheNthWrite()
{
    for (int n = 1; n <= 4; ++n) {
        const QString p = makeImage(m_dir, 512, 6);
        FileSectorDevice file(p, 512, FileSectorDevice::Mode::ReadWrite);
        QVERIFY(file.open());
        FaultSectorDevice d(file);
        d.failAtWrite(n, QStringLiteral("no space left"));
        int done = 0;
        IoResult last;
        for (int i = 0; i < 4; ++i) {
            last = d.writeSectors(i, QByteArray(512, char('a' + i)));
            if (!last)
                break;
            ++done;
        }
        QCOMPARE(done, n - 1);
        QVERIFY(!last);
        QCOMPARE(last.error, QStringLiteral("no space left"));
        QVERIFY(d.hasFailed());
        QCOMPARE(d.writeLog().size(), n - 1);
        // The failing write left the sector as it was, and the ones before it are there.
        QVERIFY(file.flush());
        const QByteArray bytes = fileBytes(p);
        for (int i = 0; i < 4; ++i) {
            const char want = i < n - 1 ? char('a' + i) : char(i);
            QCOMPARE(bytes.mid(i * 512, 512), QByteArray(512, want));
        }
    }
}

void TestDevice::faultDeviceStaysFailedOrNot()
{
    const QString p = makeImage(m_dir, 512, 6);
    FileSectorDevice file(p, 512, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice d(file);
    d.failAtWrite(2);
    QVERIFY(d.writeSectors(0, QByteArray(512, 'a')));
    QVERIFY(!d.writeSectors(1, QByteArray(512, 'b')));
    // Unplugged: the writes and the flush after it fail too, the reads still answer.
    QVERIFY(!d.writeSectors(2, QByteArray(512, 'c')));
    QVERIFY(!d.flush());
    QByteArray out;
    QVERIFY(d.readSectors(0, 1, out));
    // A transient failure: only the n-th write fails.
    d.failAtWrite(1);
    d.setStaysFailed(false);
    QVERIFY(!d.writeSectors(3, QByteArray(512, 'd')));
    QVERIFY(d.writeSectors(3, QByteArray(512, 'e')));
    QVERIFY(d.flush());
    QCOMPARE(fileBytes(p).mid(3 * 512, 512), QByteArray(512, 'e'));
}

void TestDevice::faultDeviceTornWrite()
{
    const QString p = makeImage(m_dir, 512, 8);
    FileSectorDevice file(p, 512, FileSectorDevice::Mode::ReadWrite);
    QVERIFY(file.open());
    FaultSectorDevice d(file);
    d.failAtWrite(1);
    d.setTornWrite(true);
    QVERIFY(!d.writeSectors(2, QByteArray(4 * 512, 'x')));
    QVERIFY(file.flush());
    const QByteArray bytes = fileBytes(p);
    // Two of the four sectors reached the disk.
    QCOMPARE(bytes.mid(2 * 512, 1024), QByteArray(1024, 'x'));
    QCOMPARE(bytes.mid(4 * 512, 512), QByteArray(512, char(4)));
    QCOMPARE(d.writeLog().size(), 1);
    QCOMPARE(d.writeLog().at(0).count, qint64(2));

    // A single sector is never cut.
    FaultSectorDevice single(file);
    single.failAtWrite(1);
    single.setTornWrite(true);
    QVERIFY(!single.writeSectors(6, QByteArray(512, 'y')));
    QVERIFY(file.flush());
    QCOMPARE(fileBytes(p).mid(6 * 512, 512), QByteArray(512, char(6)));
}

void TestDevice::faultDeviceRealDiskFlag()
{
    const QString p = makeImage(m_dir, 512, 2);
    FileSectorDevice file(p, 512);
    QVERIFY(file.open());
    FaultSectorDevice d(file);
    QVERIFY(!d.isRealDisk());
    d.setRealDisk(true);
    QVERIFY(d.isRealDisk());
    QVERIFY(!file.isRealDisk());
}

QTEST_GUILESS_MAIN(TestDevice)
#include "tst_installer_device.moc"
