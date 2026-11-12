// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The firmware partition in read only mode (docs/specs/bootloader-legacy.md
// sections 7, 9 and 10), on the fixtures of P4.1 (section 26.2, the list of
// entries and the state), and on a few images patched here.

#include "constants.h"
#include "faultdevice.h"
#include "firmware.h"
#include "fixtures.h"
#include "recognise.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const char *name(State s)
{
    switch (s) {
    case State::NotRead:
        return "NotRead";
    case State::Clean:
        return "Clean";
    case State::Installed:
        return "Installed";
    case State::Inconsistent:
        return "Inconsistent";
    }
    return "?";
}

const char *name(fp::State s)
{
    switch (s) {
    case fp::State::NotRead:
        return "NotRead";
    case fp::State::Clean:
        return "Clean";
    case fp::State::Installed:
        return "Installed";
    case fp::State::Inconsistent:
        return "Inconsistent";
    }
    return "?";
}

QByteArray le32(quint32 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i)
        b[i] = char(v >> (8 * i));
    return b;
}

quint32 byteSum(const QByteArray &b)
{
    quint32 sum = 0;
    for (char c : b)
        sum += quint8(c);
    return sum;
}

struct Patch
{
    qint64 offset;
    QByteArray bytes;
};

} // namespace

class TestFirmware : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); }

    void entryKindNames();
    void sizesAndAlignment();
    void writeDataLimit();

    void fixturesGiveTheirEntriesAndState_data();
    void fixturesGiveTheirEntriesAndState();
    void entryFieldsAndImageBytes_data();
    void entryFieldsAndImageBytes();
    void readingNeverWrites_data();
    void readingNeverWrites();

    void dnanTagIsAccepted();
    void listEndsAtAnotherTag();
    void listStopsAtTenEntries();
    void hibeIsKnown();
    void imageAreaOfThe3rdGeneration();
    void directoryMustFitInOneSector();
    void orderOfTheEntries();
    void osbkRules();
    void notRecognisedIsNotRead();

private:
    struct Opened
    {
        QString path;
        std::unique_ptr<FileSectorDevice> dev;
        fp::FixtureBuild build;
        Recognition rec;
        FirmwareState fw;
    };
    // Builds the fixture, applies `patchList` to the file, opens it read-only and
    // reads the firmware. `patchList` gets the build to compute offsets.
    template <typename F> Opened open(fp::FixtureId id, F makePatches)
    {
        Opened o;
        o.path = m_dir.filePath(QStringLiteral("fixture.img"));
        QFile::remove(o.path);
        o.build = fp::Fixtures::build(id, o.path);
        if (!o.build.ok)
            return o;
        QFile f(o.path);
        if (!f.open(QIODevice::ReadWrite))
            return o;
        const QList<Patch> patchList = makePatches(f, o.build.pod);
        for (const Patch &p : patchList) {
            f.seek(p.offset);
            f.write(p.bytes);
        }
        f.close();
        const int reported = fp::Fixtures::info(id).expected.reportedSectorSize;
        o.dev.reset(new FileSectorDevice(o.path, reported ? reported : o.build.pod.sectorSize));
        if (!o.dev->open()) {
            o.dev.reset();
            return o;
        }
        o.rec = recognise(*o.dev);
        o.fw = readFirmware(*o.dev, o.rec);
        return o;
    }
    Opened open(fp::FixtureId id)
    {
        return open(id, [](QFile &, const fp::FakePodResult &) { return QList<Patch>(); });
    }
    static qint64 entryAt(const fp::FakePodResult &r, int i, int field = 0)
    {
        return r.firmwareStart + r.directoryOffset + 40 * i + field;
    }
    static quint32 readLe32(QFile &f, qint64 at)
    {
        f.seek(at);
        const QByteArray b = f.read(4);
        return quint32(quint8(b[0])) | quint32(quint8(b[1])) << 8 | quint32(quint8(b[2])) << 16 |
               quint32(quint8(b[3])) << 24;
    }

    QTemporaryDir m_dir;
};

