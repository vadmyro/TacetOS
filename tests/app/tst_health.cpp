// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The Health window and the health part of the card (docs/specs/health.md sections 8, 9 and the
// "Screens" tests of section 10), on the fixtures of tests/fixtures/health and on a temporary
// folder as the iPod volume. Never a real iPod.

#include "constants.h"
#include "home/healthwatcher.h"
#include "home/healthwindow.h"
#include "home/ipodcard.h"
#include "style/stylecontroller.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace tacetos;
using namespace tacetos::device;
using namespace tacetos::health;

namespace {

constexpr HostSystem kHost = HostSystem::Linux;
const QDateTime kReadAt(QDate(2026, 11, 27), QTime(13, 3), QTimeZone::UTC);

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(TACETOS_HEALTH_FIXTURES "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

HealthState stateOf(const QByteArray &health, const QByteArray &charge = {},
                    bool runningOurs = true)
{
    HealthState state;
    state.reading = makeReading(
        health, charge.isEmpty() ? std::nullopt : std::optional<QByteArray>(charge), kReadAt);
    state.runningOurs = runningOurs;
    addReading(&state.history, state.reading);
    return state;
}

// A history whose readings before the last one have these estimates.
void addEstimates(HealthState *state, const QList<double> &estimates)
{
    QList<HistoryReading> earlier;
    for (const double hours : estimates) {
        HistoryReading reading;
        reading.readAt = kReadAt.addDays(-30);
        reading.estimateH = hours;
        reading.healthSha256 = QString::number(earlier.size());
        earlier << reading;
    }
    state->history.readings = earlier + state->history.readings;
}

QList<Model> models()
{
    Model model;
    model.id = QStringLiteral("classic-6g");
    model.name = QStringLiteral("iPod Classic");
    model.family = QStringLiteral("classic");
    return {model};
}

DetectedIpod ipodOn(const QString &mountPoint, FirmwareKind line = FirmwareKind::Tacetos)
{
    DetectedIpod ipod;
    ipod.state = IpodState::Ready;
    ipod.model = QStringLiteral("classic-6g");
    ipod.portPath = QStringLiteral("1-2");
    ipod.serial = QStringLiteral("000A27001A2B3C4D");
    ipod.running = RunningFirmware::Other;
    ipod.firmwareLine.kind = line;
    ipod.firmwareLine.version = QStringLiteral("0.3.0");
    VolumeInfo volume;
    volume.mountPoint = mountPoint;
    volume.totalBytes = 1000;
    volume.freeBytes = 500;
    ipod.volume = volume;
    return ipod;
}

ScreenEntry entryOf(ScreenKind kind, const DetectedIpod &ipod)
{
    ScreenEntry entry;
    entry.portPath = ipod.portPath;
    entry.state.kind = kind;
    entry.ipod = ipod;
    return entry;
}

// The buttons of the card in the order they are laid out, the hidden ones left out.
QList<QPushButton *> shownButtons(IpodCard *card)
{
    QList<QPushButton *> shown;
    QList<QPushButton *> all = card->findChildren<QPushButton *>();
    std::sort(all.begin(), all.end(),
              [](QPushButton *a, QPushButton *b) { return a->x() < b->x(); });
    for (QPushButton *button : std::as_const(all))
        if (button->isVisible())
            shown << button;
    return shown;
}

} // namespace

class TestHealthScreens : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void sourceLines_data();
    void sourceLines();
    void noHealthFile();
    void diskVerdicts_data();
    void diskVerdicts();
    void batteryVerdicts_data();
    void batteryVerdicts();
    void values();
    void perFullCharge();
    void changeSinceEarlier();

    void cardLines_data();
    void cardLines();
    void healthButton();
    void healthButtonOnlyWithUsableFile();
    void lineStaysWithResume();

    void watcherReadsAndKeepsHistory();
    void watcherIgnoresOtherFirmware();
};

void TestHealthScreens::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    StyleController controller;
    controller.apply(Qt::ColorScheme::Light);
    qRegisterMetaType<HomeAction>();
}

