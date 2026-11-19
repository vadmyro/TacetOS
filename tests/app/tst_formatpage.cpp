// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "runner.h"
#include "wizard/backuppage.h"
#include "wizard/formatpage.h"
#include "wizarderrors.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <memory>

using namespace tacetos;
using namespace tacetos::wizard;

namespace {

// What the test step does, read and written from both threads.
struct Script
{
    // 0: waiting for the name, 1: the name came, 2: let it write, 3: let it wait for the volume,
    // 4: let it end.
    std::atomic<int> gate{0};
    std::atomic<bool> answered{false};
    std::atomic<bool> cancelled{false};
    QString typed;
    QString failCode;
    QString ipodName = QStringLiteral("JOHN'S IPOD");
    int files = 1234;
    double bytes = 71.0e9;
    bool classic = false;
};

class FormatLikeStep : public Step
{
public:
    explicit FormatLikeStep(Script &script) : script_(script) {}

    StepOutcome run(StepContext &context) override
    {
        QJsonObject info;
        info.insert(QStringLiteral("ipodName"), script_.ipodName);
        info.insert(QStringLiteral("files"), script_.files);
        info.insert(QStringLiteral("bytes"), script_.bytes);
        const UserAnswer answer = context.waitForUser(WaitKind::TypedName, info);
        if (answer.cancelled) {
            script_.cancelled = true;
            return StepOutcome::cancelled();
        }
        script_.typed = answer.text;
        script_.answered = true;
        if (!until(2, context))
            return StepOutcome::cancelled();
        if (script_.classic) {
            context.firstWrite();
            context.progress(QStringLiteral("erase"), 3, 10);
        } else {
            context.progress(QStringLiteral("check"), 0, 0);
            context.firstWrite();
            context.progress(QStringLiteral("write"), 250, 1000);
        }
        if (!until(3, context))
            return StepOutcome::cancelled();
        context.progress(QStringLiteral("volume"), 0, 0);
        if (!until(4, context))
            return StepOutcome::cancelled();
        if (!script_.failCode.isEmpty())
            return StepOutcome::failed(script_.failCode);
        return StepOutcome::done();
    }

private:
    bool until(int gate, StepContext &context)
    {
        while (script_.gate < gate) {
            if (context.stopRequested())
                return false;
            QThread::msleep(5);
        }
        return true;
    }

    Script &script_;
};

class DoneStep : public Step
{
public:
    StepOutcome run(StepContext &) override { return StepOutcome::done(); }
};

QLineEdit *field(QWidget *page)
{
    return page->findChild<QLineEdit *>(QStringLiteral("typedNameField"));
}

QLabel *label(QWidget *page, const QString &name)
{
    return page->findChild<QLabel *>(name);
}

} // namespace

class TestFormatPage : public QObject
{
    Q_OBJECT

private:
    struct Fixture
    {
        QTemporaryDir dir;
        Script script;
        qint64 clock = 0;
        std::unique_ptr<WizardRunner> runner;
        std::unique_ptr<FormatPage> page;
        FlowShape shape;
        QString stepName;

        explicit Fixture(bool convert = false, bool classic = false)
            : stepName(convert ? QStringLiteral("convert") : QStringLiteral("format"))
        {
            script.classic = classic;
            shape.kind = FlowKind::Install;
            shape.path = InstallPath::Clean;
            shape.family = classic ? Family::Dfu : Family::Partition;
            shape.macFormat = convert;
            runner = std::make_unique<WizardRunner>(
                dir.path(), [this](const QString &name) -> std::unique_ptr<Step> {
                    if (name == stepName)
                        return std::make_unique<FormatLikeStep>(script);
                    return std::make_unique<DoneStep>();
                });
            page = std::make_unique<FormatPage>(runner.get());
            page->setClock([this] { return clock; });
            page->setBackupFolder(QStringLiteral("/home/anna/Backups/ABC123/2026-11-18_201430"));
            page->show();
            WizardRunner::Start s;
            s.serial = QStringLiteral("ABC123");
            s.model = QStringLiteral("video-5g");
            s.shape = shape;
            s.packageManifest = QStringLiteral("/p/m.json");
            QCOMPARE(runner->start(s), QString());
        }