void TestFirmware::entryKindNames()
{
    QCOMPARE(entryKindName(EntryKind::Osos), QStringLiteral("OSOS"));
    QCOMPARE(entryKindName(EntryKind::Rsrc), QStringLiteral("RSRC"));
    QCOMPARE(entryKindName(EntryKind::Aupd), QStringLiteral("AUPD"));
    QCOMPARE(entryKindName(EntryKind::Osbk), QStringLiteral("OSBK"));
    QCOMPARE(entryKindName(EntryKind::Hibe), QStringLiteral("HIBE"));
    QCOMPARE(entryKindName(EntryKind::Other), QStringLiteral("other"));
}

void TestFirmware::sizesAndAlignment()
{
    // Section 9: the padded size is the length rounded up to a whole sector.
    QCOMPARE(roundUpToSector(0, 512), qint64(0));
    QCOMPARE(roundUpToSector(1, 512), qint64(512));
    QCOMPARE(roundUpToSector(512, 512), qint64(512));
    QCOMPARE(roundUpToSector(513, 512), qint64(1024));
    QCOMPARE(roundUpToSector(2049, 2048), qint64(4096));
    // Past 4 GiB: the entry length is 32 bits, the arithmetic is not.
    QCOMPARE(roundUpToSector(qint64(0xFFFFFFFF), 4096), qint64(0x100000000LL));
    DirectoryEntry e;
    e.length = 4000;
    QCOMPARE(e.paddedBytes(512), qint64(4096));
    QCOMPARE(e.paddedBytes(2048), qint64(4096));
    e.length = 0xFFFFFFFF;
    QCOMPARE(e.paddedBytes(512), qint64(0x100000000LL));
}

void TestFirmware::writeDataLimit()
{
    const qint64 mib = 1024 * 1024;
    QVERIFY(writeDataFits(7 * mib, mib, 512));
    QVERIFY(!writeDataFits(7 * mib, mib + 1, 512));
    // The padding counts: 7 MiB + 1 byte is padded to 7 MiB + 512.
    QVERIFY(writeDataFits(7 * mib + 1, mib - 512, 512));
    QVERIFY(!writeDataFits(7 * mib + 1, mib - 511, 512));
    QVERIFY(!writeDataFits(-1, 10, 512));
    QVERIFY(!writeDataFits(10, 10, 0));
}

void TestFirmware::fixturesGiveTheirEntriesAndState_data()
{
    QTest::addColumn<int>("id");
    for (const fp::FixtureInfo &i : fp::Fixtures::all())
        QTest::newRow(i.name) << int(i.id);
}

void TestFirmware::fixturesGiveTheirEntriesAndState()
{
    QFETCH(int, id);
    const fp::FixtureInfo &info = fp::Fixtures::info(fp::FixtureId(id));
    const fp::Expected &want = info.expected;
    Opened o = open(fp::FixtureId(id));
    QVERIFY2(o.build.ok, qPrintable(o.build.error));
    QVERIFY(o.dev);

    if (want.verdict != fp::Verdict::Recognised) {
        QCOMPARE(int(o.fw.state), int(State::NotRead));
        QVERIFY(o.fw.entryList.isEmpty());
        QCOMPARE(o.fw.message, o.rec.message);
        return;
    }

    QStringList entries;
    for (const DirectoryEntry &e : o.fw.entryList)
        entries << entryKindName(e.kind);
    QCOMPARE(entries, want.entries);
    QCOMPARE(QLatin1String(name(o.fw.state)), QLatin1String(name(want.state)));

    if (want.state == fp::State::Inconsistent) {
        QCOMPARE(o.fw.failingCheck, want.firstFailingCheck);
        QCOMPARE(o.fw.message, want.message);
    } else {
        // Checks 1 and 4 to 7 are not about the entries: the state says nothing.
        QCOMPARE(o.fw.failingCheck, 0);
        QVERIFY(o.fw.message.isEmpty());
    }

    // The partition and the directory, as the builder of the image placed them.
    QCOMPARE(o.fw.firmwareBytes, o.build.pod.firmwareBytes);
    QCOMPARE(o.fw.directoryStart, o.build.pod.directoryOffset);
    QCOMPARE(o.fw.imageArea, o.build.pod.imageArea);
    QCOMPARE(o.fw.sectorSize, o.build.pod.sectorSize);
    QCOMPARE(o.fw.target, want.target);
    if (!want.entries.isEmpty()) {
        QVERIFY(o.fw.osIndex >= 0);
        QCOMPARE(o.fw.entryList.at(o.fw.osIndex).kind, EntryKind::Osos);
        QCOMPARE(o.fw.osbkIndex >= 0, want.entries.contains(QStringLiteral("OSBK")));
    }
}