void TestHealthScreens::sourceLines_data()
{
    QTest::addColumn<QByteArray>("event");
    QTest::addColumn<QByteArray>("clock");
    QTest::addColumn<bool>("running");
    QTest::addColumn<QString>("line");

    const QString plugged =
        QStringLiteral("Read when the iPod was plugged in, %1.")
            .arg(QLocale().toString(kReadAt.toLocalTime().date(), QStringLiteral("d MMM yyyy")) +
                 QStringLiteral(", ") +
                 QLocale().toString(kReadAt.toLocalTime().time(), QLocale::ShortFormat));
    QTest::newRow("usb, running TacetOS")
        << QByteArray("usb") << QByteArray("2026-11-27 14:03:00") << true << plugged;
    QTest::newRow("usb, running Apple's")
        << QByteArray("usb") << QByteArray("2026-11-27 14:03:00") << false
        << QStringLiteral("From the last time TacetOS ran on this iPod.");
    QTest::newRow("shutdown, clock set")
        << QByteArray("shutdown") << QByteArray("2026-11-20 08:15:00") << true
        << QStringLiteral("Read when the iPod was last switched off, 20 Nov 2026, 08:15.");
    QTest::newRow("shutdown, clock unset")
        << QByteArray("shutdown") << QByteArray("unset") << true
        << QStringLiteral("Read when the iPod was last switched off.");
}

void TestHealthScreens::sourceLines()
{
    QFETCH(QByteArray, event);
    QFETCH(QByteArray, clock);
    QFETCH(bool, running);
    QFETCH(QString, line);

    QByteArray text = fixture("health-good.txt");
    text.replace("event: usb", "event: " + event);
    text.replace("clock: 2026-11-27 14:03:00", "clock: " + clock);
    const HealthState state = stateOf(text, {}, running);
    QVERIFY(state.usable());
    QCOMPARE(healthSourceLine(state), line);

    HealthWindow window(QStringLiteral("iPod Classic"), state);
    QVERIFY(window.sourceLabel());
    QCOMPARE(window.sourceLabel()->text(), line);
    QVERIFY(!window.emptyLabel());
}

void TestHealthScreens::noHealthFile()
{
    for (const QByteArray &health : {QByteArray(), QByteArray("format: 1\nevent: usb\n")}) {
        HealthState state;
        state.reading =
            makeReading(health.isEmpty() ? std::nullopt : std::optional<QByteArray>(health),
                        std::nullopt, kReadAt);
        QVERIFY(!state.usable());
        HealthWindow window(QStringLiteral("iPod Classic"), state);
        QVERIFY(window.emptyLabel());
        QCOMPARE(window.emptyLabel()->text(),
                 QStringLiteral("TacetOS has not written a health reading on this iPod yet. "
                                "Unplug it, then plug it in again while it is on."));
        QVERIFY(!window.sourceLabel());
        QVERIFY(!window.diskVerdictLabel());
        QVERIFY(!window.batteryVerdictLabel());
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("healthName"))->text(),
                 QStringLiteral("iPod Classic"));
        QVERIFY(window.closeButton());
        QCOMPARE(cardHealthLine(state).text, QString());
    }
}

void TestHealthScreens::diskVerdicts_data()
{
    QTest::addColumn<QByteArray>("file");
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("role");
    QTest::addColumn<QString>("model");

    QTest::newRow("good") << QByteArray("health-good.txt") << QStringLiteral("No sign of wear.")
                          << QString() << QStringLiteral("TEST HDD 80GB, hard disk");
    QTest::newRow("watch") << QByteArray("health-watch.txt")
                           << QStringLiteral("Some sectors were replaced or could not be read. "
                                             "Keep a copy of your music on the computer.")
                           << QString() << QStringLiteral("TEST HDD 80GB, hard disk");
    QTest::newRow("failing") << QByteArray("health-failing.txt")
                             << QStringLiteral(
                                    "The disk is failing. Copy your music to the computer soon.")
                             << QStringLiteral("danger")
                             << QStringLiteral("TEST HDD 80GB, hard disk");
    QTest::newRow("unknown, flash")
        << QByteArray("health-flash.txt") << QStringLiteral("Flash storage gives no wear data.")
        << QStringLiteral("secondary") << QStringLiteral("TEST HDD 80GB, flash storage");
    QTest::newRow("unknown, unsupported")
        << QByteArray("health-unsupported.txt") << QStringLiteral("This disk gives no health data.")
        << QStringLiteral("secondary") << QStringLiteral("TEST HDD 80GB, hard disk");
}

