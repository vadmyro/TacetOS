// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "runner.h"
#include "wizard/dfututorial.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QApplication>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QThread>
#include <QXmlStreamReader>
#include <QtTest>
#include <atomic>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

// One recorded event of the step: a progress line, or a wait of the user (the reason is the
// one `classic-dfu` gives to the wait).
struct Event
{
    QString stage;
    qint64 done = 0;
    qint64 total = 0;
    bool wait = false;
    QString reason;
    QString hint;
    QString failedStage;
};

Event progress(const QString &stage, qint64 done = 0, qint64 total = 0)
{
    Event e;
    e.stage = stage;
    e.done = done;
    e.total = total;
    return e;
}

Event wait(const QString &reason = QString(), const QString &hint = QString())
{
    Event e;
    e.wait = true;
    e.reason = reason;
    e.hint = hint;
    return e;
}

// Plays the events in order. Between two events it stops until the test lets it go on, so the
// test reads the page after each one.
struct Script
{
    QList<Event> events;
    std::atomic<int> allowed{0};
    std::atomic<int> played{0};
    std::atomic<int> answers{0};
    QString failCode;
};

class RecordedDfuStep : public Step
{
public:
    explicit RecordedDfuStep(Script &script) : script_(script) {}

    StepOutcome run(StepContext &context) override
    {
        for (const Event &e : script_.events) {
            while (script_.allowed <= script_.played) {
                if (context.stopRequested())
                    return StepOutcome::cancelled();
                QThread::msleep(2);
            }
            if (e.wait) {
                QJsonObject info;
                info.insert(QStringLiteral("model"), QStringLiteral("classic-6g"));
                info.insert(QStringLiteral("reason"), e.reason);
                info.insert(QStringLiteral("hint"), e.hint);
                if (!e.failedStage.isEmpty())
                    info.insert(QStringLiteral("stage"), e.failedStage);
                script_.played++;
                const UserAnswer answer = context.waitForUser(WaitKind::ReadyForDfu, info);
                if (answer.cancelled)
                    return StepOutcome::cancelled();
                script_.answers++;
            } else {
                context.progress(e.stage, e.done, e.total);
                script_.played++;
            }
        }
        if (!script_.failCode.isEmpty())
            return StepOutcome::failed(script_.failCode);
        return StepOutcome::done();
    }

private:
    Script &script_;
};

class DoneStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

// The fill of a named part, read from the SVG that the page shows.
QString fillOf(const QByteArray &svg, const QString &id)
{
    QXmlStreamReader reader(svg);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.attributes().value(QLatin1String("id")) == id)
            return reader.attributes().value(QLatin1String("fill")).toString();
    }
    return QStringLiteral("<no part>");
}

QString opacityOf(const QByteArray &svg, const QString &id)
{
    QXmlStreamReader reader(svg);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.attributes().value(QLatin1String("id")) == id)
            return reader.attributes().value(QLatin1String("opacity")).toString();
    }
    return QStringLiteral("<no part>");
}

// The middle of the screen and the middle of the center button, once the SVG is drawn.
QList<QRgb> pixelsOf(const QByteArray &svg)
{
    QSvgRenderer renderer(svg);
    QImage image(120, 200, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    if (!renderer.isValid())
        return {};
    return {image.pixel(60, 50), image.pixel(60, 142)};
}

QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}

QLabel *label(QWidget *page, const QString &name)
{
    return page->findChild<QLabel *>(name);
}

} // namespace

