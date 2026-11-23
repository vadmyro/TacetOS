// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// docs/specs/install-wizard.md sections 7.2 to 7.5 and 15.4: the step
// `firmware`. A temporary folder plays the music volume; the packages are made
// by the test, with files that are no real build.

#include "backup.h"
#include "constants.h"
#include "firmware.h"
#include "testzip.h"
#include "wizardconstants.h"
#include "wizarderrors.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>

using namespace tacetos::wizard;
using tacetos::wizard::testing::storedZip;
namespace device = tacetos::device;
namespace installer = tacetos::installer;

namespace {

const QString kTarget = QStringLiteral("ipod6g");
const QString kSerial = QStringLiteral("8K7FIRMWARE");

QString fw()
{
    return QString(device::kFirmwareFolder);
}

QByteArray zipName(const QString &inside, const QString &top = fw())
{
    return (top + QLatin1Char('/') + inside).toUtf8();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QString sha(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// Every file under a folder with its bytes, hidden ones included.
QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> all;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        all.insert(QDir(root).relativeFilePath(path),
                   f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("?"));
    }
    return all;
}

void copyTree(const QString &from, const QString &to)
{
    const auto all = snapshot(from);
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        writeFile(to + QLatin1Char('/') + it.key(), it.value());
}

// The files of the package, inside the firmware folder.
QMap<QString, QByteArray> packageFiles(const QString &infoTarget = kTarget)
{
    return {
        {QString(device::kFirmwareInfoFile), "Target: " + infoTarget.toUtf8() + "\nVersion: 4.0\n"},
        {QStringLiteral("tacetos.ipod"), QByteArray(300000, 'p')},
        {QStringLiteral("themes/default.cfg"), "package theme"},
        {QStringLiteral("fonts/a.fnt"), QByteArray(5000, 'a')},
        {QStringLiteral("codecs/mp3.codec"), QByteArray(20000, 'c')},
    };
}

class Sources : public FirmwareSources
{
public:
    QString volumeRoot() const override { return volume; }
    QString target() const override { return kTarget; }
    QString firmwareFolder() const override { return folder; }
    qint64 freeBytes() const override { return free; }
    QString settingsBackup() const override { return backup; }
    installer::FileFault fault() const override { return injected; }

    QString volume;
    QString folder = fw();
    qint64 free = -1;
    QString backup;
    installer::FileFault injected;
};

class Context : public StepContext
{
public:
    QString serial() const override { return kSerial; }
    QString model() const override { return QStringLiteral("classic-6g"); }
    FlowShape shape() const override
    {
        return {Family::Dfu, FlowKind::Install, InstallPath::Keep, false};
    }
    bool isResume() const override { return false; }
    bool stopRequested() const override { return stop; }
    void progress(const QString &stage, qint64, qint64) override
    {
        if (!stages.contains(stage))
            stages << stage;
    }
    UserAnswer waitForUser(WaitKind kind, const QJsonObject &info) override
    {
        waits << kind;
        offer = info;
        return {false, takeOffer, QString()};
    }
    QJsonValue fact(const QString &, const QString &key) const override { return facts.value(key); }
    void setFact(const QString &key, const QJsonValue &value) override { facts.insert(key, value); }
    QString backupFolder() const override { return folder; }
    void setBackupFolder(const QString &path) override { folder = path; }
    QString packageManifest() const override { return manifest; }
    QString packageSha256() const override { return manifestSha; }

    QString manifest;
    QString manifestSha;
    QString folder;
    std::atomic<bool> stop{false};
    bool takeOffer = true;
    QList<WaitKind> waits;
    QJsonObject offer;
    QJsonObject facts;
    QStringList stages;
};

} // namespace