        ~Fixture()
        {
            script.gate = 4;
            runner->cancel();
            runner->waitFinished(5000);
        }

        void type(const QString &text) { field(page.get())->setText(text); }
        bool asking() { return page->isAsking(); }
        bool confirmOn() { return page->confirmButton()->isEnabled(); }
    };

private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }

    void confirmationShowsWhatIsErased()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        QCOMPARE(label(f.page.get(), "formatTitle")->text(),
                 QStringLiteral("All music on this iPod will be erased"));
        const QString summary = label(f.page.get(), "formatSummary")->text();
        QVERIFY(summary.contains(QLocale().toString(1234) + QStringLiteral(" files")));
        QVERIFY(summary.contains(backupSizeText(71000000000LL)));
        QVERIFY(label(f.page.get(), "formatBackupFolder")
                    ->text()
                    .contains(QStringLiteral("2026-11-18_201430")));
        QCOMPARE(label(f.page.get(), "typedNameExpected")->text(), QStringLiteral("JOHN'S IPOD"));
        QVERIFY(label(f.page.get(), "typedNameExpected")->textInteractionFlags() &
                Qt::TextSelectableByMouse);
        QVERIFY(field(f.page.get())->text().isEmpty());
        QVERIFY(field(f.page.get())->hasFocus() || field(f.page.get())->isVisible());
        QVERIFY(!f.confirmOn());
        QCOMPARE(f.page->confirmButton()->text(), QStringLiteral("Erase the iPod"));
        QCOMPARE(f.page->confirmButton()->property("role").toString(), QStringLiteral("danger"));
        // A format has no note about Mac iPods.
        QVERIFY(label(f.page.get(), "formatConvertNote")->isHidden());
    }

    void wrongTextKeepsTheButtonOff()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        for (const QString &text :
             {QString(), QStringLiteral("john's ipod"), QStringLiteral("JOHN'S"),
              QStringLiteral("ANNA'S IPOD"), QStringLiteral("   ")}) {
            f.type(text);
            QVERIFY2(!f.confirmOn(), qPrintable(text));
        }
        // Enter on a wrong text sends nothing.
        f.type(QStringLiteral("john's ipod"));
        QTest::keyClick(field(f.page.get()), Qt::Key_Return);
        QTest::qWait(50);
        QVERIFY(!f.script.answered);
        QVERIFY(f.asking());
    }

    void rightTextSendsTheTypedText()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        f.type(QStringLiteral("  JOHN'S IPOD "));
        QVERIFY(f.confirmOn());
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        QTRY_VERIFY(f.script.answered);
        QCOMPARE(f.script.typed, QStringLiteral("  JOHN'S IPOD "));
        // Nothing can be clicked or typed any more.
        QVERIFY(!f.confirmOn());
        QVERIFY(!field(f.page.get())->isEnabled());
        QVERIFY(!f.page->isAsking());
        QCOMPARE(label(f.page.get(), "formatTitle")->text(), QStringLiteral("Erasing the iPod"));
    }

    void enterPressesTheButtonWhenOn()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::keyClick(field(f.page.get()), Qt::Key_Return);
        QTRY_VERIFY(f.script.answered);
        QCOMPARE(f.script.typed, QStringLiteral("JOHN'S IPOD"));
    }

    void emptyNameNeverMatches()
    {
        Fixture f;
        f.script.ipodName.clear();
        QTRY_VERIFY(f.asking());
        f.type(QString());
        QVERIFY(!f.confirmOn());
        f.type(QStringLiteral("  "));
        QVERIFY(!f.confirmOn());
    }

    void conversionTellsAboutMac()
    {
        Fixture f(true);
        QTRY_VERIFY(f.asking());
        QVERIFY(!label(f.page.get(), "formatConvertNote")->isHidden());
        QVERIFY(label(f.page.get(), "formatConvertNote")->text().contains(QStringLiteral("Mac")));
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        QTRY_VERIFY(f.script.answered);
        QCOMPARE(label(f.page.get(), "formatTitle")->text(), QStringLiteral("Converting the iPod"));
    }

    void closingIsRefusedDuringTheWrite()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        // While it waits for the name, the page closes.
        QVERIFY(f.page->closeRefusal().isEmpty());
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        QTRY_VERIFY(f.script.answered);
        // The answer is sent, the first write is not made yet.
        QVERIFY(!f.page->isWriting());
        f.script.gate = 2;
        QTRY_VERIFY(f.page->isWriting());
        QCOMPARE(f.page->closeRefusal(),
                 QStringLiteral("TacetOS is writing to the iPod. Wait for the end of this step, "
                                "then close"));
        QVERIFY(!f.page->close());
        QVERIFY(f.page->isVisible());
        QCOMPARE(label(f.page.get(), "formatStage")->text(), QStringLiteral("Writing"));
        QCOMPARE(f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"))->value(), 250);
        // Done: the page can close again.
        f.script.gate = 4;
        QTRY_VERIFY(f.page->isDone());
        QVERIFY(f.page->closeRefusal().isEmpty());
        QVERIFY(f.page->close());
    }

    void closingIsAllowedWhileWaiting()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        QVERIFY(f.page->close());
        QVERIFY(!f.page->isVisible());
    }

    void volumeWaitShowsTheSecondsLeft()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        f.script.gate = 3;
        QTRY_COMPARE(label(f.page.get(), "formatStage")->text(),
                     QStringLiteral("Waiting for the iPod to come back: 30 s"));
        f.clock = 20 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "formatStage")->text(),
                 QStringLiteral("Waiting for the iPod to come back: 10 s"));
        f.clock = 45 * 1000;
        f.page->refreshCountdown();
        QCOMPARE(label(f.page.get(), "formatStage")->text(),
                 QStringLiteral("Waiting for the iPod to come back: 0 s"));
    }

    void classicShowsTheCountOfEntries()
    {
        Fixture f(false, true);
        QTRY_VERIFY(f.asking());
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        f.script.gate = 2;
        QTRY_COMPARE(label(f.page.get(), "formatStage")->text(),
                     QStringLiteral("Erasing the files"));
        QCOMPARE(label(f.page.get(), "stepProgressText")->text(),
                 QStringLiteral("3 of 10 entries deleted"));
        QCOMPARE(f.page->findChild<QProgressBar *>(QStringLiteral("stepProgress"))->value(), 300);
    }

    void failureShowsItsMessage()
    {
        Fixture f;
        f.script.failCode = QString(error::volumeNotBack);
        QTRY_VERIFY(f.asking());
        f.type(QStringLiteral("JOHN'S IPOD"));
        QTest::mouseClick(f.page->confirmButton(), Qt::LeftButton);
        f.script.gate = 4;
        QTRY_VERIFY(f.page->isFailed());
        QVERIFY(!label(f.page.get(), "formatBanner")->isHidden());
        QVERIFY(label(f.page.get(), "formatBanner")
                    ->text()
                    .contains(QStringLiteral("did not come back")));
        QVERIFY(!f.page->isWriting());
    }

    void cancelWhileWaitingStops()
    {
        Fixture f;
        QTRY_VERIFY(f.asking());
        f.runner->cancel();
        QTRY_VERIFY(f.page->isStopped());
        QVERIFY(!f.page->isAsking());
        QVERIFY(!f.page->isWriting());
        QVERIFY(f.script.cancelled);
        QVERIFY(!f.script.answered);
    }
};

QTEST_MAIN(TestFormatPage)
#include "tst_formatpage.moc"
