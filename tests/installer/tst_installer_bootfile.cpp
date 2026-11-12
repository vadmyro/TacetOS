// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The bootloader file (docs/specs/bootloader-legacy.md section 8, and check 4 of
// section 11), on the synthetic files of P4.1 and on a few made here.

#include "bootfile.h"
#include "fixtures.h"

#include <QTest>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

class TestBootFile : public QObject
{
    Q_OBJECT

private slots:
    void tableMatchesTheFakeFiles();
    void checksumIsModelPlusByteSum();
    void filesOfTheFixtures_data();
    void filesOfTheFixtures();
    void goodFileIsRefusedForEveryOtherTarget();
    void checkFixturesGiveTheirMessage_data();
    void checkFixturesGiveTheirMessage();
    void headerIsBigEndian();
    void edgeLengths();
    void unknownTarget();
    void programIsReturned();
};

void TestBootFile::tableMatchesTheFakeFiles()
{
    // The fake files were written from the same table by another piece of code.
    const QList<fp::BootTarget> rows = fp::BootloaderFile::targets();
    QCOMPARE(rows.size(), 9);
    for (const fp::BootTarget &r : rows) {
        const BootFileModel *m = bootFileModel(QString::fromLatin1(r.target));
        QVERIFY2(m, r.target);
        QCOMPARE(QByteArray(m->tag), QByteArray(r.tag));
        QCOMPARE(m->number, quint32(r.number));
    }
    QVERIFY(!bootFileModel(QStringLiteral("ipodgray")));
}

void TestBootFile::checksumIsModelPlusByteSum()
{
    QCOMPARE(bootFileChecksum(5, QByteArray()), quint32(5));
    QCOMPARE(bootFileChecksum(5, QByteArray("\x01\x02\xFF", 3)), quint32(5 + 1 + 2 + 255));
    // Unsigned bytes: 0x80 counts 128, not -128.
    QCOMPARE(bootFileChecksum(0, QByteArray(3, char(0x80))), quint32(384));
    // Modulo 2^32.
    const QByteArray big(300, char(0xFF));
    QCOMPARE(bootFileChecksum(0xFFFFFFF0u, big), quint32(0xFFFFFFF0u + 300 * 255u));
    QCOMPARE(bootFileChecksum(3, QByteArray("abc")), quint32(3 + 'a' + 'b' + 'c'));
    for (const fp::BootTarget &r : fp::BootloaderFile::targets()) {
        const QByteArray p = fp::BootloaderFile::program(QString::fromLatin1(r.target));
        QCOMPARE(bootFileChecksum(r.number, p), fp::BootloaderFile::checksum(r.number, p));
    }
}

void TestBootFile::filesOfTheFixtures_data()
{
    QTest::addColumn<int>("id");
    for (const fp::BootFileInfo &i : fp::Fixtures::bootFiles())
        QTest::newRow(i.name) << int(i.id);
}

void TestBootFile::filesOfTheFixtures()
{
    QFETCH(int, id);
    const fp::BootFileInfo &info = fp::Fixtures::bootFileInfo(fp::BootFileId(id));
    const BootFileCheck c =
        checkBootloaderFile(fp::Fixtures::bootFile(info.id), QString::fromLatin1(info.target));
    QCOMPARE(c.ok(), info.ok);
    if (info.ok)
        QVERIFY(c.message.isEmpty());
    else
        QCOMPARE(c.message, info.message);
}

void TestBootFile::goodFileIsRefusedForEveryOtherTarget()
{
    // Section 8: a tag and a number per model make a file for another model
    // impossible to check, even if the checksum alone would pass.
    const QList<fp::BootTarget> rows = fp::BootloaderFile::targets();
    for (const fp::BootTarget &a : rows) {
        const QByteArray file = fp::BootloaderFile::build(QString::fromLatin1(a.target));
        for (const fp::BootTarget &b : rows) {
            const BootFileCheck c = checkBootloaderFile(file, QString::fromLatin1(b.target));
            QCOMPARE(c.ok(), QByteArray(a.target) == QByteArray(b.target));
            if (!c.ok())
                QCOMPARE(int(c.status), int(BootFileStatus::WrongModel));
        }
    }
}