void TestHealthScreens::diskVerdicts()
{
    QFETCH(QByteArray, file);
    QFETCH(QString, line);
    QFETCH(QString, role);
    QFETCH(QString, model);

    const HealthState state = stateOf(fixture(file.constData()));
    QCOMPARE(diskVerdictLine(state).text, line);
    HealthWindow window(QStringLiteral("iPod Classic"), state);
    QCOMPARE(window.diskVerdictLabel()->text(), line);
    QCOMPARE(window.diskVerdictLabel()->property("role").toString(), role);
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("diskModel"))->text(), model);
}

void TestHealthScreens::batteryVerdicts_data()
{
    QTest::addColumn<QList<double>>("estimates");
    QTest::addColumn<QByteArray>("charging");
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("role");

    QTest::newRow("unknown, 2 of 6")
        << QList<double>{20.0, 21.0} << QByteArray("no")
        << QStringLiteral("Not enough full charges measured yet (2 of 6).")
        << QStringLiteral("secondary");
    QTest::newRow("good") << QList<double>{20.0, 21.0, 19.0, 20.0, 20.5, 19.5} << QByteArray("no")
                          << QStringLiteral("The battery holds its charge.") << QString();
    QTest::newRow("worn") << QList<double>{20.0, 21.0, 19.0, 9.0, 10.0, 8.0} << QByteArray("no")
                          << QStringLiteral("The battery lasts about half as long as it used to.")
                          << QStringLiteral("danger");
    QTest::newRow("watch") << QList<double>{20.0, 21.0, 19.0, 9.0, 10.0, 8.0} << QByteArray("error")
                           << QStringLiteral("The charger reported an error at the last reading. "
                                             "Unplug the iPod and plug it in again.")
                           << QString();
}

void TestHealthScreens::batteryVerdicts()
{
    QFETCH(QList<double>, estimates);
    QFETCH(QByteArray, charging);
    QFETCH(QString, line);
    QFETCH(QString, role);

    QByteArray text = fixture("health-good.txt");
    text.replace("charging: no", "charging: " + charging);
    HealthState state = stateOf(text);
    addEstimates(&state, estimates);
    QCOMPARE(batteryVerdictLine(state).text, line);
    HealthWindow window(QStringLiteral("iPod Classic"), state);
    QCOMPARE(window.batteryVerdictLabel()->text(), line);
    QCOMPARE(window.batteryVerdictLabel()->property("role").toString(), role);
}

void TestHealthScreens::values()
{
    HealthWindow window(QStringLiteral("iPod Classic"), stateOf(fixture("health-good.txt")));
    QCOMPARE(window.value(QStringLiteral("Reallocated sectors")), QStringLiteral("0"));
    QCOMPARE(window.value(QStringLiteral("Pending sectors")), QStringLiteral("0"));
    QCOMPARE(window.value(QStringLiteral("Uncorrectable sectors")), QStringLiteral("0"));
    QCOMPARE(window.value(QStringLiteral("Spin-up retries")), QStringLiteral("0"));
    QCOMPARE(window.value(QStringLiteral("Temperature")), QStringLiteral("31 °C"));
    QCOMPARE(window.value(QStringLiteral("Power-on time")), QStringLiteral("2,140 h"));
    QCOMPARE(window.value(QStringLiteral("Power cycles")), QStringLiteral("1,873"));
    QCOMPARE(window.value(QStringLiteral("Head load cycles")), QStringLiteral("52,310"));
    QCOMPARE(window.value(QStringLiteral("Drop sensor events")), QStringLiteral("41"));
    QCOMPARE(window.value(QStringLiteral("Level")), QStringLiteral("74 % (3.91 V)"));
    QCOMPARE(window.value(QStringLiteral("On battery since the last charge")),
             QStringLiteral("5 h 12 min"));
    QCOMPARE(window.value(QStringLiteral("Longest on one charge")), QStringLiteral("31 h 40 min"));
    // No estimate: the row is hidden.
    QCOMPARE(window.value(QStringLiteral("Per full charge")), QString());

    // Without SMART no attribute is shown.
    HealthWindow older(QStringLiteral("iPod Video"), stateOf(fixture("health-unsupported.txt")));
    QCOMPARE(older.value(QStringLiteral("Reallocated sectors")), QString());
    QCOMPARE(older.value(QStringLiteral("Temperature")), QString());
    QCOMPARE(older.value(QStringLiteral("Level")), QStringLiteral("74 % (3.91 V)"));
}

