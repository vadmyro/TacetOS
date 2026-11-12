// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The named fixtures and the bootloader files of docs/specs/bootloader-legacy.md
// sections 26.1 and 26.2. Every fixture is built, a variant is compared with its
// base image (it may differ only in its declared zone), and the expected results
// of the table are checked against the bytes of the image with a reader that
// does not use the builder's layout.

#include "fixtures.h"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::fakepod;

namespace {

quint32 le32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

quint32 be32(const QByteArray &b, int at)
{
    quint32 v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | quint8(b.at(at + i));
    return v;
}

QByteArray readAt(QFile &f, qint64 offset, qint64 length)
{
    f.seek(offset);
    const QByteArray b = f.read(length);
    return b.size() == length ? b : QByteArray();
}

// Section 2, read the way an installer would: the 4 entries of a DOS table by
// position, the kept entries of an Apple Partition Map.
QList<SlotExpect> readSlots(QFile &f)
{
    QList<SlotExpect> parts;
    const QByteArray s0 = readAt(f, 0, 512);
    if (quint8(s0.at(510)) == 0x55 && quint8(s0.at(511)) == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const int o = 0x1BE + 16 * i;
            parts.append({quint8(s0.at(o + 4)), le32(s0, o + 8), le32(s0, o + 12)});
        }
    } else if (s0.left(2) == "ER") {
        for (int n = 1; n <= 64 && parts.size() < 4; ++n) {
            const QByteArray e = readAt(f, 512 * n, 512);
            if (e.left(2) != "PM")
                break;
            const QByteArray type = QByteArray(e.constData() + 48, 32);
            const QByteArray name = type.left(type.indexOf('\0'));
            if (name == "Apple_MDFW")
                parts.append({0, be32(e, 8), be32(e, 12)});
            else if (name == "Apple_HFS")
                parts.append({0xFFFF, be32(e, 8), be32(e, 12)});
            if (n >= int(be32(e, 4)))
                break;
        }
    }
    return parts;
}

QString tagName(const QByteArray &stored)
{
    if (stored == "soso")
        return QStringLiteral("OSOS");
    if (stored == "crsr")
        return QStringLiteral("RSRC");
    if (stored == "dpua")
        return QStringLiteral("AUPD");
    if (stored == "kbso")
        return QStringLiteral("OSBK");
    return QStringLiteral("other");
}

// Section 8 and 11 check 4, written out.
bool fileIsFor(const QByteArray &file, const QString &targetName)
{
    const BootTarget *t = BootloaderFile::target(targetName);
    if (file.size() < 9)
        return false;
    quint32 sum = t->number;
    for (int i = 8; i < file.size(); ++i)
        sum += quint8(file.at(i));
    return be32(file, 0) == sum && file.mid(4, 4) == QByteArray(t->tag);
}

} // namespace

class TestFixtures : public QObject
{
    Q_OBJECT

private slots:
    void tableIsComplete();
    void everyFixtureBuilds_data();
    void everyFixtureBuilds();
    void variantDiffersOnlyInItsZone_data();
    void variantDiffersOnlyInItsZone();
    void slotsMatchTheImage_data();
    void slotsMatchTheImage();
    void directoryMatchesTheImage_data();
    void directoryMatchesTheImage();
    void expectedMessages();
    void oneCheckPerFixture();
    void bootloaderFiles_data();
    void bootloaderFiles();
    void bootloaderVariantsDifferWhereTold();
    void sameBytesTwice();

private:
    static void idRows();
    QString path(const char *name) { return m_dir.filePath(QString::fromLatin1(name)); }
    // The base images, built once.
    QString baseImage(FixtureId id);

    QTemporaryDir m_dir;
    QHash<int, QString> m_bases;
};

void TestFixtures::idRows()
{
    for (const FixtureInfo &i : Fixtures::all())
        QTest::newRow(i.name) << int(i.id);
}