void TestFirmware::entryFieldsAndImageBytes_data()
{
    QTest::addColumn<int>("id");
    // The images built from scratch only: nothing is patched, so the checksum of
    // every entry is the sum of its bytes (section 7.2).
    for (const fp::FixtureInfo &i : fp::Fixtures::all()) {
        if (!i.base && i.expected.verdict == fp::Verdict::Recognised)
            QTest::newRow(i.name) << int(i.id);
    }
}

void TestFirmware::entryFieldsAndImageBytes()
{
    QFETCH(int, id);
    Opened o = open(fp::FixtureId(id));
    QVERIFY(o.dev);
    QVERIFY(o.fw.state == State::Clean || o.fw.state == State::Installed);
    for (const DirectoryEntry &e : std::as_const(o.fw.entryList)) {
        QCOMPARE(e.deviceTag, QByteArray("!ATA"));
        QByteArray image;
        const IoResult io =
            readBytes(*o.dev, o.rec.firmwareStart + o.fw.imageStart(e), e.length, image);
        QVERIFY2(io, qPrintable(io.error));
        QCOMPARE(byteSum(image), e.checksum);
    }
    const DirectoryEntry &os = o.fw.entryList.at(o.fw.osIndex);
    QCOMPARE(os.version >> 8, o.rec.modelValue);
    if (o.fw.isNano2g()) {
        // Section 7.5: the bootloader is the last image, Apple's entry has no offset.
        QCOMPARE(os.entryOffset, quint32(0));
    } else if (o.fw.state == State::Installed) {
        // Section 7.4: a multiple of the sector size, inside the image.
        QVERIFY(os.entryOffset != 0);
        QCOMPARE(os.entryOffset % o.fw.sectorSize, quint32(0));
        QVERIFY(os.entryOffset < os.length);
        QCOMPARE(os.flashAddress, kFlashAddressUnused);
    } else {
        QCOMPARE(os.entryOffset, quint32(0));
    }
}

void TestFirmware::readingNeverWrites_data()
{
    QTest::addColumn<int>("id");
    QTest::newRow("clean") << int(fp::FixtureId::DosIpodVideo);
    QTest::newRow("installed") << int(fp::FixtureId::DosIpodVideoInstalled);
    QTest::newRow("nano2g") << int(fp::FixtureId::DosNano2gInstalled);
    QTest::newRow("inconsistent") << int(fp::FixtureId::DosOverlap);
}

void TestFirmware::readingNeverWrites()
{
    QFETCH(int, id);
    Opened o = open(fp::FixtureId(id));
    QVERIFY(o.dev);
    const QByteArray before =
        QCryptographicHash::hash(fileBytes(o.path), QCryptographicHash::Sha256);
    FaultSectorDevice watched(*o.dev);
    const Recognition r = recognise(watched);
    const FirmwareState s = readFirmware(watched, r);
    QVERIFY(s.state != State::NotRead);
    QCOMPARE(watched.writeCount(), 0);
    QCOMPARE(watched.flushCount(), 0);
    QCOMPARE(QCryptographicHash::hash(fileBytes(o.path), QCryptographicHash::Sha256), before);
}