void TestHealthScreens::perFullCharge()
{
    // A full charge and a drop of 60 points over 12 h: 20.0 h per full charge.
    QByteArray text = fixture("health-good.txt");
    text.replace("battery_level: 74", "battery_level: 40");
    text.replace("runtime_s: 18720", "runtime_s: 43200");
    HealthState one = stateOf(text, fixture("charge-100.txt"));
    QCOMPARE(one.history.estimates(), QList<double>{20.0});
    HealthWindow alone(QStringLiteral("iPod Classic"), one);
    QCOMPARE(alone.value(QStringLiteral("Per full charge")), QStringLiteral("about 20.0 h"));

    HealthState three = one;
    addEstimates(&three, {30.0, 10.0});
    HealthWindow median(QStringLiteral("iPod Classic"), three);
    QCOMPARE(median.value(QStringLiteral("Per full charge")),
             QStringLiteral("about 20.0 h, median of the last 3 charges"));
}

void TestHealthScreens::changeSinceEarlier()
{
    HealthState state = stateOf(fixture("health-watch.txt"));
    HistoryReading before;
    before.readAt = QDateTime(QDate(2026, 11, 12), QTime(12, 0), QTimeZone::UTC);
    before.smart = QMap<int, quint32>{{5, 0}, {197, 0}, {198, 0}, {10, 0}};
    before.healthSha256 = QStringLiteral("earlier");
    HistoryReading same = before;
    same.readAt = QDateTime(QDate(2026, 11, 20), QTime(12, 0), QTimeZone::UTC);
    same.smart = QMap<int, quint32>{{5, 3}, {197, 0}, {198, 0}, {10, 0}};
    same.healthSha256 = QStringLiteral("same");
    state.history.readings.prepend(same);
    state.history.readings.prepend(before);

    HealthWindow window(QStringLiteral("iPod Classic"), state);
    // The reading of 20 Nov had the same value: the change is counted from 12 Nov.
    QCOMPARE(window.value(QStringLiteral("Reallocated sectors")),
             QStringLiteral("3 (+3 since 12 Nov)"));
    // No earlier reading with another value.
    QCOMPARE(window.value(QStringLiteral("Pending sectors")), QStringLiteral("0"));
}

void TestHealthScreens::cardLines_data()
{
    QTest::addColumn<QByteArray>("file");
    QTest::addColumn<QByteArray>("charging");
    QTest::addColumn<QList<double>>("estimates");
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("role");

    const QList<double> worn{20.0, 21.0, 19.0, 9.0, 10.0, 8.0};
    QTest::newRow("disk failing, over a worn battery")
        << QByteArray("health-failing.txt") << QByteArray("no") << worn
        << QStringLiteral("Disk failing: copy your music soon.") << QStringLiteral("danger");
    QTest::newRow("battery worn, over a disk to watch")
        << QByteArray("health-watch.txt") << QByteArray("no") << worn
        << QStringLiteral("Battery worn.") << QStringLiteral("danger");
    QTest::newRow("disk watch, over a charger error")
        << QByteArray("health-watch.txt") << QByteArray("error") << QList<double>{}
        << QStringLiteral("Disk: some sectors replaced.") << QStringLiteral("secondary");
    QTest::newRow("charger error")
        << QByteArray("health-good.txt") << QByteArray("error") << QList<double>{}
        << QStringLiteral("Charger error at the last reading.") << QStringLiteral("secondary");
    QTest::newRow("good") << QByteArray("health-good.txt") << QByteArray("no")
                          << QList<double>{20.0, 21.0, 19.0, 20.0, 20.5, 19.5} << QString()
                          << QString();
    QTest::newRow("unknown") << QByteArray("health-unsupported.txt") << QByteArray("no")
                             << QList<double>{} << QString() << QString();
}