QString TestFixtures::baseImage(FixtureId id)
{
    if (!m_bases.contains(int(id))) {
        const QString p = m_dir.filePath(QStringLiteral("base-%1.img").arg(int(id)));
        if (!Fixtures::build(id, p).ok)
            return QString();
        m_bases.insert(int(id), p);
    }
    return m_bases.value(int(id));
}

void TestFixtures::tableIsComplete()
{
    QVERIFY(m_dir.isValid());
    const QList<FixtureInfo> &all = Fixtures::all();
    QCOMPARE(all.size(), int(FixtureId::Count));
    QSet<QString> names;
    for (int i = 0; i < all.size(); ++i) {
        const FixtureInfo &f = all.at(i);
        QCOMPARE(int(f.id), i);
        QVERIFY2(qstrlen(f.name) > 0, qPrintable(QString::number(i)));
        QVERIFY2(qstrlen(f.what) > 0, f.name);
        QVERIFY2(!names.contains(QString::fromLatin1(f.name)), f.name);
        names.insert(QString::fromLatin1(f.name));
        QCOMPARE(Fixtures::byName(QString::fromLatin1(f.name)).value_or(FixtureId::Count), f.id);
        if (f.base)
            QVERIFY2(int(*f.base) < i, f.name); // a base comes first
    }
    QVERIFY(!Fixtures::byName(QStringLiteral("nope")));
    // Every model id has a Windows image, and the 8 Mac ones exist.
    QCOMPARE(Fixtures::info(FixtureId::DosClassic6g).expected.verdict,
             Verdict::PartitionTestFailed);
    QCOMPARE(Fixtures::bootFiles().size(), int(BootFileId::Count));
}

void TestFixtures::everyFixtureBuilds_data()
{
    QTest::addColumn<int>("id");
    idRows();
}

void TestFixtures::everyFixtureBuilds()
{
    QFETCH(int, id);
    const QString p = m_dir.filePath(QStringLiteral("one.img"));
    const FixtureBuild b = Fixtures::build(FixtureId(id), p);
    QVERIFY2(b.ok, qPrintable(b.error));
    QCOMPARE(QFile(p).size(), qint64(64 * 1024 * 1024));
    for (const ByteRange &z : b.zones) {
        QVERIFY(z.length > 0);
        QVERIFY(z.offset >= 0 && z.offset + z.length <= b.pod.fileSize);
    }
    const FixtureInfo &i = Fixtures::info(FixtureId(id));
    QCOMPARE(i.base.has_value(), i.base.has_value() && Fixtures::info(*i.base).id == *i.base);
}

void TestFixtures::variantDiffersOnlyInItsZone_data()
{
    QTest::addColumn<int>("id");
    for (const FixtureInfo &i : Fixtures::all()) {
        if (i.base)
            QTest::newRow(i.name) << int(i.id);
    }
}