void TestBootFile::checkFixturesGiveTheirMessage_data()
{
    QTest::addColumn<int>("id");
    for (const fp::FixtureInfo &i : fp::Fixtures::all()) {
        if (i.bootFile)
            QTest::newRow(i.name) << int(i.id);
    }
}

void TestBootFile::checkFixturesGiveTheirMessage()
{
    QFETCH(int, id);
    const fp::FixtureInfo &info = fp::Fixtures::info(fp::FixtureId(id));
    QCOMPARE(info.expected.firstFailingCheck, 4);
    const BootFileCheck c =
        checkBootloaderFile(fp::Fixtures::bootFile(*info.bootFile), info.expected.target);
    QVERIFY(!c.ok());
    QCOMPARE(c.message, info.expected.message);
}

void TestBootFile::headerIsBigEndian()
{
    const QString target = QStringLiteral("ipodvideo");
    QByteArray file = fp::BootloaderFile::build(target);
    const quint32 sum = bootFileChecksum(5, file.mid(8));
    QCOMPARE(quint8(file.at(0)), quint8(sum >> 24));
    QCOMPARE(quint8(file.at(3)), quint8(sum));
    const BootFileCheck c = checkBootloaderFile(file, target);
    QVERIFY(c.ok());
    QCOMPARE(c.headerChecksum, sum);
    QCOMPARE(c.tag, QByteArray("ipvd"));
    // The same number written little-endian is a different checksum.
    std::swap(file[0], file[3]);
    std::swap(file[1], file[2]);
    const BootFileCheck bad = checkBootloaderFile(file, target);
    QCOMPARE(int(bad.status), int(BootFileStatus::BadChecksum));
}

void TestBootFile::edgeLengths()
{
    const QString target = QStringLiteral("ipodvideo");
    const QByteArray good = fp::BootloaderFile::build(target);
    QCOMPARE(int(checkBootloaderFile(QByteArray(), target).status), int(BootFileStatus::TooShort));
    for (int n : {1, 4, 5, 7, 8})
        QCOMPARE(int(checkBootloaderFile(good.left(n), target).status),
                 int(BootFileStatus::TooShort));
    // One byte of program is enough.
    const QByteArray program(1, 'x');
    const QByteArray tiny = fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(5, program),
                                                         QByteArray("ipvd"), program);
    QCOMPARE(tiny.size(), 9);
    QVERIFY(checkBootloaderFile(tiny, target).ok());
    // The smallest difference is refused: one bit of one byte, and one byte less.
    QByteArray flipped = good;
    flipped[good.size() - 1] = char(flipped.at(good.size() - 1) ^ 0x80);
    QCOMPARE(int(checkBootloaderFile(flipped, target).status), int(BootFileStatus::BadChecksum));
    QCOMPARE(int(checkBootloaderFile(good.left(good.size() - 1), target).status),
             int(BootFileStatus::BadChecksum));
    // A byte added after the program changes the sum too.
    QCOMPARE(int(checkBootloaderFile(good + QByteArray(1, '\1'), target).status),
             int(BootFileStatus::BadChecksum));
}

void TestBootFile::unknownTarget()
{
    const QByteArray file = fp::BootloaderFile::build(QStringLiteral("ipodvideo"));
    for (const QString &t :
         {QString(), QStringLiteral("ipodgray"), QStringLiteral("ipodclassic")}) {
        const BootFileCheck c = checkBootloaderFile(file, t);
        QCOMPARE(int(c.status), int(BootFileStatus::UnknownTarget));
        QVERIFY(c.message.startsWith(QStringLiteral("check 4: ")));
    }
}

void TestBootFile::programIsReturned()
{
    const QString target = QStringLiteral("ipodnano2g");
    const QByteArray file = fp::BootloaderFile::build(target);
    const BootFileCheck c = checkBootloaderFile(file, target);
    QVERIFY(c.ok());
    QCOMPARE(c.program, file.mid(8));
    // The 2nd generation nano file starts with the hash block (section 7.5).
    QVERIFY(c.program.size() > 0x800);
}

QTEST_GUILESS_MAIN(TestBootFile)
#include "tst_installer_bootfile.moc"