void TestHealthScreens::cardLines()
{
    QFETCH(QByteArray, file);
    QFETCH(QByteArray, charging);
    QFETCH(QList<double>, estimates);
    QFETCH(QString, line);
    QFETCH(QString, role);

    QByteArray text = fixture(file.constData());
    text.replace("charging: no", "charging: " + charging);
    HealthState state = stateOf(text);
    addEstimates(&state, estimates);
    const HealthLine healthLine = cardHealthLine(state);
    QCOMPARE(healthLine.text, line);

    IpodCard card;
    card.setEntry(entryOf(ScreenKind::Ready, ipodOn(QStringLiteral("/media/IPOD"))), models(),
                  kHost);
    card.setHealth(true, healthLine);
    card.show();
    QCOMPARE(card.healthLabel()->isVisible(), !line.isEmpty());
    if (!line.isEmpty()) {
        QCOMPARE(card.healthLabel()->text(), line);
        QCOMPARE(card.healthLabel()->property("role").toString(), role);
    }
    QVERIFY(card.healthButton()->isVisible());
}

void TestHealthScreens::healthButton()
{
    IpodCard card;
    card.setEntry(entryOf(ScreenKind::Ready, ipodOn(QStringLiteral("/media/IPOD"))), models(),
                  kHost);
    card.setHealth(true);
    card.show();

    // Right after Show files, secondary, and the main button stays the wizard's.
    const QList<QPushButton *> buttons = shownButtons(&card);
    const qsizetype open = buttons.indexOf(card.openButton());
    QVERIFY(open >= 0);
    QCOMPARE(buttons.value(open + 1), card.healthButton());
    QCOMPARE(card.healthButton()->text(), QStringLiteral("Health"));
    QCOMPARE(card.healthButton()->property("role").toString(), QStringLiteral("secondary"));
    QCOMPARE(card.wizardButton()->property("role").toString(), QStringLiteral("primary"));

    // With an update, Update is the main one.
    card.setUpdate(QStringLiteral("0.4.0"));
    QCOMPARE(card.wizardButton()->text(), QStringLiteral("Update"));
    QCOMPARE(card.healthButton()->property("role").toString(), QStringLiteral("secondary"));

    QSignalSpy asked(&card, &IpodCard::actionRequested);
    card.healthButton()->click();
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.at(0).at(0).value<HomeAction>(), HomeAction::Health);

    // Busy: shown and disabled, as every button.
    DetectedIpod ipod = ipodOn(QStringLiteral("/media/IPOD"));
    card.setEntry(entryOf(ScreenKind::Busy, ipod), models(), kHost);
    QVERIFY(card.healthButton()->isVisible());
    QVERIFY(!card.healthButton()->isEnabled());

    // The wizard window is open: the card has only "Show the wizard".
    card.setEntry(entryOf(ScreenKind::Ready, ipod), models(), kHost);
    card.setWizardState(false, true);
    QVERIFY(!card.healthButton()->isVisible());
}

void TestHealthScreens::healthButtonOnlyWithUsableFile()
{
    IpodCard card;
    card.setEntry(entryOf(ScreenKind::Ready, ipodOn(QStringLiteral("/media/IPOD"))), models(),
                  kHost);
    card.show();
    QVERIFY(!card.healthButton()->isVisible());
    card.setHealth(false, {QStringLiteral("Battery worn."), HealthLine::Style::Danger});
    QVERIFY(!card.healthButton()->isVisible());
    QVERIFY(!card.healthLabel()->isVisible());

    // A firmware that is not TacetOS: nothing, even with a reading.
    for (const FirmwareKind line : {FirmwareKind::Apple, FirmwareKind::Custom}) {
        card.setEntry(entryOf(ScreenKind::Ready, ipodOn(QStringLiteral("/media/IPOD"), line)),
                      models(), kHost);
        card.setHealth(true, {QStringLiteral("Battery worn."), HealthLine::Style::Danger});
        QVERIFY(!card.healthButton()->isVisible());
        QVERIFY(!card.healthLabel()->isVisible());
    }
}