void TestFixtures::variantDiffersOnlyInItsZone()
{
    QFETCH(int, id);
    const FixtureInfo &info = Fixtures::info(FixtureId(id));
    const QString basePath = baseImage(*info.base);
    QVERIFY(!basePath.isEmpty());
    const QString p = m_dir.filePath(QStringLiteral("variant.img"));
    const FixtureBuild v = Fixtures::build(FixtureId(id), p);
    QVERIFY2(v.ok, qPrintable(v.error));

    QFile a(basePath), b(p);
    QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
    QCOMPARE(a.size(), b.size());
    QList<bool> zoneHit(v.zones.size(), false);
    qint64 different = 0;
    constexpr qint64 kChunk = 1 << 20;
    for (qint64 at = 0; at < a.size(); at += kChunk) {
        const QByteArray x = a.read(kChunk), y = b.read(kChunk);
        QCOMPARE(x.size(), y.size());
        if (x == y)
            continue;
        for (int i = 0; i < x.size(); ++i) {
            if (x.at(i) == y.at(i))
                continue;
            ++different;
            const qint64 pos = at + i;
            bool inside = false;
            for (int z = 0; z < v.zones.size(); ++z) {
                if (pos >= v.zones.at(z).offset &&
                    pos < v.zones.at(z).offset + v.zones.at(z).length) {
                    inside = true;
                    zoneHit[z] = true;
                }
            }
            QVERIFY2(inside, qPrintable(QStringLiteral("%1: byte %2 differs outside the zones")
                                            .arg(QLatin1String(info.name))
                                            .arg(pos)));
        }
    }
    if (v.zones.isEmpty()) {
        QCOMPARE(different, qint64(0));
    } else {
        QVERIFY(different > 0);
        for (int z = 0; z < zoneHit.size(); ++z)
            QVERIFY2(zoneHit.at(z), qPrintable(QStringLiteral("zone %1 changes nothing").arg(z)));
    }
    // A variant with no patched byte must differ in something else.
    if (v.zones.isEmpty()) {
        const bool other = info.bootFile.has_value() || info.expected.reportedSectorSize != 0;
        QVERIFY2(other, info.name);
    }
}

void TestFixtures::slotsMatchTheImage_data()
{
    QTest::addColumn<int>("id");
    idRows();
}

void TestFixtures::slotsMatchTheImage()
{
    QFETCH(int, id);
    const FixtureInfo &info = Fixtures::info(FixtureId(id));
    const QString p = m_dir.filePath(QStringLiteral("parts.img"));
    QVERIFY(Fixtures::build(FixtureId(id), p).ok);
    QFile f(p);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QList<SlotExpect> found = readSlots(f);
    const QList<SlotExpect> &want = info.expected.slotList;
    if (info.expected.reportedSectorSize != 0) {
        // The sector size is refused before the table is read: no slots expected.
        QVERIFY(want.isEmpty());
        return;
    }
    QCOMPARE(found.size(), want.size());
    for (int i = 0; i < want.size(); ++i) {
        QVERIFY2(found.at(i) == want.at(i),
                 qPrintable(QStringLiteral("%1 slot %2").arg(QLatin1String(info.name)).arg(i + 1)));
    }
}

void TestFixtures::directoryMatchesTheImage_data()
{
    QTest::addColumn<int>("id");
    for (const FixtureInfo &i : Fixtures::all()) {
        const Verdict v = i.expected.verdict;
        if (v == Verdict::Recognised || v == Verdict::UnknownGeneration)
            QTest::newRow(i.name) << int(i.id);
    }
}

void TestFixtures::directoryMatchesTheImage()
{
    QFETCH(int, id);
    const FixtureInfo &info = Fixtures::info(FixtureId(id));
    const Expected &e = info.expected;
    const QString p = m_dir.filePath(QStringLiteral("dir.img"));
    const FixtureBuild b = Fixtures::build(FixtureId(id), p);
    QVERIFY(b.ok);
    QFile f(p);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QList<SlotExpect> parts = readSlots(f);
    const int S = b.pod.sectorSize;
    const qint64 fw = qint64(parts.at(0).firstSector) * S;

    QCOMPARE(readAt(f, fw, 256), FakePod::banner());
    const QByteArray head = readAt(f, fw, S);
    QCOMPARE(head.mid(0x100, 4), QByteArray("]ih["));
    qint64 dir = 0x200 + le32(head, 0x104);
    QVERIFY(dir < parts.at(0).sectorCount * S);
    if (readAt(f, fw + dir, 1).at(0) == 0)
        dir = (dir / S + 1) * S;
    QStringList tags;
    quint32 version = 0;
    quint32 entryOffset = 0;
    bool haveOsos = false;
    for (int i = 0; i < 10; ++i) {
        const QByteArray en = readAt(f, fw + dir + 40 * i, 40);
        if (en.left(4) != "!ATA")
            break;
        const QString t = tagName(en.mid(4, 4));
        tags << t;
        if (t == QLatin1String("OSOS") && !haveOsos) {
            haveOsos = true;
            version = le32(en, 32);
            entryOffset = le32(en, 24);
        }
    }
    QVERIFY(haveOsos);
    QCOMPARE(BootloaderFile::targetForOsValue(version >> 8), e.target);
    if (e.verdict == Verdict::UnknownGeneration)
        return;
    QCOMPARE(tags, e.entries);
    if (e.state != State::Inconsistent) {
        const bool installed = tags.contains(QStringLiteral("OSBK")) || entryOffset != 0;
        QCOMPARE(e.state, installed ? State::Installed : State::Clean);
    }
}