void TestFirmware::dnanTagIsAccepted()
{
    Opened o = open(fp::FixtureId::DosVideoRsrc, [](QFile &, const fp::FakePodResult &r) {
        return QList<Patch>{{entryAt(r, 0), "DNAN"}, {entryAt(r, 1), "DNAN"}};
    });
    QVERIFY(o.dev);
    QCOMPARE(o.fw.entryList.size(), 2);
    QCOMPARE(o.fw.entryList.at(0).deviceTag, QByteArray("DNAN"));
    QCOMPARE(int(o.fw.state), int(State::Clean));
}

void TestFirmware::listEndsAtAnotherTag()
{
    // Entry 2 does not start with a device tag: the list is the first two.
    Opened o = open(fp::FixtureId::DosVideoRsrcAupd, [](QFile &, const fp::FakePodResult &r) {
        return QList<Patch>{{entryAt(r, 2), "XXXX"}};
    });
    QVERIFY(o.dev);
    QCOMPARE(o.fw.entryList.size(), 2);
    QCOMPARE(int(o.fw.state), int(State::Clean));
}

void TestFirmware::listStopsAtTenEntries()
{
    // Twelve entries that all carry a device tag: ten are read.
    Opened o = open(fp::FixtureId::DosIpodVideo, [](QFile &f, const fp::FakePodResult &r) {
        f.seek(entryAt(r, 0));
        const QByteArray first = f.read(40);
        QList<Patch> p;
        for (int i = 1; i < 12; ++i)
            p.append({entryAt(r, i), first});
        return p;
    });
    QVERIFY(o.dev);
    QCOMPARE(o.fw.entryList.size(), 10);
    // The state is refused anyway: ten OSOS entries.
    QCOMPARE(int(o.fw.state), int(State::Inconsistent));
}

void TestFirmware::hibeIsKnown()
{
    Opened o = open(fp::FixtureId::DosVideoRsrc, [](QFile &, const fp::FakePodResult &r) {
        return QList<Patch>{{entryAt(r, 1, 4), "ebih"}};
    });
    QVERIFY(o.dev);
    QCOMPARE(o.fw.entryList.size(), 2);
    QCOMPARE(entryKindName(o.fw.entryList.at(1).kind), QStringLiteral("HIBE"));
    QCOMPARE(int(o.fw.state), int(State::Clean));
}

void TestFirmware::imageAreaOfThe3rdGeneration()
{
    // One image: the area is one sector after the start.
    Opened one = open(fp::FixtureId::DosIpod3g);
    QVERIFY(one.dev);
    QCOMPARE(int(one.fw.formatVersion), 2);
    QCOMPARE(one.fw.imageArea, qint64(one.fw.sectorSize));
    // More than one image: the area is the start of the partition (section 7.3).
    Opened two = open(fp::FixtureId::DosIpod3g, [](QFile &f, const fp::FakePodResult &r) {
        f.seek(entryAt(r, 0));
        QByteArray e = f.read(40);
        e.replace(4, 4, "dpua");
        return QList<Patch>{{entryAt(r, 1), e}};
    });
    QVERIFY(two.dev);
    QCOMPARE(two.fw.entryList.size(), 2);
    QCOMPARE(two.fw.imageArea, qint64(0));
    QCOMPARE(two.fw.imageStart(two.fw.entryList.at(1)), qint64(two.fw.entryList.at(1).devOffset));
    // The 4th generation, format version 3, keeps the sector with two images.
    Opened video = open(fp::FixtureId::DosVideoRsrc);
    QCOMPARE(video.fw.imageArea, qint64(video.fw.sectorSize));
}

