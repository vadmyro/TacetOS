// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// Our installer against the reference partition tool, byte for byte
// (docs/specs/bootloader-legacy.md, sections 13 to 17). The same image is copied
// twice. Install, update and uninstall run on one copy through our code and on the
// other through the tool, with the same synthetic bootloader file, and after each
// step the two files are compared whole.
//
// The spec says where our writes differ from the tool's on purpose, and only those
// zones may differ (the table in `zonesFor()` below, each with its section). Any
// other byte that differs fails the test. A difference that no section explains is
// never fixed here: it is reported with its offsets, and the spec is looked at by a
// person.
//
// The tool is built by build-reference-tools.sh and is never changed. Without
// TACETOS_REFERENCE_TOOLS_DIR every test is skipped. The 2nd generation nano has
// sectors of 2048 bytes, which the tool cannot read from a plain file: like in
// tst_reference_partition it goes through a loop disk, on Linux with
// TACETOS_REFERENCE_LOOP=1 and a sudo that asks for no password (the CI).

#include "bootloaderfile.h"
#include "constants.h"
#include "fakepod.h"
#include "firmware.h"
#include "operation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace tacetos::installer;
namespace fp = tacetos::fakepod;

namespace {

QString toolPath()
{
    const QString dir = QString::fromUtf8(TACETOS_REFERENCE_TOOLS_DIR);
    if (dir.isEmpty())
        return QString();
    QString path = QDir(dir).filePath(QStringLiteral("partition-tool"));
#ifdef Q_OS_WIN
    path += QStringLiteral(".exe");
#endif
    return path;
}

struct Run
{
    bool started = false;
    int exitCode = -1;
    QString output;
};

Run run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    Run r;
    r.started = p.waitForStarted(10000);
    if (!r.started)
        return r;
    p.closeWriteChannel();
    if (!p.waitForFinished(120000)) {
        p.kill();
        p.waitForFinished();
    }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.output = QString::fromUtf8(p.readAll());
    return r;
}

QByteArray readAt(const QString &path, qint64 offset, qint64 length)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return QByteArray();
    return f.read(length);
}

quint32 le32(const QByteArray &b, qint64 at)
{
    return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8 |
           quint32(quint8(b[at + 2])) << 16 | quint32(quint8(b[at + 3])) << 24;
}

// A bootloader file for `target`: header, then a program of `length` bytes.
QByteArray fileFor(const QString &target, int length)
{
    const QByteArray program = fp::BootloaderFile::program(target, length);
    const fp::BootTarget *t = fp::BootloaderFile::target(target);
    return fp::BootloaderFile::assemble(fp::BootloaderFile::checksum(t->number, program),
                                        QByteArray(t->tag), program);
}

QString targetOf(const QString &modelId)
{
    return fp::BootloaderFile::targetForOsValue(fp::FakePod::model(modelId)->osValue);
}

// A byte range of the whole image, and why the two copies may differ there.
struct Zone
{
    qint64 start;
    qint64 end;
    QString what;
    QString section;
};

// The first bytes that differ outside every zone, merged into ranges.
struct Range
{
    qint64 start;
    qint64 end;
};

QList<Range> unexplainedDifferences(const QString &a, const QString &b, const QList<Zone> &zones)
{
    QList<Range> out;
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly) || fa.size() != fb.size())
        return {Range{0, -1}};
    constexpr qint64 kChunk = 1 << 20;
    for (qint64 base = 0; base < fa.size(); base += kChunk) {
        const QByteArray ca = fa.read(kChunk);
        const QByteArray cb = fb.read(kChunk);
        if (ca == cb)
            continue;
        for (qint64 i = 0; i < ca.size(); ++i) {
            if (ca[i] == cb[i])
                continue;
            const qint64 at = base + i;
            bool explained = false;
            for (const Zone &z : zones) {
                if (at >= z.start && at < z.end) {
                    explained = true;
                    break;
                }
            }
            if (explained)
                continue;
            if (!out.isEmpty() && out.last().end == at)
                out.last().end = at + 1;
            else
                out.append(Range{at, at + 1});
        }
    }
    return out;
}