class TstWizardFirmware : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    int m_count = 0;

    QString newFolder(const QString &name)
    {
        const QString path = m_tmp.filePath(name + QString::number(++m_count));
        QDir().mkpath(path);
        return path;
    }

    // A package in a folder of its own; the context of a flow that started
    // with it.
    void makePackage(Context &context, const QMap<QString, QByteArray> &files = packageFiles(),
                     const QString &top = fw())
    {
        const QString folder = newFolder(QStringLiteral("package"));
        QList<QPair<QByteArray, QByteArray>> entries{{zipName(QString(), top), QByteArray()}};
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            entries.append({zipName(it.key(), top), it.value()});
        const QByteArray zip = storedZip(entries);
        const QByteArray bootloader(1000, 'b');
        writeFile(folder + QStringLiteral("/tacetos-ipod6g-0.2.0.zip"), zip);
        writeFile(folder + QStringLiteral("/tacetos-bootloader-ipod6g-0.2.0.ipod"), bootloader);
        auto file = [](const char *name, const char *kind, const QByteArray &bytes) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), QLatin1String(name));
            o.insert(QStringLiteral("kind"), QLatin1String(kind));
            o.insert(QStringLiteral("size"), double(bytes.size()));
            o.insert(QStringLiteral("sha256"), sha(bytes));
            return o;
        };
        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), 1);
        manifest.insert(QStringLiteral("target"), kTarget);
        manifest.insert(
            QStringLiteral("files"),
            QJsonArray{file("tacetos-ipod6g-0.2.0.zip", "firmware", zip),
                       file("tacetos-bootloader-ipod6g-0.2.0.ipod", "bootloader", bootloader)});
        const QByteArray text = QJsonDocument(manifest).toJson();
        context.manifest = folder + QStringLiteral("/tacetos-ipod6g-0.2.0.json");
        writeFile(context.manifest, text);
        context.manifestSha = sha(text);
    }

    // A volume whose firmware folder holds the user's files and old firmware.
    QString volumeWithOldFirmware()
    {
        const QString v = newFolder(QStringLiteral("volume"));
        const QString f = v + QLatin1Char('/') + fw();
        writeFile(f + QStringLiteral("/config.cfg"), "volume: 12\n");
        writeFile(f + QStringLiteral("/Fixed.cfg"), "fixed\n");
        writeFile(f + QStringLiteral("/.resume.cfg"), QByteArray(64, 'r'));
        writeFile(f + QStringLiteral("/themes/mine.cfg"), "my theme");
        writeFile(f + QStringLiteral("/themes/default.cfg"), "my old default");
        writeFile(f + QStringLiteral("/wps/Été/x.wps"), "wps");
        writeFile(f + QStringLiteral("/rocks.data/game.sav"), "score 42");
        writeFile(f + QStringLiteral("/database_0.tcd"), QByteArray(400, 'd'));
        writeFile(f + QStringLiteral("/old.rock"), "old plugin");
        writeFile(f + QLatin1Char('/') + QString(device::kFirmwareInfoFile), "Target: ipod6g\n");
        writeFile(v + QStringLiteral("/Music/a.mp3"), QByteArray(1000, 'm'));
        return v;
    }

    // What the firmware folder holds after an install over `volumeWithOldFirmware()`.
    static QMap<QString, QByteArray> expectedAfterUpgrade()
    {
        QMap<QString, QByteArray> all = packageFiles();
        all.insert(QStringLiteral("config.cfg"), "volume: 12\n");
        all.insert(QStringLiteral("Fixed.cfg"), "fixed\n");
        all.insert(QStringLiteral(".resume.cfg"), QByteArray(64, 'r'));
        all.insert(QStringLiteral("themes/mine.cfg"), "my theme");
        all.insert(QStringLiteral("wps/Été/x.wps"), "wps");
        all.insert(QStringLiteral("rocks.data/game.sav"), "score 42");
        return all;
    }

    static bool noWorkFolders(const QString &volume)
    {
        return !QFileInfo::exists(volume + QLatin1Char('/') + fw() + QString(kNewSuffix)) &&
               !QFileInfo::exists(volume + QLatin1Char('/') + fw() + QString(kOldSuffix));
    }

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void freshInstall()
    {
        Context context;
        makePackage(context);
        Sources sources;
        sources.volume = newFolder(QStringLiteral("volume"));
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        QCOMPARE(snapshot(sources.volume + QLatin1Char('/') + fw()), packageFiles());
        QVERIFY(noWorkFolders(sources.volume));
        QVERIFY(context.facts.value(QStringLiteral("swapped")).toBool());
        QCOMPARE(context.stages, QStringList({QStringLiteral("unpack"), QStringLiteral("keep"),
                                              QStringLiteral("check")}));
        QVERIFY(context.waits.isEmpty());
    }

    // Branding section 8.2: a model with its own firmware folder gets the firmware
    // there, and the check of section 11.1 reads it there.
    void folderOfTheModel()
    {
        Context context;
        makePackage(context, packageFiles(), QStringLiteral(".own"));
        Sources sources;
        sources.volume = newFolder(QStringLiteral("volume"));
        sources.folder = QStringLiteral(".own");
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        QCOMPARE(snapshot(sources.volume + QStringLiteral("/.own")), packageFiles());
        QVERIFY(!QFileInfo::exists(sources.volume + QLatin1Char('/') + fw()));
        const QString zip =
            QFileInfo(context.manifest).absoluteDir().filePath("tacetos-ipod6g-0.2.0.zip");
        QVERIFY(firmwareFolderDifference(sources.volume, zip, kTarget, sources.folder).isEmpty());
        QVERIFY(!firmwareFolderDifference(sources.volume, zip, kTarget, fw()).isEmpty());
    }

    // Section 7.3: the user's files stay byte for byte, the package's file wins
    // in the theme folders, the database and the rest go.
    void keepsTheUserFiles()
    {
        Context context;
        makePackage(context);
        Sources sources;
        sources.volume = volumeWithOldFirmware();
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        QCOMPARE(snapshot(sources.volume + QLatin1Char('/') + fw()), expectedAfterUpgrade());
        QVERIFY(noWorkFolders(sources.volume));
        QVERIFY(QFileInfo::exists(sources.volume + QStringLiteral("/Music/a.mp3")));
    }

    // Section 7.1: the package is checked again before anything is written.
    void packageCheckedAgain_data()
    {
        QTest::addColumn<bool>("changedManifest");
        QTest::addColumn<QString>("code");
        QTest::newRow("zip changed") << false << QString(error::packageDamaged);
        QTest::newRow("manifest changed") << true << QString(error::packageChanged);
    }

    void packageCheckedAgain()
    {
        QFETCH(bool, changedManifest);
        QFETCH(QString, code);
        Context context;
        makePackage(context);
        if (changedManifest)
            context.manifestSha = sha("the manifest the flow started with");
        else
            writeFile(QFileInfo(context.manifest).absolutePath() +
                          QStringLiteral("/tacetos-ipod6g-0.2.0.zip"),
                      QByteArray(100, 'z'));
        Sources sources;
        sources.volume = volumeWithOldFirmware();
        const auto before = snapshot(sources.volume);
        FirmwareStep step(sources);
        QCOMPARE(step.run(context).code, code);
        QCOMPARE(snapshot(sources.volume), before);
    }

    // Section 7.2: the room is checked before the unpack.
    void noRoom()
    {
        Context context;
        makePackage(context);
        Sources sources;
        sources.volume = volumeWithOldFirmware();
        sources.free = kVolumeMargin;
        const auto before = snapshot(sources.volume);
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QCOMPARE(out.code, QString(error::firmwareNoRoom));
        qint64 need = kVolumeMargin;
        for (const QByteArray &bytes : packageFiles())
            need += bytes.size();
        for (const char *kept : {"config.cfg", "Fixed.cfg", ".resume.cfg", "themes/mine.cfg",
                                 "wps/Été/x.wps", "rocks.data/game.sav"})
            need += expectedAfterUpgrade().value(QString::fromUtf8(kept)).size();
        QCOMPARE(out.detail, QString::number(need));
        QCOMPARE(snapshot(sources.volume), before);
    }

    // A volume that fills up, and a cancel: the new folder goes, the old one
    // is as it was.
    void stoppedBeforeTheSwap_data()
    {
        QTest::addColumn<bool>("full");
        QTest::addColumn<QString>("code");
        QTest::newRow("volume full") << true << QString(error::firmwareWriteFailed);
        QTest::newRow("cancel") << false << QString();
    }

    void stoppedBeforeTheSwap()
    {
        QFETCH(bool, full);
        QFETCH(QString, code);
        Context context;
        makePackage(context);
        Sources sources;
        sources.volume = volumeWithOldFirmware();
        const auto before = snapshot(sources.volume);
        sources.injected = [&](const QString &path, qint64) {
            if (!path.endsWith(QStringLiteral("mp3.codec")))
                return QString();
            if (full)
                return QString::fromLatin1(installer::kNoSpace);
            context.stop = true;
            return QString();
        };
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        if (full)
            QCOMPARE(out.code, code);
        else
            QCOMPARE(out.kind, StepOutcome::Kind::Cancelled);
        QCOMPARE(snapshot(sources.volume), before);
        QVERIFY(noWorkFolders(sources.volume));
        QVERIFY(!context.facts.value(QStringLiteral("swapped")).toBool());
    }

    // Section 7.5: a cut at each row of the table, then the step runs again.
    void afterACut_data()
    {
        QTest::addColumn<bool>("firmware");
        QTest::addColumn<bool>("fresh");
        QTest::addColumn<bool>("old");
        QTest::addColumn<bool>("swapped");
        QTest::addColumn<bool>("unpacks");
        QTest::newRow("before the swap") << true << true << false << false << true;
        QTest::newRow("between the renames") << false << true << true << false << false;
        QTest::newRow("after the swap") << true << false << true << true << false;
        QTest::newRow("rename of the new failed") << false << false << true << false << true;
        QTest::newRow("swap done, nothing left") << true << false << false << true << false;
        QTest::newRow("nothing half done") << true << false << false << false << true;
    }

    void afterACut()
    {
        QFETCH(bool, firmware);
        QFETCH(bool, fresh);
        QFETCH(bool, old);
        QFETCH(bool, swapped);
        QFETCH(bool, unpacks);
        Context context;
        makePackage(context);

        // The new folder of a whole run, and the old folder it replaces.
        Sources done;
        done.volume = volumeWithOldFirmware();
        const QString oldCopy = newFolder(QStringLiteral("oldcopy"));
        copyTree(done.volume + QLatin1Char('/') + fw(), oldCopy);
        Context first;
        first.manifest = context.manifest;
        first.manifestSha = context.manifestSha;
        QCOMPARE(FirmwareStep(done).run(first).kind, StepOutcome::Kind::Done);
        const QString newCopy = done.volume + QLatin1Char('/') + fw();

        // The volume as the cut left it.
        Sources sources;
        sources.volume = newFolder(QStringLiteral("cut"));
        const QString folder = sources.volume + QLatin1Char('/') + fw();
        const bool firmwareIsNew = firmware && (old || swapped);
        if (firmware)
            copyTree(firmwareIsNew ? newCopy : oldCopy, folder);
        if (fresh) {
            // Cut in the middle of the unpack: a part of the new folder only.
            copyTree(newCopy, folder + QString(kNewSuffix));
            if (!old)
                QFile::remove(folder + QString(kNewSuffix) + QStringLiteral("/tacetos.ipod"));
        }
        if (old)
            copyTree(oldCopy, folder + QString(kOldSuffix));
        context.facts.insert(QStringLiteral("swapped"), swapped);

        // A write fails the run: only a run that unpacks writes files.
        bool wrote = false;
        sources.injected = [&wrote](const QString &, qint64) {
            wrote = true;
            return QString();
        };
        const StepOutcome out = FirmwareStep(sources).run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        QCOMPARE(wrote, unpacks);
        QVERIFY(noWorkFolders(sources.volume));
        QCOMPARE(snapshot(folder), expectedAfterUpgrade());
    }

    // Section 7.3: no firmware folder, but a settings backup of this iPod.
    void settingsOffer_data()
    {
        QTest::addColumn<bool>("take");
        QTest::newRow("taken") << true;
        QTest::newRow("refused") << false;
    }

    void settingsOffer()
    {
        QFETCH(bool, take);
        // The backup of an uninstall of this serial.
        class BackupFrom : public BackupSources
        {
        public:
            QString volumeRoot() const override { return volume; }
            QString volumeLabel() const override { return QStringLiteral("IPOD"); }
            QString backupRoot() const override { return root; }
            qint64 freeBytes(const QString &) const override { return -1; }
            qint64 volumeUsedBytes() const override { return 0; }
            FirmwarePartition *firmwarePartition() const override { return nullptr; }
            qint64 firmwarePartitionBytes() const override { return 0; }
            QString volume;
            QString root;
        } backupSources;
        backupSources.volume = volumeWithOldFirmware();
        backupSources.root = newFolder(QStringLiteral("backups"));
        Context backupContext;
        QCOMPARE(BackupStep(BackupKind::Settings, backupSources).run(backupContext).kind,
                 StepOutcome::Kind::Done);

        Context context;
        makePackage(context);
        context.takeOffer = take;
        Sources sources;
        sources.volume = newFolder(QStringLiteral("volume"));
        sources.backup = backupContext.folder;
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QVERIFY2(out.kind == StepOutcome::Kind::Done, qPrintable(out.code + out.detail));
        QCOMPARE(context.waits, QList<WaitKind>{WaitKind::Offer});
        QCOMPARE(context.offer.value(QStringLiteral("backup")).toString(), sources.backup);
        // The settings backup holds the kept files of section 7.3: the same as
        // an install over the old folder.
        const auto expected = take ? expectedAfterUpgrade() : packageFiles();
        QCOMPARE(snapshot(sources.volume + QLatin1Char('/') + fw()), expected);
    }

    // Section 7.4 step 6: an info file for another model fails the check
    // after, and the next run starts again from the unpack.
    void checkAfterFails()
    {
        Context context;
        makePackage(context, packageFiles(QStringLiteral("ipodvideo")));
        Sources sources;
        sources.volume = newFolder(QStringLiteral("volume"));
        FirmwareStep step(sources);
        const StepOutcome out = step.run(context);
        QCOMPARE(out.code, QString(error::firmwareWriteFailed));
        QVERIFY(!context.facts.value(QStringLiteral("swapped")).toBool());
    }
};

QTEST_GUILESS_MAIN(TstWizardFirmware)
#include "tst_wizard_firmware.moc"