class TestDfuTutorial : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        Script script;
        qint64 clock = 0;
        QString stepName;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<DfuTutorialPage> page;
        std::unique_ptr<QSignalSpy> waiting;

        explicit Fixture(const QList<Event> &events, bool uninstall = false)
            : stepName(uninstall ? QStringLiteral("classic-remove")
                                 : QStringLiteral("classic-bootloader"))
        {
            script.events = events;
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (name == stepName)
                        return std::make_unique<RecordedDfuStep>(script);
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<DfuTutorialPage>(runner.get());
            page->setClock([this] { return clock; });
            page->setReducedMotion(false);
            page->show();
            // Made after the page: when it counts a wait, the page has read it too.
            waiting = std::make_unique<QSignalSpy>(runner.get(), &WizardRunner::stepWaiting);
            FlowShape shape;
            shape.kind = uninstall ? FlowKind::Uninstall : FlowKind::Install;
            shape.path = uninstall ? InstallPath::None : InstallPath::Clean;
            shape.family = Family::Dfu;
            WizardRunner::Start s;
            s.serial = QStringLiteral("ABC123");
            s.model = QStringLiteral("classic-6g");
            s.shape = shape;
            s.packageManifest = QStringLiteral("/p/m.json");
            QCOMPARE(runner->start(s), QString());
        }

        ~Fixture()
        {
            script.allowed = 1000;
            runner->cancel();
            runner->waitFinished(5000);
        }

        // Lets the step play the next event, and waits until it has.
        void next()
        {
            const int target = script.played + 1;
            script.allowed = target;
            QTRY_VERIFY_WITH_TIMEOUT(script.played >= target, 5000);
        }

        // The page has read the events played so far.
        void settle() { QTest::qWait(30); }

        // The step waits for the answer number `count`: a click now is not lost.
        void waits(int count) { QTRY_COMPARE_WITH_TIMEOUT(int(waiting->count()), count, 5000); }
    };

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    // The wheel: the parts are named and the four ways are the colours of section 6.1.
    void waysColourTheNamedParts()
    {
        for (const Palette &p : {Palette::light(), Palette::dark()}) {
            const QByteArray rest = DfuTutorialPage::drawingSvg(DfuWay::Rest, p);
            QVERIFY(!rest.isEmpty());
            QCOMPARE(fillOf(rest, "menu"), hex(p.border));
            QCOMPARE(fillOf(rest, "center"), hex(p.border));
            QCOMPARE(fillOf(rest, "play"), hex(p.border));
            QCOMPARE(fillOf(rest, "screen"), hex(p.window));

            const QByteArray hold = DfuTutorialPage::drawingSvg(DfuWay::Hold, p);
            QCOMPARE(fillOf(hold, "menu"), hex(p.accent));
            QCOMPARE(fillOf(hold, "center"), hex(p.accent));
            QCOMPARE(fillOf(hold, "play"), hex(p.border));
            QCOMPARE(fillOf(hold, "screen"), QStringLiteral("#000000"));

            const QByteArray release = DfuTutorialPage::drawingSvg(DfuWay::Release, p);
            QCOMPARE(fillOf(release, "menu"), hex(p.border));
            QCOMPARE(fillOf(release, "center"), hex(p.border));
            QCOMPARE(fillOf(release, "play"), hex(p.border));
            QCOMPARE(fillOf(release, "screen"), QStringLiteral("#000000"));

            const QByteArray wrong = DfuTutorialPage::drawingSvg(DfuWay::WrongButtons, p);
            QCOMPARE(fillOf(wrong, "menu"), hex(p.border));
            QCOMPARE(fillOf(wrong, "center"), hex(p.danger));
            QCOMPARE(fillOf(wrong, "play"), hex(p.danger));
            QCOMPARE(fillOf(wrong, "screen"), hex(p.window));
        }
    }

    // The pulse is an opacity of 100 % and 60 %, only in the way Hold.
    void pulseIsOnlyInTheHoldWay()
    {
        const Palette p = Palette::light();
        QCOMPARE(opacityOf(DfuTutorialPage::drawingSvg(DfuWay::Hold, p, 0.6), "menu"),
                 QStringLiteral("0.6"));
        QCOMPARE(opacityOf(DfuTutorialPage::drawingSvg(DfuWay::Hold, p, 0.6), "center"),
                 QStringLiteral("0.6"));
        QCOMPARE(opacityOf(DfuTutorialPage::drawingSvg(DfuWay::Release, p, 0.6), "menu"),
                 QStringLiteral("1"));
    }

    // The SVG is a real drawing: the wheel is drawn and the screen changes colour.
    void drawingRenders()
    {
        const Palette p = Palette::light();
        auto pixel = [&](DfuWay way) { return pixelsOf(DfuTutorialPage::drawingSvg(way, p)); };
        const QList<QRgb> rest = pixel(DfuWay::Rest);
        const QList<QRgb> hold = pixel(DfuWay::Hold);
        QCOMPARE(rest.size(), 2);
        QCOMPARE(hold.size(), 2);
        QCOMPARE(QColor(rest[0]).name(), p.window.name());
        QCOMPARE(QColor(hold[0]).name(), QStringLiteral("#000000"));
        QCOMPARE(QColor(rest[1]).name(), p.border.name());
        QCOMPARE(QColor(hold[1]).name(), p.accent.name());
    }

    // DFU on time: Start, the stages of section 6.3 one after the other, then the end.
    void dfuOnTime()
    {
        Fixture f({wait(), progress(QStringLiteral("check")), progress(QStringLiteral("eject")),
                   progress(QStringLiteral("spin-down")), progress(QStringLiteral("wait-device")),
                   progress(QStringLiteral("dfu-detected")), progress(QStringLiteral("quiet")),
                   progress(QStringLiteral("send"), 250, 1000),
                   progress(QStringLiteral("send"), 500, 1000),
                   progress(QStringLiteral("resume"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Put the iPod in DFU mode"));
        QCOMPARE(f.page->way(), DfuWay::Rest);
        QVERIFY(f.page->startButton()->isVisible());
        QVERIFY(f.page->startButton()->isEnabled());
        QVERIFY(f.page->findChild<QWidget *>("dfuWarning")->isVisible());
        QVERIFY(label(f.page.get(), "dfuDiskModeWarning")->text().contains("Disk Mode"));
        QCOMPARE(label(f.page.get(), "dfuDiskModeWarning")->property("role").toString(),
                 QStringLiteral("danger"));
        // Nothing has been answered before the click.
        QCOMPARE(int(f.script.answers), 0);

        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        QTRY_COMPARE(int(f.script.answers), 1);
        QVERIFY(!f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Getting the iPod ready"));

        for (const char *stage : {"check", "eject", "spin-down"}) {
            f.next();
            f.settle();
            QCOMPARE(f.page->titleText(), QStringLiteral("Getting the iPod ready"));
            QCOMPARE(f.page->way(), DfuWay::Rest);
            Q_UNUSED(stage)
        }
        QVERIFY(f.page->findChild<QProgressBar *>("stepProgress")->isVisible());
        QCOMPARE(f.page->findChild<QProgressBar *>("stepProgress")->maximum(), 0);

        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Hold MENU and the center button now"));
        QCOMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(fillOf(f.page->currentSvg(), "center"), hex(Palette::light().accent));
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("15"));
        QCOMPARE(label(f.page.get(), "dfuUnder")->text(), QStringLiteral("Keep holding"));

        // The detection saw the DFU mode: let go, with no click.
        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Let go of the buttons now"));
        QCOMPARE(f.page->way(), DfuWay::Release);
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("Sending in 10 s"));
        // `quiet` comes after: nothing is restarted.
        f.clock = 4000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("Sending in 6 s"));
        f.next();
        f.settle();
        QCOMPARE(f.page->titleText(), QStringLiteral("Let go of the buttons now"));
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("Sending in 6 s"));

        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Sending TacetOS to the iPod"));
        QCOMPARE(f.page->way(), DfuWay::Release);
        QCOMPARE(label(f.page.get(), "dfuUnder")->text(), QStringLiteral("Do not unplug the iPod"));
        auto *bar = f.page->findChild<QProgressBar *>("stepProgress");
        QTRY_COMPARE(bar->value(), 250);
        f.next();
        QTRY_COMPARE(bar->value(), 500);

        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("The iPod restarts"));
        QCOMPARE(f.page->way(), DfuWay::Release);
        QTRY_VERIFY(f.page->isDone());
        QVERIFY(!f.page->isFailed());
    }

    // The countdown of the hold: 15 to 0 with the clock of the test, then the longer text.
    void countdownFromFifteenToZero()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.clock = 1000;
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(f.page->countdownSeconds(), 15);
        const struct
        {
            qint64 ms;
            const char *text;
        } steps[] = {{0, "15"}, {999, "15"}, {1000, "14"}, {7500, "8"}, {14999, "1"}};
        for (const auto &s : steps) {
            f.clock = 1000 + s.ms;
            f.page->refreshCountdown();
            QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QString::fromLatin1(s.text));
            QCOMPARE(label(f.page.get(), "dfuUnder")->text(), QStringLiteral("Keep holding"));
        }
        f.clock = 1000 + 15000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("0"));
        QCOMPARE(label(f.page.get(), "dfuUnder")->text(),
                 QStringLiteral("Keep holding. It can take a few more seconds"));
        // It does not go below zero.
        f.clock = 1000 + 40000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("0"));
        QCOMPARE(f.page->countdownSeconds(), 0);
    }

    // Disk Mode instead of DFU: the wrong buttons, the explanation, Start again.
    void diskModeInstead()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device")),
                   progress(QStringLiteral("disk-mode-instead")),
                   wait(QString(error::diskModeInstead)), progress(QStringLiteral("wait-device"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod started in Disk Mode"));
        QCOMPARE(f.page->way(), DfuWay::WrongButtons);
        const Palette p = Palette::light();
        QCOMPARE(fillOf(f.page->currentSvg(), "center"), hex(p.danger));
        QCOMPARE(fillOf(f.page->currentSvg(), "play"), hex(p.danger));
        QCOMPARE(label(f.page.get(), "dfuReason")->text(),
                 QStringLiteral("You held the center button and Play. Hold MENU and the center "
                                "button instead."));
        QVERIFY(f.page->startButton()->isVisible());
        QVERIFY(f.page->startButton()->isEnabled());

        // The step waits again; the page says the same.
        f.next();
        f.waits(2);
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod started in Disk Mode"));
        QCOMPARE(int(f.script.answers), 1);

        // Start again: the new try goes on.
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        QTRY_COMPARE(int(f.script.answers), 2);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(f.page->titleText(), QStringLiteral("Hold MENU and the center button now"));
        QVERIFY(label(f.page.get(), "dfuReason")->isHidden() ||
                !f.page->startButton()->isVisible());
    }

    // Wizard section 9.4: Start before the iPod is there as a disk. The page says why it waits
    // and how to leave DFU mode, then follows the new try by itself; after a while without the
    // disk, the steps and Start again with the reason.
    void startWithoutTheDisk()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-disk")),
                   progress(QStringLiteral("wait-disk")), progress(QStringLiteral("check")),
                   progress(QStringLiteral("wait-device")), wait(QString(error::noDisk))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        QTRY_COMPARE(int(f.script.answers), 1);

        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Waiting for the iPod"));
        QCOMPARE(f.page->way(), DfuWay::Rest);
        QVERIFY(!f.page->isAsking());
        QVERIFY(!f.page->startButton()->isVisible());
        QVERIFY(label(f.page.get(), "dfuUnder")->text().contains("DFU mode"));
        f.next();
        f.settle();
        QCOMPARE(f.page->titleText(), QStringLiteral("Waiting for the iPod"));

        // The disk is back: the try goes on without a click.
        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Getting the iPod ready"));
        QVERIFY(label(f.page.get(), "dfuUnder")->text().isEmpty());
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(int(f.script.answers), 1);

        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod did not come back"));
        QVERIFY(label(f.page.get(), "dfuReason")->text().contains("MENU and the center button"));
        QVERIFY(f.page->startButton()->isEnabled());
    }

    // The helper sees the DFU device: `quiet` alone says to let go, without `dfu-detected`.
    void quietAloneSaysLetGo()
    {
        Fixture f(
            {wait(), progress(QStringLiteral("wait-device")), progress(QStringLiteral("quiet"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Release);
        QCOMPARE(f.page->titleText(), QStringLiteral("Let go of the buttons now"));
    }

    // Detection sees the DFU device before the helper does: the next `wait-device` of the
    // helper does not send the user back to holding the buttons.
    void waitDeviceAfterDfuDetectedKeepsLetGo()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device")),
                   progress(QStringLiteral("dfu-detected")),
                   progress(QStringLiteral("wait-device")), progress(QStringLiteral("quiet"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Release);
        f.next();
        QTest::qWait(200);
        QCOMPARE(f.page->way(), DfuWay::Release);
        QCOMPARE(f.page->titleText(), QStringLiteral("Let go of the buttons now"));
        f.next();
        QTest::qWait(200);
        QCOMPARE(f.page->way(), DfuWay::Release);
    }

    // Nothing arrives: the step gives the help and the steps again, with Start.
    void nothingArrives()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device")),
                   wait(QString(error::dfuFailed), QStringLiteral("Check the cable.")),
                   progress(QStringLiteral("wait-device"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        f.clock = 70000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "dfuCountdown")->text(), QStringLiteral("0"));

        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod did not appear in DFU mode"));
        QCOMPARE(f.page->way(), DfuWay::Rest);
        QCOMPARE(label(f.page.get(), "dfuReason")->text(), QStringLiteral("Check the cable."));
        QVERIFY(label(f.page.get(), "dfuStep3")->isVisible());
        QVERIFY(f.page->startButton()->isEnabled());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        QTRY_COMPARE(int(f.script.answers), 2);
        // The countdown starts again from 15 at the new `wait-device`.
        f.clock = 80000;
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(f.page->countdownSeconds(), 15);
    }

    // Every Start has the order written above it.
    void startHintIsAboveEveryStart()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device")),
                   wait(QString(error::dfuFailed), QStringLiteral("Check the cable."))});
        QLabel *hint = label(f.page.get(), "dfuStartHint");
        QCOMPARE(hint->text(),
                 QStringLiteral("Press Start first, then hold MENU and the center button"));
        QVERIFY(hint->isHidden());
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QVERIFY(hint->isVisible());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QVERIFY(hint->isHidden());
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QVERIFY(hint->isVisible());
    }

    // A try that stopped after the send says the transfer was interrupted, with its text;
    // one that never saw the iPod in DFU mode says so.
    void failureAfterTheSendSaysTheTransferWasInterrupted()
    {
        Event failed =
            wait(QString(error::dfuFailed),
                 QStringLiteral("The transfer was interrupted. The iPod is not changed."));
        failed.failedStage = QStringLiteral("send");
        Fixture f({wait(), progress(QStringLiteral("send"), 10, 100), failed});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->titleText(), QStringLiteral("Sending TacetOS to the iPod"));
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("The transfer to the iPod was interrupted"));
        QCOMPARE(label(f.page.get(), "dfuReason")->text(),
                 QStringLiteral("Nothing was written: the iPod starts Apple's software again. "
                                "Wait for its menu, then press Start to try again."));
        QVERIFY(label(f.page.get(), "dfuStartHint")->isVisible());
    }

    // The iPod left DFU mode three times: its message, and Start asks the window to resume.
    void backInDfuOffersStart()
    {
        Fixture f({wait(), progress(QStringLiteral("send"), 10, 100)});
        f.script.failCode = QString(error::backInDfu);
        QSignalSpy retry(f.page.get(), &DfuTutorialPage::retryRequested);
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_VERIFY(f.page->isFailed());
        QCOMPARE(f.page->titleText(), QStringLiteral("The iPod keeps leaving DFU mode"));
        QVERIFY(f.page->startButton()->isVisible());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        QCOMPARE(retry.count(), 1);
    }

    // The uninstall has its own title, the rest is the same.
    void uninstallTitle()
    {
        Fixture f({wait()}, true);
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QCOMPARE(f.page->titleText(), QStringLiteral("Put the iPod in DFU mode to remove TacetOS"));
    }

    // The pulse: once a second between 100 % and 60 %, not at all with less motion.
    void pulseFollowsTheSystemMotion()
    {
        Fixture f({wait(), progress(QStringLiteral("wait-device"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QCOMPARE(opacityOf(f.page->currentSvg(), "menu"), QStringLiteral("1"));
        f.page->pulseOnce();
        QCOMPARE(opacityOf(f.page->currentSvg(), "menu"), QStringLiteral("0.6"));
        QCOMPARE(opacityOf(f.page->currentSvg(), "center"), QStringLiteral("0.6"));
        QCOMPARE(opacityOf(f.page->currentSvg(), "play"), QStringLiteral("1"));
        f.page->pulseOnce();
        QCOMPARE(opacityOf(f.page->currentSvg(), "menu"), QStringLiteral("1"));

        f.page->setReducedMotion(true);
        QVERIFY(f.page->reducedMotion());
        for (int i = 0; i < 4; ++i) {
            f.page->pulseOnce();
            QCOMPARE(opacityOf(f.page->currentSvg(), "menu"), QStringLiteral("1"));
        }
    }

    // Names: the drawing, the countdown and the buttons can be read by a screen reader.
    void accessibleNames()
    {
        Fixture f(
            {wait(), progress(QStringLiteral("wait-device")), progress(QStringLiteral("quiet"))});
        f.next();
        QTRY_VERIFY(f.page->isAsking());
        auto *drawing = f.page->findChild<QWidget *>("dfuDrawing");
        QVERIFY(drawing);
        QVERIFY(!drawing->accessibleName().isEmpty());
        QCOMPARE(f.page->startButton()->accessibleName(), QStringLiteral("Start"));
        QVERIFY(f.page->findChild<QLabel *>("dfuCountdown"));
        const QString rest = drawing->accessibleName();
        QTest::mouseClick(f.page->startButton(), Qt::LeftButton);
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Hold);
        QVERIFY(drawing->accessibleName() != rest);
        QVERIFY(drawing->accessibleName().contains("hold"));
        f.next();
        QTRY_COMPARE(f.page->way(), DfuWay::Release);
        QVERIFY(drawing->accessibleName().contains("let go"));
        QVERIFY(
            f.page->findChild<QWidget *>("dfuWrongDrawing")->accessibleName().contains("wrong"));
        QVERIFY(f.page->findChild<QProgressBar *>("stepProgress")->accessibleName().size() > 0);
    }
};

QTEST_MAIN(TestDfuTutorial)
#include "tst_dfututorial.moc"