void TestHealthScreens::lineStaysWithResume()
{
    IpodCard card;
    card.setEntry(entryOf(ScreenKind::Ready, ipodOn(QStringLiteral("/media/IPOD"))), models(),
                  kHost);
    card.setUpdate(QStringLiteral("0.4.0"));
    card.setHealth(true, {QStringLiteral("Battery worn."), HealthLine::Style::Danger});
    card.setWizardState(true, false);
    card.show();
    QCOMPARE(card.wizardButton()->text(), QStringLiteral("Resume"));
    QVERIFY(!card.updateLabel()->isVisible());
    QVERIFY(card.healthLabel()->isVisible());
    QVERIFY(card.healthButton()->isVisible());
    QCOMPARE(card.healthButton()->property("role").toString(), QStringLiteral("secondary"));
}

void TestHealthScreens::watcherReadsAndKeepsHistory()
{
    QTemporaryDir volume;
    QTemporaryDir data;
    QVERIFY(volume.isValid() && data.isValid());
    const QString folder = volume.path() + QLatin1Char('/') + QString(kFirmwareFolder);
    QVERIFY(QDir().mkpath(folder));
    {
        QFile file(folder + QLatin1Char('/') + QString(kHealthFile));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fixture("health-watch.txt"));
    }

    HealthWatcher watcher(data.path(), nullptr);
    QSignalSpy changed(&watcher, &HealthWatcher::healthChanged);
    const DetectedIpod ipod = ipodOn(volume.path());
    watcher.show({entryOf(ScreenKind::Ready, ipod)}, models());
    QCOMPARE(watcher.reads(), 1);
    QCOMPARE(changed.size(), 1);
    std::optional<HealthState> state = watcher.stateFor(ipod.portPath);
    QVERIFY(state && state->usable());
    QCOMPARE(state->disk(), DiskVerdict::Watch);
    QCOMPARE(cardHealthLine(*state).text, QStringLiteral("Disk: some sectors replaced."));

    // The same list again reads nothing; a busy card keeps what was read.
    watcher.show({entryOf(ScreenKind::Ready, ipod)}, models());
    watcher.show({entryOf(ScreenKind::Busy, ipod)}, models());
    QCOMPARE(watcher.reads(), 1);
    QVERIFY(watcher.stateFor(ipod.portPath));

    // Refresh reads again; the same file is one reading of the history.
    watcher.refresh();
    QCOMPARE(watcher.reads(), 2);
    state = watcher.stateFor(ipod.portPath);
    QCOMPARE(state->history.readings.size(), 1);
    const QString key = historyKey(state->reading, ipod.serial);
    QVERIFY(QFile::exists(historyPath(data.path(), key)));

    // Gone from `ready`: nothing is known any more.
    watcher.show({}, models());
    QVERIFY(!watcher.stateFor(ipod.portPath));
    QCOMPARE(changed.size(), 3);
}

void TestHealthScreens::watcherIgnoresOtherFirmware()
{
    QTemporaryDir volume;
    QVERIFY(volume.isValid());
    HealthWatcher watcher(QString(), nullptr);

    // Apple's firmware line: the files are never read.
    watcher.show({entryOf(ScreenKind::Ready, ipodOn(volume.path(), FirmwareKind::Apple))},
                 models());
    QCOMPARE(watcher.reads(), 0);

    // TacetOS with no file: read, but nothing usable.
    watcher.show({entryOf(ScreenKind::Ready, ipodOn(volume.path()))}, models());
    QCOMPARE(watcher.reads(), 1);
    const std::optional<HealthState> state = watcher.stateFor(QStringLiteral("1-2"));
    QVERIFY(state);
    QVERIFY(!state->usable());
}

QTEST_MAIN(TestHealthScreens)
#include "tst_health.moc"