void TestFirmware::directoryMustFitInOneSector()
{
    // The directory starts 16 bytes before the end of a 512-byte sector: a single
    // entry does not fit (section 11 check 2).
    Opened o = open(fp::FixtureId::DosIpodVideo, [](QFile &f, const fp::FakePodResult &r) {
        f.seek(entryAt(r, 0));
        const QByteArray e = f.read(40);
        return QList<Patch>{{r.firmwareStart + 0x104, le32(0x1F0)},
                            {r.firmwareStart + 0x200 + 0x1F0, e}};
    });
    QVERIFY(o.dev);
    QCOMPARE(o.rec.directoryStart, qint64(0x200 + 0x1F0));
    QCOMPARE(int(o.fw.state), int(State::Inconsistent));
    QCOMPARE(o.fw.failingCheck, 2);
    QCOMPARE(o.fw.message, QStringLiteral("check 2: the directory does not fit in one sector"));
}

void TestFirmware::orderOfTheEntries()
{
    // The third entry starts before the second one: not increasing (check 3).
    Opened o = open(fp::FixtureId::DosVideoRsrcAupd, [](QFile &f, const fp::FakePodResult &r) {
        const quint32 second = readLe32(f, entryAt(r, 1, 12));
        return QList<Patch>{{entryAt(r, 2, 12), le32(second - 512)}};
    });
    QVERIFY(o.dev);
    QCOMPARE(int(o.fw.state), int(State::Inconsistent));
    QCOMPARE(o.fw.failingCheck, 3);
    QCOMPARE(o.fw.message, QStringLiteral("check 3: the entries are not in increasing order"));
}

void TestFirmware::osbkRules()
{
    // An OSBK entry on a model that does not use one.
    Opened video = open(fp::FixtureId::DosVideoRsrc, [](QFile &, const fp::FakePodResult &r) {
        return QList<Patch>{{entryAt(r, 1, 4), "kbso"}};
    });
    QVERIFY(video.dev);
    QCOMPARE(video.fw.message,
             QStringLiteral("check 2: the directory holds an OSBK entry on a model that does not "
                            "use one"));

    // 2nd generation nano: two OSBK entries, and the order swapped.
    Opened two = open(fp::FixtureId::DosNano2gInstalled, [](QFile &f, const fp::FakePodResult &r) {
        f.seek(entryAt(r, 0));
        return QList<Patch>{{entryAt(r, 2), f.read(40)}};
    });
    QVERIFY(two.dev);
    QCOMPARE(two.fw.message,
             QStringLiteral("check 2: the directory holds more than one OSBK entry"));
    Opened swapped =
        open(fp::FixtureId::DosNano2gInstalled, [](QFile &, const fp::FakePodResult &r) {
            return QList<Patch>{{entryAt(r, 0, 4), "soso"}, {entryAt(r, 1, 4), "kbso"}};
        });
    QVERIFY(swapped.dev);
    QCOMPARE(swapped.fw.message, QStringLiteral("check 2: the OSBK entry is not the first one"));

    // The clean 2nd generation nano is Clean, the installed one is Installed, and
    // the late directory changes nothing.
    QCOMPARE(int(open(fp::FixtureId::DosNano2g).fw.state), int(State::Clean));
    QCOMPARE(int(open(fp::FixtureId::DosNano2gInstalled).fw.state), int(State::Installed));
    QCOMPARE(int(open(fp::FixtureId::DosNano2gInstalledLate).fw.state), int(State::Installed));
    QCOMPARE(open(fp::FixtureId::DosNano2gLateDirectory).fw.directoryStart, qint64(2048));
}

void TestFirmware::notRecognisedIsNotRead()
{
    Opened o = open(fp::FixtureId::DosNoBanner);
    QVERIFY(o.dev);
    QCOMPARE(int(o.fw.state), int(State::NotRead));
    QCOMPARE(o.fw.message, o.rec.message);
    QVERIFY(o.fw.entryList.isEmpty());
    QCOMPARE(o.fw.osIndex, -1);
}

QTEST_GUILESS_MAIN(TestFirmware)
#include "tst_installer_firmware.moc"
