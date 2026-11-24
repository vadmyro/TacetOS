// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The whole percentage of the big progress page (docs/specs/boot-screen.md section 10.3), fed
// with the runner signals recorded from real flows (progress/*.tsv) and with made-up cases.

#include "flow.h"
#include "wholeprogress.h"

#include <QFile>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QTest>
#include <functional>

using namespace tacetos::wizard;

namespace {

// Replays a recording: `steps`, `started`, `progress` and `done` lines, tab-separated. After each
// line, `after` gets the line and may act on the progress; `back` says if the percent ever went
// back. Returns (step, percent) at each `done`.
QList<QPair<QString, int>> replay(const QString &file, WholeProgress &whole, bool &back,
                                  const std::function<void(const QStringList &)> &after = {})
{
    QList<QPair<QString, int>> ends;
    QFile f(QStringLiteral(TACETOS_PROGRESS_DIR "/") + file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("cannot open %s", qPrintable(f.fileName()));
        return ends;
    }
    int last = 0;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList p = line.split(QLatin1Char('\t'));
        if (p.at(0) == QLatin1String("steps"))
            whole.reset(p.at(1).split(QLatin1Char(',')));
        else if (p.at(0) == QLatin1String("started"))
            whole.stepStarted(p.at(1).toInt(), p.at(2));
        else if (p.at(0) == QLatin1String("progress"))
            whole.stepProgress(p.at(1), p.at(3).toLongLong(), p.at(4).toLongLong());
        else if (p.at(0) == QLatin1String("done"))
            whole.stepDone(p.at(2));
        if (after)
            after(p);
        if (whole.percent() < last)
            back = true;
        last = qMax(last, whole.percent());
        if (p.at(0) == QLatin1String("done"))
            ends.append({p.at(2), whole.percent()});
    }
    return ends;
}

using Ends = QList<QPair<QString, int>>;

} // namespace

class TestWholeProgress : public QObject
{
    Q_OBJECT

private slots:
    void writingSteps()
    {
        for (const QLatin1String name :
             {step::format, step::convert, step::firmware, step::bootloader,
              step::classicBootloader, step::removeBootloader, step::classicRemove,
              step::removeFirmware, step::verify})
            QVERIFY2(WholeProgress::isWriting(name), name.data());
        for (const QLatin1String name : {step::diagnose, step::backupFull, step::backupKeep,
                                         step::backupSettings, step::diskMode, step::finish})
            QVERIFY2(!WholeProgress::isWriting(name), name.data());
    }

    // A clean install of a Classic: four writing steps (erase, firmware, bootloader, check), so
    // each is a quarter. The checks and the backup move nothing; the bytes of the transfer move
    // nothing either, the wait for the restart does.
    void recordedClassicInstall()
    {
        WholeProgress whole;
        bool back = false;
        QList<int> duringErase;
        QList<int> duringRestart;
        const Ends ends = replay(
            QStringLiteral("classic-clean-install.tsv"), whole, back, [&](const QStringList &p) {
                if (p.at(0) == QLatin1String("progress") && p.at(1) == step::format)
                    duringErase.append(whole.percent());
                if (p.at(0) == QLatin1String("progress") && p.at(2) == QLatin1String("send"))
                    duringRestart.append(whole.percent());
                if (p.at(0) == QLatin1String("progress") && p.at(2) == QLatin1String("resume")) {
                    duringRestart.append(whole.percent());
                    whole.restartWaited(30 * 1000, 60 * 1000);
                    duringRestart.append(whole.percent());
                    whole.restartWaited(120 * 1000, 60 * 1000);
                    duringRestart.append(whole.percent());
                }
            });
        QVERIFY(!back);
        QCOMPARE(whole.count(), 4);
        QCOMPARE(ends, Ends({{"diagnose", 0},
                             {"backup-full", 0},
                             {"format", 25},
                             {"firmware", 50},
                             {"classic-bootloader", 75},
                             {"verify", 100},
                             {"finish", 100}}));
        QCOMPARE(duringErase, QList<int>({0, 8, 16, 25}));
        // 50 before the restart, half the wait, then at most 0.9 of the share.
        QCOMPARE(duringRestart, QList<int>({50, 50, 62, 72}));
    }

    // "Keep my music" on an older iPod: Disk Mode is no writing step, so three thirds.
    void recordedLegacyInstall()
    {
        WholeProgress whole;
        bool back = false;
        const Ends ends = replay(QStringLiteral("legacy-keep-install.tsv"), whole, back);
        QVERIFY(!back);
        QCOMPARE(whole.count(), 3);
        QCOMPARE(ends, Ends({{"diagnose", 0},
                             {"backup-keep", 0},
                             {"disk-mode", 0},
                             {"firmware", 33},
                             {"bootloader", 66},
                             {"verify", 100},
                             {"finish", 100}}));
    }

    // A total of 0 keeps the part; a try again of the same step never takes the bar back; a
    // new flow does.
    void neverBackUntilANewFlow()
    {
        const QStringList steps = {QString(step::firmware), QString(step::bootloader)};
        WholeProgress whole;
        whole.reset(steps);
        whole.stepStarted(0, QString(step::firmware));
        whole.stepProgress(QString(step::firmware), 1, 2);
        QCOMPARE(whole.percent(), 25);
        whole.stepProgress(QString(step::firmware), 0, 0);
        QCOMPARE(whole.percent(), 25);
        whole.stepStarted(0, QString(step::firmware));
        whole.stepProgress(QString(step::firmware), 1, 10);
        QCOMPARE(whole.percent(), 25);
        whole.stepProgress(QString(step::firmware), 2, 1);
        QCOMPARE(whole.percent(), 50);
        whole.reset(steps);
        QCOMPARE(whole.percent(), 0);
        QCOMPARE(whole.fraction(), 0.0);
    }

    // A flow resumed at its second writing step counts the first one as done.
    void resumeCountsTheStepsBefore()
    {
        WholeProgress whole;
        whole.reset({QString(step::diagnose), QString(step::format), QString(step::firmware),
                     QString(step::finish)});
        whole.stepStarted(2, QString(step::firmware));
        QCOMPARE(whole.percent(), 50);
        whole.stepDone(QString(step::firmware));
        QCOMPARE(whole.percent(), 100);
    }

    // A flow with no writing step leaves the bar at 0.
    void noWritingStep()
    {
        WholeProgress whole;
        whole.reset({QString(step::diagnose), QString(step::backupSettings)});
        whole.stepStarted(1, QString(step::backupSettings));
        whole.stepProgress(QString(step::backupSettings), 5, 10);
        whole.stepDone(QString(step::backupSettings));
        QCOMPARE(whole.count(), 0);
        QCOMPARE(whole.percent(), 0);
    }
};

QTEST_GUILESS_MAIN(TestWholeProgress)
#include "tst_wizard_wholeprogress.moc"