constexpr int kHashBlock = 0x800;

} // namespace

class TstInstallerReference : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void sameBytesAsTheReferenceTool_data();
    void sameBytesAsTheReferenceTool();

private:
    struct Image
    {
        QString path;
        int sectorSize = 512;
    };

    bool referenceStep(const Image &img, const QStringList &args, QString *output);
    bool oursStep(const Image &img, Operation op, const QByteArray &file, QString *message);

    QString m_tool;
    QTemporaryDir m_dir;
    int m_count = 0;
};

void TstInstallerReference::init()
{
    m_tool = toolPath();
    if (m_tool.isEmpty())
        QSKIP("TACETOS_REFERENCE_TOOLS_DIR is not set: no reference tool to compare with");
    if (!QFileInfo(m_tool).isExecutable())
        QSKIP(qPrintable(
            QStringLiteral("no reference tool at %1 (run tests/reference/build-reference-tools.sh)")
                .arg(m_tool)));
}

// The tool on a copy of the image. A plain file for 512 byte sectors; a loop disk
// with sectors of 2048 bytes for the 2nd generation nano.
bool TstInstallerReference::referenceStep(const Image &img, const QStringList &args,
                                          QString *output)
{
    if (img.sectorSize == 512) {
        const Run r = run(m_tool, QStringList{img.path} + args);
        *output = r.output;
        return r.started && r.exitCode == 0;
    }
#ifdef Q_OS_LINUX
    const Run attach =
        run(QStringLiteral("sudo"),
            {QStringLiteral("-n"), QStringLiteral("losetup"), QStringLiteral("--find"),
             QStringLiteral("--show"), QStringLiteral("--sector-size"),
             QString::number(img.sectorSize), img.path});
    *output = attach.output;
    const QString device = attach.output.trimmed();
    if (!attach.started || attach.exitCode != 0 || !device.startsWith(QStringLiteral("/dev/loop")))
        return false;
    const Run r =
        run(QStringLiteral("sudo"), QStringList{QStringLiteral("-n"), m_tool, device} + args);
    const Run detach = run(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("losetup"),
                                                    QStringLiteral("-d"), device});
    *output = r.output + detach.output;
    return r.started && r.exitCode == 0 && detach.exitCode == 0;
#else
    return false;
#endif
}

bool TstInstallerReference::oursStep(const Image &img, Operation op, const QByteArray &file,
                                     QString *message)
{
    FileSectorDevice dev(img.path, img.sectorSize, FileSectorDevice::Mode::ReadWrite);
    if (!dev.open().ok)
        return false;
    OperationRequest req;
    req.op = op;
    req.scan = takeScanSnapshot(dev, recognise(dev));
    req.bootloaderFile = file;
    req.backupDir = m_dir.filePath(QStringLiteral("backup%1").arg(++m_count));
    req.allowRealWrites = false;
    const OperationResult res = runOperation(dev, req);
    *message = res.message;
    return res.ok;
}

void TstInstallerReference::sameBytesAsTheReferenceTool_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<bool>("mac");
    QTest::addColumn<bool>("following");
    QTest::addColumn<int>("installLength");
    QTest::addColumn<int>("updateLength");

    // Appended layout, section 14. Alone: the new data fits before the end of the
    // partition, an update with a larger program too. With following images
    // (RSRC and AUPD): the install moves them (section 14.1), the update is smaller.
    for (const QString &id : fp::FakePod::modelIds()) {
        if (id == QLatin1String("classic-6g"))
            continue;
        const bool nano2g = id == QLatin1String("nano-2g");
        const QByteArray name = id.toLatin1();
        QTest::newRow(name + " alone") << id << false << false << 4000 << 6000;
        QTest::newRow(name + " with following images") << id << false << true << 4000 << 3000;
        if (fp::FakePod::model(id)->hasMac && !nano2g)
            QTest::newRow(name + " mac with following images")
                << id << true << true << 4000 << 3000;
    }
}