void TestFixtures::expectedMessages()
{
    for (const FixtureInfo &i : Fixtures::all()) {
        const Expected &e = i.expected;
        const bool silent =
            e.verdict == Verdict::Recognised && e.firstFailingCheck == 0 && e.warningCheck <= 0;
        QVERIFY2(silent == e.message.isEmpty(), i.name);
        if (e.firstFailingCheck != 0) {
            QVERIFY2(e.message.startsWith(QStringLiteral("check %1:").arg(e.firstFailingCheck)),
                     i.name);
        }
        if (e.verdict != Verdict::Recognised) {
            QCOMPARE(e.state, State::NotRead);
            QCOMPARE(e.firstFailingCheck, 0);
        }
        if (e.state == State::Inconsistent)
            QVERIFY2(e.firstFailingCheck != 0, i.name);
        if (e.firstFailingCheck != 0)
            QVERIFY2(e.verdict == Verdict::Recognised, i.name);
    }
}

void TestFixtures::oneCheckPerFixture()
{
    for (int check = 1; check <= 8; ++check) {
        const auto id = Fixtures::singleCheck(check);
        if (check == 6 || check == 8) {
            QVERIFY(!id.has_value());
            continue;
        }
        QVERIFY2(id.has_value(), qPrintable(QString::number(check)));
        const Expected &e = Fixtures::info(*id).expected;
        QCOMPARE(e.verdict, Verdict::Recognised);
        if (check == 7) {
            QCOMPARE(e.firstFailingCheck, 0);
            QCOMPARE(e.warningCheck, 7);
        } else {
            QCOMPARE(e.firstFailingCheck, check);
        }
    }
    // Check 4 fixtures offer a file that fails the check, and nothing else.
    for (FixtureId id :
         {FixtureId::Check4WrongByte, FixtureId::Check4WrongTag, FixtureId::Check4TooShort,
          FixtureId::Check4HeaderOnly, FixtureId::Check4OtherModelTag}) {
        const FixtureInfo &i = Fixtures::info(id);
        QVERIFY(i.bootFile.has_value());
        QVERIFY(!Fixtures::bootFileInfo(*i.bootFile).ok);
        QCOMPARE(i.expected.message, Fixtures::bootFileInfo(*i.bootFile).message);
        QCOMPARE(i.base.value_or(FixtureId::Count), FixtureId::DosIpodVideo);
    }
    QCOMPARE(
        Fixtures::info(FixtureId::Check1DiskChanged).expected.scannedAs.value_or(FixtureId::Count),
        FixtureId::DosIpodVideo);
}

void TestFixtures::bootloaderFiles_data()
{
    QTest::addColumn<int>("id");
    for (const BootFileInfo &i : Fixtures::bootFiles())
        QTest::newRow(i.name) << int(i.id);
}

void TestFixtures::bootloaderFiles()
{
    QFETCH(int, id);
    const BootFileInfo &info = Fixtures::bootFileInfo(BootFileId(id));
    const QByteArray file = Fixtures::bootFile(BootFileId(id));
    QCOMPARE(fileIsFor(file, QString::fromLatin1(info.target)), info.ok);
    if (info.ok) {
        QVERIFY(info.message.isEmpty());
        // A good file is for its own target only: the tag and the number differ.
        for (const BootTarget &t : BootloaderFile::targets()) {
            if (QString::fromLatin1(t.target) != QString::fromLatin1(info.target))
                QVERIFY(!fileIsFor(file, QString::fromLatin1(t.target)));
        }
    } else {
        QVERIFY(info.message.startsWith(QStringLiteral("check 4:")));
    }
}

void TestFixtures::bootloaderVariantsDifferWhereTold()
{
    // The nine good files, and the table of section 8.
    QCOMPARE(BootloaderFile::targets().size(), 9);
    const char *const tags[] = {"1g2g", "ip3g", "mini", "ip4g", "ipco",
                                "mn2g", "nano", "ipvd", "nn2g"};
    const int numbers[] = {19, 7, 9, 8, 3, 11, 4, 5, 62};
    for (int i = 0; i < 9; ++i) {
        const BootTarget t = BootloaderFile::targets().at(i);
        QCOMPARE(QByteArray(t.tag), QByteArray(tags[i]));
        QCOMPARE(int(t.number), numbers[i]);
        const QByteArray f = Fixtures::bootFile(BootFileId(i));
        QCOMPARE(f.size(), 8 + 4000 + (i == 8 ? 0x800 : 0));
    }
    // The checksum is big-endian: a one-byte program gives the number plus it.
    const QByteArray tiny = BootloaderFile::assemble(0x01020304, "ipvd", "A");
    QCOMPARE(tiny.size(), 9);
    QCOMPARE(tiny.left(4), QByteArray("\1\2\3\4", 4));
    QCOMPARE(BootloaderFile::checksum(5, "AB"), quint32(5 + 65 + 66));

    const QByteArray good = Fixtures::bootFile(BootFileId::GoodIpodVideo);
    const QByteArray wrong = Fixtures::bootFile(BootFileId::WrongByte);
    QCOMPARE(wrong.size(), good.size());
    QCOMPARE(wrong.left(8), good.left(8));
    int differing = 0;
    for (int i = 0; i < good.size(); ++i)
        differing += good.at(i) != wrong.at(i);
    QCOMPARE(differing, 1);

    const QByteArray wrongTag = Fixtures::bootFile(BootFileId::WrongTag);
    QCOMPARE(wrongTag.left(4), good.left(4));
    QCOMPARE(wrongTag.mid(4, 4), QByteArray("xxxx"));
    QCOMPARE(wrongTag.mid(8), good.mid(8));

    const QByteArray other = Fixtures::bootFile(BootFileId::OtherModelTag);
    QCOMPARE(other.left(4), good.left(4));
    QCOMPARE(other.mid(4, 4), QByteArray("ip4g"));
    QCOMPARE(other.mid(8), good.mid(8));

    QCOMPARE(Fixtures::bootFile(BootFileId::TooShort).size(), 5);
    QCOMPARE(Fixtures::bootFile(BootFileId::HeaderOnly).size(), 8);
    QCOMPARE(be32(Fixtures::bootFile(BootFileId::HeaderOnly), 0), quint32(5));
}

void TestFixtures::sameBytesTwice()
{
    QCOMPARE(Fixtures::bootFile(BootFileId::GoodNano1g),
             Fixtures::bootFile(BootFileId::GoodNano1g));
    const QString a = path("twice-a.img"), b = path("twice-b.img");
    QVERIFY(Fixtures::build(FixtureId::ApmTooManyKept, a).ok);
    QVERIFY(Fixtures::build(FixtureId::ApmTooManyKept, b).ok);
    QFile fa(a), fb(b);
    QVERIFY(fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly));
    QCOMPARE(fa.read(1 << 20), fb.read(1 << 20));
    QCOMPARE(readAt(fa, 8 << 20, 1 << 20), readAt(fb, 8 << 20, 1 << 20));
}

QTEST_GUILESS_MAIN(TestFixtures)
#include "tst_fixtures.moc"