void TstInstallerReference::sameBytesAsTheReferenceTool()
{
    QFETCH(QString, model);
    QFETCH(bool, mac);
    QFETCH(bool, following);
    QFETCH(int, installLength);
    QFETCH(int, updateLength);

    const bool nano2g = model == QLatin1String("nano-2g");
    if (nano2g) {
#ifndef Q_OS_LINUX
        QSKIP("a loop disk with sectors of 2048 bytes only exists on Linux");
#else
        if (qEnvironmentVariable("TACETOS_REFERENCE_LOOP") != QLatin1String("1"))
            QSKIP("no loop disk: set TACETOS_REFERENCE_LOOP=1 where sudo asks for no password "
                  "(the CI)");
#endif
    }

    fp::FakePodSpec spec;
    spec.modelId = model;
    spec.path = m_dir.filePath(QStringLiteral("ours%1.bin").arg(++m_count));
    spec.table = mac ? fp::TableKind::Apm : fp::TableKind::Dos;
    spec.withRsrc = spec.withAupd = following;
    const fp::FakePodResult pod = fp::FakePod::build(spec);
    QVERIFY2(pod.ok, qPrintable(pod.error));
    const int sector = pod.sectorSize;
    const Image ours{spec.path, sector};

    if (mac) {
        // Section 12 and section 24 rule 6: the tool installs on a Mac iPod, TacetOS
        // refuses on purpose. Nothing to compare: the refusal is checked, and not a
        // byte of the image may change.
        const QByteArray before = readAt(ours.path, 0, QFileInfo(ours.path).size());
        QVERIFY(!before.isEmpty());
        QString message;
        QVERIFY(
            !oursStep(ours, Operation::Install, fileFor(targetOf(model), installLength), &message));
        QVERIFY2(message.contains(QLatin1String("Formatted for Mac")), qPrintable(message));
        QCOMPARE(readAt(ours.path, 0, QFileInfo(ours.path).size()), before);
        return;
    }
    const Image ref{m_dir.filePath(QStringLiteral("ref%1.bin").arg(++m_count)), sector};
    QVERIFY(QFile::copy(ours.path, ref.path));

    const QString target = targetOf(model);
    const qint64 fw = pod.firmwareStart;
    // The original image of the firmware partition, kept for the moved images.
    const QByteArray original = readAt(ours.path, fw, pod.firmwareBytes);

    auto stateOf = [&](const QString &path) {
        FileSectorDevice dev(path, sector);
        return dev.open().ok ? readFirmware(dev, recognise(dev)) : FirmwareState();
    };

    QList<Zone> zones;
    // How far the tool moves following images beyond what we move them (section 14.1).
    qint64 extraMove = 0;
    // 2nd generation nano: the clean image before the install, and the bytes our
    // bootloader took after the last image (the larger of install and update).
    const QString clean = m_dir.filePath(QStringLiteral("clean%1.bin").arg(++m_count));
    if (nano2g)
        QVERIFY(QFile::copy(ours.path, clean));
    qint64 bootStart = 0, bootEnd = 0;

    enum Step { Install, Update, Uninstall };
    for (const Step step : {Install, Update, Uninstall}) {
        const char *stepName = step == Install  ? "install"
                               : step == Update ? "update"
                                                : "uninstall";
        const int length = step == Install ? installLength : updateLength;
        const QByteArray file = step == Uninstall ? QByteArray() : fileFor(target, length);
        const FirmwareState before = stateOf(ours.path);
        QVERIFY2(before.state == (step == Install ? State::Clean : State::Installed), stepName);

        QString message;
        QVERIFY2(oursStep(ours,
                          step == Install  ? Operation::Install
                          : step == Update ? Operation::Update
                                           : Operation::Uninstall,
                          file, &message),
                 qPrintable(QStringLiteral("%1, our code: %2").arg(stepName, message)));

        if (step == Uninstall && nano2g) {
            // The command line of the tool cannot uninstall on this model (it only
            // looks for the added-bootloader case, section 16), so there is nothing
            // to compare with. Our result is compared with the clean image from before
            // the install instead: section 17 gives the directory back as it was, and
            // the only bytes that may stay different are those of our bootloader,
            // left after the last image ("The bytes of our bootloader stay after the
            // last image, unused").
            const FirmwareState after = stateOf(ours.path);
            QCOMPARE(after.state, State::Clean);
            const QList<Zone> left{{bootStart, bootEnd,
                                    QStringLiteral("our bootloader left after the last image"),
                                    QStringLiteral("section 17")}};
            const QList<Range> bad = unexplainedDifferences(ours.path, clean, left);
            QVERIFY2(bad.isEmpty(),
                     qPrintable(QStringLiteral("after uninstall, %1 range(s) differ from the "
                                               "clean image that no section explains, the first "
                                               "at image offset 0x%2")
                                    .arg(bad.size())
                                    .arg(bad.isEmpty() ? 0 : bad.first().start, 0, 16)));
            return;
        }

        QString output;
        bool refOk;
        if (step == Uninstall) {
            refOk = referenceStep(ref, {QStringLiteral("-d")}, &output);
        } else {
            // The tool checks the tag of the file against its own name for the model.
            // For the 2nd generation nano that name is `nn2x`, and the file we write
            // for it is tagged `nn2g`: the two tools of section 8 disagree, and
            // section 27 leaves the choice open. Only these 4 bytes of the file
            // differ; the program, and so the checksum, are the same.
            QByteArray toolFile = file;
            if (nano2g)
                toolFile.replace(4, 4, "nn2x");
            const QString filePath = m_dir.filePath(QStringLiteral("boot%1.ipod").arg(++m_count));
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(toolFile), qint64(toolFile.size()));
            f.close();
            refOk = referenceStep(ref, {QStringLiteral("-a"), filePath}, &output);
        }
        // The tool exits with 0 even when it refuses (it prints "--add-bootloader failed").
        refOk = refOk && !output.contains(QStringLiteral("bootloader failed"));
        QVERIFY2(refOk, qPrintable(QStringLiteral("%1, the tool: %2").arg(stepName, output)));

        const FirmwareState after = stateOf(ours.path);
        QCOMPARE(after.state, step == Uninstall ? State::Clean : State::Installed);
        QCOMPARE(stateOf(ref.path).state, after.state);
        const qint64 dir = fw + after.directoryStart;

        if (step != Uninstall && !nano2g) {
            // Section 13 rule 6: the zero padding between the end of the program and
            // the next whole sector. The tool pads with zeros here too, so this is not
            // a zone: the bytes must be the same, and ours are checked to be zero.
            const DirectoryEntry &os = after.entryList.at(after.osIndex);
            const qint64 progEnd = fw + after.imageStart(os) + os.length;
            const qint64 padEnd = roundUpToSector(progEnd - fw, sector) + fw;
            QVERIFY2(readAt(ours.path, progEnd, padEnd - progEnd) ==
                         QByteArray(int(padEnd - progEnd), '\0'),
                     "our padding after the program is not zero");

            // Section 14.1: the following images move together, as one block, and
            // the tool moves them one sector further.
            if (step == Install && following) {
                const qint64 delta = qint64(after.entryList.at(1).devOffset) -
                                     qint64(before.entryList.at(1).devOffset);
                if (delta > 0) {
                    const qint64 blockStart = before.imageStart(before.entryList.at(1));
                    const DirectoryEntry &last = before.entryList.last();
                    const qint64 blockEnd = before.imageStart(last) + last.paddedBytes(sector);
                    extraMove = sector;
                    QCOMPARE(readAt(ours.path, fw + blockStart + delta, blockEnd - blockStart),
                             original.mid(int(blockStart), int(blockEnd - blockStart)));
                    QCOMPARE(readAt(ref.path, fw + blockStart + delta + extraMove,
                                    blockEnd - blockStart),
                             original.mid(int(blockStart), int(blockEnd - blockStart)));
                    zones.append(
                        {fw + blockStart + delta, fw + blockEnd + delta + extraMove,
                         QStringLiteral("the moved images, one sector further in the tool"),
                         QStringLiteral("section 14.1")});
                }
            }
        }
        if (step != Uninstall && nano2g) {
            // Section 15 step 2: our new image starts on a whole sector. The tool puts
            // it right after the end of the last image, whatever its length, so the
            // new entry has a smaller devOffset and the data sit a few bytes earlier.
            // Nothing else changes: the entry is the same in every other field, and
            // both places hold the hash block and the program, then zeros to the end
            // of the sector.
            const FirmwareState toolState = stateOf(ref.path);
            QCOMPARE(toolState.osIndex, after.osIndex);
            const DirectoryEntry &o = after.entryList.at(after.osIndex);
            const DirectoryEntry &t = toolState.entryList.at(toolState.osIndex);
            QCOMPARE(qint64(o.devOffset), roundUpToSector(t.devOffset, sector));
            QByteArray payload = file.mid(8);
            const qint64 padded = roundUpToSector(payload.size(), sector);
            payload.append(QByteArray(int(padded - payload.size()), '\0'));
            const qint64 oursStart = fw + after.imageStart(o) - kHashBlock;
            const qint64 toolStart = fw + toolState.imageStart(t) - kHashBlock;
            QCOMPARE(readAt(ours.path, oursStart, padded), payload);
            if (bootEnd == 0)
                bootStart = oursStart;
            bootEnd = qMax(bootEnd, oursStart + padded);
            QCOMPARE(readAt(ref.path, toolStart, padded), payload);
            zones.append({oursStart, oursStart + padded,
                          QStringLiteral("new image, on a whole sector (ours)"),
                          QStringLiteral("section 15 step 2")});
            zones.append({toolStart, toolStart + padded,
                          QStringLiteral("new image, not rounded to a sector (tool)"),
                          QStringLiteral("section 15 step 2")});
            zones.append({dir + after.osIndex * kDirectoryEntrySize + 12,
                          dir + after.osIndex * kDirectoryEntrySize + 16,
                          QStringLiteral("devOffset of the new entry"),
                          QStringLiteral("section 15 step 2")});
        }
        // The moved images carry a devOffset one sector larger in the tool, in every
        // directory read after the move (section 14.1). The same four bytes of each
        // following entry; nothing else of the directory.
        QVERIFY(extraMove == 0 || !nano2g);
        if (extraMove) {
            const QByteArray oursDir = readAt(ours.path, dir, 400);
            const QByteArray refDir = readAt(ref.path, dir, 400);
            for (int i = 1; i < after.entryList.size(); ++i) {
                const qint64 at = qint64(i) * kDirectoryEntrySize + 12;
                QCOMPARE(qint64(le32(refDir, at)), qint64(le32(oursDir, at)) + extraMove);
                zones.append({dir + at, dir + at + 4,
                              QStringLiteral("devOffset of the entry %1 after the move").arg(i),
                              QStringLiteral("section 14.1")});
            }
        }

        const QList<Range> bad = unexplainedDifferences(ours.path, ref.path, zones);
        QString report;
        for (int i = 0; i < bad.size() && i < 8; ++i) {
            const qint64 s = bad.at(i).start;
            report += QStringLiteral("\n  image offset 0x%1 (firmware partition offset 0x%2), %3 "
                                     "byte(s): ours %4, tool %5")
                          .arg(s, 0, 16)
                          .arg(s - fw, 0, 16)
                          .arg(bad.at(i).end - s)
                          .arg(QString::fromLatin1(readAt(ours.path, s, 8).toHex(' ')),
                               QString::fromLatin1(readAt(ref.path, s, 8).toHex(' ')));
        }
        QVERIFY2(
            bad.isEmpty(),
            qPrintable(QStringLiteral("after %1, %2 range(s) differ that no section explains:%3")
                           .arg(stepName)
                           .arg(bad.size())
                           .arg(report)));
    }
}

QTEST_GUILESS_MAIN(TstInstallerReference)
#include "tst_installer_reference.moc"
