// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

// The home screen and the card of an iPod (docs/specs/home-screen.md section 11.2), with
// entries written by hand: no iPod, no system.

#include "home/homescreen.h"
#include "home/ipodcard.h"
#include "style/palette.h"
#include "style/stylecontroller.h"

#include <QApplication>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace tacetos;
using namespace tacetos::device;

Q_DECLARE_METATYPE(tacetos::device::ScreenKind)
Q_DECLARE_METATYPE(tacetos::device::OperationKind)
Q_DECLARE_METATYPE(tacetos::device::DiskError)
Q_DECLARE_METATYPE(tacetos::device::IpodState)

namespace {

constexpr HostSystem kHost = HostSystem::Linux;

QList<Model> models()
{
    QList<Model> list;
    for (const auto &[id, name] : {std::pair{"classic-6g", "iPod Classic"},
                                   std::pair{"nano-1g", "iPod nano (1st generation)"},
                                   std::pair{"ipod-video", "iPod Video"}}) {
        Model model;
        model.id = QString::fromLatin1(id);
        model.name = QString::fromLatin1(name);
        list << model;
    }
    return list;
}

DetectedIpod ipodIn(IpodState state, const QString &model = QStringLiteral("classic-6g"))
{
    DetectedIpod ipod;
    ipod.state = state;
    ipod.model = model;
    ipod.portPath = QStringLiteral("1-2");
    ipod.firmwareLine.kind = FirmwareKind::Tacetos;
    ipod.firmwareLine.version = QStringLiteral("0.1");
    return ipod;
}

ScreenEntry entryOf(const QString &port, ScreenKind kind, std::optional<DetectedIpod> ipod)
{
    ScreenEntry entry;
    entry.portPath = port;
    entry.state.kind = kind;
    entry.ipod = ipod;
    return entry;
}

QRgb pixelOf(QWidget *widget)
{
    return widget->grab().toImage().pixelColor(widget->width() / 2, widget->height() / 2).rgb();
}

} // namespace

class TestHome : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void stateCards_data();
    void stateCards();
    void busyCard();
    void spaceLabel();
    void spaceHidden_data();
    void spaceHidden();
    void otherModelOtherDrawing();
    void unknownModelIsGeneric();
    void nameFromVariantModelOrDefault();
    void firmwareLineHiddenWhenEmpty();
    void releasedCardKeepsTheName();
    void accessibleNames();
    void zeroOneThreeIpods();
    void columnIsCentredAndCapped();
    void detectionComing();
    void headerAndMark();
    void buttonSignals();
    void refreshSignal();
};

void TestHome::initTestCase()
{
    QLocale::setDefault(QLocale::c());
    StyleController controller;
    controller.apply(Qt::ColorScheme::Light);
    qRegisterMetaType<HomeAction>();
}

void TestHome::stateCards_data()
{
    QTest::addColumn<ScreenKind>("kind");
    QTest::addColumn<OperationKind>("operation");
    QTest::addColumn<DiskError>("error");
    QTest::addColumn<IpodState>("reason");
    QTest::addColumn<bool>("hasIpod");
    QTest::addColumn<IpodState>("detection");
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("banner");
    QTest::addColumn<QString>("dot");
    QTest::addColumn<bool>("buttons");
    QTest::addColumn<QString>("openText");
    QTest::addColumn<bool>("enabled");

    using K = ScreenKind;
    using O = OperationKind;
    using E = DiskError;
    using S = IpodState;
    const QString none;
    const auto row = [&](const char *name, K kind, O op, E error, S reason, bool hasIpod,
                         S detection, const QString &message, const QString &banner,
                         const char *dot, bool buttons, const QString &openText, bool enabled) {
        QTest::newRow(name) << kind << op << error << reason << hasIpod << detection << message
                            << banner << QString::fromLatin1(dot) << buttons << openText << enabled;
    };
    const QString notMounted = QStringLiteral(
        "The iPod storage is there but is not open on this computer. TacetOS can open it.");

    row("unplugged", K::Unplugged, O::Mount, E::None, S::Ready, false, S::Ready,
        QStringLiteral("Connect your iPod."), none, "dot-secondary", false, none, true);
    row("reading", K::Reading, O::Mount, E::None, S::Ready, true, S::Waiting,
        QStringLiteral("Reading the iPod..."), none, "dot-secondary", false, none, true);
    row("ready", K::Ready, O::Mount, E::None, S::Ready, true, S::Ready, QStringLiteral("Ready."),
        none, "dot-accent", true, QStringLiteral("Open"), true);
    row("not-mounted", K::NotMounted, O::Mount, E::None, S::Ready, true, S::NoVolume, notMounted,
        none, "dot-secondary", true, QStringLiteral("Open"), true);
    row("dfu", K::Dfu, O::Mount, E::None, S::Ready, true, S::Dfu,
        QStringLiteral("The iPod is in DFU mode."), none, "dot-secondary", false, none, true);
    row("rebooting", K::Rebooting, O::Mount, E::None, S::Ready, true, S::Ready,
        QStringLiteral("The iPod is restarting. Wait..."), none, "dot-secondary", false, none,
        true);
    row("busy", K::Busy, O::Mount, E::None, S::Ready, true, S::NoVolume,
        QStringLiteral("Opening the iPod..."), none, "dot-secondary", true, QStringLiteral("Open"),
        false);
    row("done", K::Done, O::Mount, E::None, S::Ready, true, S::Ready, QStringLiteral("Ready."),
        QStringLiteral("The iPod is open."), "dot-accent", false, none, true);
    row("failed", K::Failed, O::Mount, E::Busy, S::Ready, true, S::NoVolume, notMounted,
        QStringLiteral("Another program is using the iPod. Close it, then try again."),
        "dot-danger", true, QStringLiteral("Open"), true);
    row("released", K::Released, O::Eject, E::None, S::Ready, false, S::Ready,
        QStringLiteral("You can unplug the iPod now."), none, "dot-secondary", false, none, true);
    row("attention-mac-format", K::Attention, O::Mount, E::None, S::MacFormat, true, S::MacFormat,
        stateMessage(ipodIn(S::MacFormat), models(), kHost), none, "dot-danger", true,
        QStringLiteral("Open read only"), true);
    row("attention-unsupported", K::Attention, O::Mount, E::None, S::UnsupportedModel, true,
        S::UnsupportedModel, QStringLiteral("This iPod is not supported."), none, "dot-danger",
        false, none, true);
    row("disk-mode", K::DiskMode, O::Mount, E::None, S::Ready, true, S::Ready,
        QStringLiteral("iPod Classic"), none, "dot-secondary", false, none, true);
}

void TestHome::stateCards()
{
    QFETCH(ScreenKind, kind);
    QFETCH(OperationKind, operation);
    QFETCH(DiskError, error);
    QFETCH(IpodState, reason);
    QFETCH(bool, hasIpod);
    QFETCH(IpodState, detection);
    QFETCH(QString, message);
    QFETCH(QString, banner);
    QFETCH(QString, dot);
    QFETCH(bool, buttons);
    QFETCH(QString, openText);
    QFETCH(bool, enabled);

    ScreenEntry entry = entryOf(QStringLiteral("1-2"), kind,
                                hasIpod ? std::optional(ipodIn(detection)) : std::nullopt);
    entry.state.operation = operation;
    entry.state.error = error;
    entry.state.reason = reason;

    IpodCard card;
    card.setEntry(entry, models(), kHost);
    card.resize(700, 300);
    card.show();
    QVERIFY(QTest::qWaitForWindowExposed(&card));

    QCOMPARE(card.findChild<QLabel *>("ipodState")->text(), message);
    QCOMPARE(card.findChild<QWidget *>("stateDot")->property("role").toString(), dot);

    // The dot is painted with a colour of the palette.
    const Palette palette = Palette::light();
    const QColor expected = dot == "dot-accent"   ? palette.accent
                            : dot == "dot-danger" ? palette.danger
                                                  : palette.textSecondary;
    QCOMPARE(pixelOf(card.findChild<QWidget *>("stateDot")), expected.rgb());

    QCOMPARE(card.ejectButton()->isVisibleTo(&card), buttons);
    QCOMPARE(card.openButton()->isVisibleTo(&card), buttons);
    if (buttons) {
        QCOMPARE(card.openButton()->text(), openText);
        QCOMPARE(card.ejectButton()->isEnabled(), enabled);
        QCOMPARE(card.openButton()->isEnabled(), enabled);
    }

    // The banner is only for `done` and `failed`.
    QWidget *bannerWidget = card.findChild<QWidget *>("banner");
    QCOMPARE(bannerWidget->isVisibleTo(&card), !banner.isEmpty());
    if (!banner.isEmpty())
        QCOMPARE(card.findChild<QLabel *>("bannerText")->text(), banner);
    QCOMPARE(card.findChild<QWidget *>("busyBar")->isVisibleTo(&card), kind == ScreenKind::Busy);
}

void TestHome::busyCard()
{
    DetectedIpod ipod = ipodIn(IpodState::Ready);
    ipod.volume = VolumeInfo{QStringLiteral("/mnt/ipod"), QStringLiteral("vfat"), {}, 1000, 500};
    ScreenEntry entry = entryOf(QStringLiteral("1-2"), ScreenKind::Busy, ipod);
    entry.state.operation = OperationKind::Eject;

    IpodCard card;
    card.setEntry(entry, models(), kHost);
    QVERIFY(!card.ejectButton()->isEnabled());
    QVERIFY(!card.openButton()->isEnabled());
    QVERIFY(card.findChild<QProgressBar *>("busyBar")->isVisibleTo(&card));
    QCOMPARE(card.findChild<QProgressBar *>("busyBar")->maximum(), 0);
    QVERIFY(!card.findChild<QProgressBar *>("spaceBar")->isVisibleTo(&card));
    QCOMPARE(card.findChild<QLabel *>("ipodState")->text(),
             QStringLiteral("Getting the iPod ready to unplug..."));

    // The card goes back to normal when the operation ends.
    entry.state.kind = ScreenKind::Ready;
    card.setEntry(entry, models(), kHost);
    QVERIFY(card.ejectButton()->isEnabled());
    QVERIFY(!card.findChild<QProgressBar *>("busyBar")->isVisibleTo(&card));
    QVERIFY(card.findChild<QProgressBar *>("spaceBar")->isVisibleTo(&card));
}

void TestHome::spaceLabel()
{
    DetectedIpod ipod = ipodIn(IpodState::Ready);
    ipod.volume = VolumeInfo{
        QStringLiteral("/mnt/ipod"), QStringLiteral("vfat"), {}, 119'200'000'000, 35'000'000'000};
    IpodCard card;
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipod), models(), kHost);
    QCOMPARE(card.findChild<QLabel *>("spaceLabel")->text(),
             QStringLiteral("84.2 GB used of 119.2 GB, 35.0 GB free"));
    QVERIFY(card.findChild<QLabel *>("spaceLabel")->isVisibleTo(&card));
    auto *bar = card.findChild<QProgressBar *>("spaceBar");
    QVERIFY(bar->isVisibleTo(&card));
    QCOMPARE(bar->value(), 706);
    QCOMPARE(bar->maximum(), 1000);
}

void TestHome::spaceHidden_data()
{
    QTest::addColumn<bool>("hasVolume");
    QTest::addColumn<QString>("mountPoint");
    QTest::addColumn<qint64>("total");

    QTest::newRow("no volume") << false << QString() << qint64(0);
    QTest::newRow("not mounted") << true << QString() << qint64(119'200'000'000);
    QTest::newRow("size unknown") << true << QStringLiteral("/mnt/ipod") << qint64(0);
}

void TestHome::spaceHidden()
{
    QFETCH(bool, hasVolume);
    QFETCH(QString, mountPoint);
    QFETCH(qint64, total);

    DetectedIpod ipod = ipodIn(IpodState::Ready);
    if (hasVolume)
        ipod.volume = VolumeInfo{mountPoint, QStringLiteral("vfat"), {}, total, total / 2};
    IpodCard card;
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipod), models(), kHost);
    QVERIFY(!card.findChild<QProgressBar *>("spaceBar")->isVisibleTo(&card));
    QVERIFY(!card.findChild<QLabel *>("spaceLabel")->isVisibleTo(&card));
}

void TestHome::otherModelOtherDrawing()
{
    IpodCard classic;
    classic.setEntry(
        entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready, "classic-6g")),
        models(), kHost);
    IpodCard nano;
    nano.setEntry(
        entryOf(QStringLiteral("1-3"), ScreenKind::Ready, ipodIn(IpodState::Ready, "nano-1g")),
        models(), kHost);

    QLabel *a = classic.findChild<QLabel *>("drawing");
    QLabel *b = nano.findChild<QLabel *>("drawing");
    QCOMPARE(a->property("drawingId").toString(), QStringLiteral("classic-6g"));
    QCOMPARE(b->property("drawingId").toString(), QStringLiteral("nano-1g"));
    QVERIFY(a->pixmap().toImage() != b->pixmap().toImage());
    // 112 px high, whatever the width.
    QCOMPARE(a->height(), 112);
    QCOMPARE(b->height(), 112);
    QCOMPARE(a->pixmap().deviceIndependentSize().height(), 112.0);
}

void TestHome::unknownModelIsGeneric()
{
    const auto drawingOf = [](std::optional<DetectedIpod> ipod) {
        IpodCard card;
        card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipod), models(), kHost);
        return card.findChild<QLabel *>("drawing")->property("drawingId").toString();
    };
    DetectedIpod ambiguous = ipodIn(IpodState::Ambiguous, QString());
    ambiguous.candidates = {QStringLiteral("nano-1g"), QStringLiteral("ipod-video")};
    QCOMPARE(drawingOf(ambiguous), QStringLiteral("generic"));
    QCOMPARE(drawingOf(std::nullopt), QStringLiteral("generic"));
    QCOMPARE(drawingOf(ipodIn(IpodState::Ready, "no-such-model")), QStringLiteral("generic"));
}

void TestHome::nameFromVariantModelOrDefault()
{
    const auto nameOf = [](std::optional<DetectedIpod> ipod) {
        IpodCard card;
        card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipod), models(), kHost);
        return card.findChild<QLabel *>("ipodName")->text();
    };
    DetectedIpod withVariant = ipodIn(IpodState::Ready);
    withVariant.variant.source = VariantSource::SysInfo;
    withVariant.variant.name = QStringLiteral("iPod Classic 120 GB");
    withVariant.variant.released = QStringLiteral("late 2008");
    // The full name, the same as the `added` line of tacet-watch.
    QCOMPARE(nameOf(withVariant), QStringLiteral("iPod Classic 120 GB (late 2008)"));
    withVariant.variant.released.clear();
    QCOMPARE(nameOf(withVariant), QStringLiteral("iPod Classic 120 GB"));
    QCOMPARE(nameOf(ipodIn(IpodState::Ready)), QStringLiteral("iPod Classic"));
    QCOMPARE(nameOf(ipodIn(IpodState::Ambiguous, QString())), QStringLiteral("iPod"));
    QCOMPARE(nameOf(std::nullopt), QStringLiteral("iPod"));
}

void TestHome::firmwareLineHiddenWhenEmpty()
{
    IpodCard card;
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready)),
                  models(), kHost);
    QVERIFY(card.findChild<QLabel *>("ipodFirmware")->isVisibleTo(&card));
    QCOMPARE(card.findChild<QLabel *>("ipodFirmware")->text(), QStringLiteral("TacetOS 0.1"));
    // No iPod behind the entry: no firmware line.
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Released, std::nullopt), models(),
                  kHost);
    QVERIFY(!card.findChild<QLabel *>("ipodFirmware")->isVisibleTo(&card));
}

void TestHome::releasedCardKeepsTheName()
{
    DetectedIpod ipod = ipodIn(IpodState::Ready);
    ipod.variant.source = VariantSource::SysInfo;
    ipod.variant.name = QStringLiteral("iPod Classic 120 GB");
    ipod.variant.released = QStringLiteral("late 2008");
    ipod.volume = VolumeInfo{
        QStringLiteral("/mnt/ipod"), QStringLiteral("vfat"), {}, 119'200'000'000, 35'000'000'000};
    IpodCard card;
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipod), models(), kHost);
    QVERIFY(card.findChild<QLabel *>("ipodFirmware")->isVisibleTo(&card));
    QVERIFY(card.findChild<QLabel *>("spaceLabel")->isVisibleTo(&card));

    // After the eject the entry carries the last complete iPod: same name, no
    // firmware line, no space, even with a volume in the record.
    card.setEntry(entryOf(QStringLiteral("1-2"), ScreenKind::Released, ipod), models(), kHost);
    QCOMPARE(card.findChild<QLabel *>("ipodName")->text(),
             QStringLiteral("iPod Classic 120 GB (late 2008)"));
    QCOMPARE(card.findChild<QLabel *>("ipodState")->text(),
             QStringLiteral("You can unplug the iPod now."));
    QVERIFY(!card.findChild<QLabel *>("ipodFirmware")->isVisibleTo(&card));
    QVERIFY(!card.findChild<QLabel *>("spaceLabel")->isVisibleTo(&card));
    QVERIFY(!card.findChild<QProgressBar *>("spaceBar")->isVisibleTo(&card));
}

void TestHome::accessibleNames()
{
    HomeScreen screen;
    screen.setEntries({entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready))},
                      models(), kHost);
    IpodCard *card = screen.cards().first();
    QCOMPARE(card->accessibleName(), QStringLiteral("iPod Classic, Ready."));
    QCOMPARE(card->ejectButton()->accessibleName(), QStringLiteral("Eject"));

    screen.setEntries(
        {entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready)),
         entryOf(QStringLiteral("1-3"), ScreenKind::Ready, ipodIn(IpodState::Ready, "nano-1g"))},
        models(), kHost);
    QCOMPARE(screen.cards().size(), 2);
    QCOMPARE(screen.cards().at(0)->ejectButton()->accessibleName(),
             QStringLiteral("Eject iPod Classic"));
    QCOMPARE(screen.cards().at(1)->openButton()->accessibleName(),
             QStringLiteral("Open iPod nano (1st generation)"));
}

void TestHome::zeroOneThreeIpods()
{
    HomeScreen screen;
    screen.resize(900, 600);
    screen.show();
    QVERIFY(QTest::qWaitForWindowExposed(&screen));
    QWidget *empty = screen.findChild<QWidget *>("emptyState");

    QVERIFY(empty->isVisible());
    QCOMPARE(screen.cards().size(), 0);
    QCOMPARE(screen.findChild<QLabel *>("emptyTitle")->text(),
             QStringLiteral("Connect your iPod."));
    QCOMPARE(screen.findChild<QLabel *>("emptyHint")->text(),
             QStringLiteral("TacetOS finds it by itself. Nothing on it changes until you ask."));

    screen.setEntries({entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready))},
                      models(), kHost);
    QVERIFY(!empty->isVisible());
    QCOMPARE(screen.cards().size(), 1);

    const QList<ScreenEntry> three = {
        entryOf(QStringLiteral("1-3"), ScreenKind::Ready, ipodIn(IpodState::Ready, "nano-1g")),
        entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready, "classic-6g")),
        entryOf(QStringLiteral("1-4"), ScreenKind::Ready, ipodIn(IpodState::Ready, "ipod-video"))};
    screen.setEntries(three, models(), kHost);
    QCOMPARE(screen.cards().size(), 3);
    const QStringList names = {QStringLiteral("iPod nano (1st generation)"),
                               QStringLiteral("iPod Classic"), QStringLiteral("iPod Video")};
    QTRY_VERIFY(screen.cards().at(0)->y() < screen.cards().at(1)->y());
    QTRY_VERIFY(screen.cards().at(1)->y() < screen.cards().at(2)->y());
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(screen.cards().at(i)->portPath(), three.at(i).portPath);
        QCOMPARE(screen.cards().at(i)->findChild<QLabel *>("ipodName")->text(), names.at(i));
    }
    QVERIFY(screen.cards().at(2)->isVisible());

    // A card that stays in the list stays the same widget; one that leaves goes.
    IpodCard *first = screen.cards().at(0);
    screen.setEntries({three.at(0)}, models(), kHost);
    QCOMPARE(screen.cards().size(), 1);
    QCOMPARE(screen.cards().at(0), first);

    screen.setEntries({}, models(), kHost);
    QCOMPARE(screen.cards().size(), 0);
    QVERIFY(empty->isVisible());
}

void TestHome::columnIsCentredAndCapped()
{
    HomeScreen screen;
    screen.resize(1400, 700);
    screen.setEntries({entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready))},
                      models(), kHost);
    screen.show();
    QVERIFY(QTest::qWaitForWindowExposed(&screen));
    QWidget *column = screen.findChild<QWidget *>("column");
    QTRY_COMPARE(column->width(), 720);
    const int left = column->mapTo(&screen, QPoint(0, 0)).x();
    QVERIFY(qAbs(left - (1400 - 720) / 2) <= 1);

    // On a narrow window the column shrinks with the margins of 24 px.
    screen.resize(500, 700);
    QTRY_COMPARE(column->width(), 500 - 48);
    // The card sits at the top of the body, not in the middle.
    IpodCard *card = screen.cards().first();
    QVERIFY(card->mapTo(&screen, QPoint(0, 0)).y() <= 56 + 24 + 1);
}

void TestHome::detectionComing()
{
    HomeScreen screen;
    screen.setDetectionAvailable(false);
    QCOMPARE(screen.findChild<QLabel *>("emptyHint")->text(),
             QStringLiteral("Detection on this system is coming."));
    screen.setDetectionAvailable(true);
    QCOMPARE(screen.findChild<QLabel *>("emptyHint")->text(),
             QStringLiteral("TacetOS finds it by itself. Nothing on it changes until you ask."));
}

void TestHome::headerAndMark()
{
    StyleController controller;
    controller.apply(Qt::ColorScheme::Light);
    HomeScreen screen;
    screen.show();
    QVERIFY(QTest::qWaitForWindowExposed(&screen));
    QCOMPARE(screen.findChild<QWidget *>("header")->height(), 56);
    QCOMPARE(screen.findChild<QLabel *>("headerTitle")->text(), QStringLiteral("TacetOS"));
    QCOMPARE(screen.findChild<QLabel *>("headerMark")->height(), 24);
    QCOMPARE(screen.findChild<QLabel *>("emptyMark")->height(), 64);
    QCOMPARE(screen.findChild<QLabel *>("headerMark")->property("markId").toString(),
             QStringLiteral("mark"));

    // The white mark follows the dark look, live.
    controller.apply(Qt::ColorScheme::Dark);
    QTRY_COMPARE(screen.findChild<QLabel *>("headerMark")->property("markId").toString(),
                 QStringLiteral("mark-white"));
    QCOMPARE(screen.findChild<QLabel *>("emptyMark")->property("markId").toString(),
             QStringLiteral("mark-white"));
    controller.apply(Qt::ColorScheme::Light);
    QTRY_COMPARE(screen.findChild<QLabel *>("headerMark")->property("markId").toString(),
                 QStringLiteral("mark"));
}

void TestHome::buttonSignals()
{
    ScreenEntry mac =
        entryOf(QStringLiteral("1-4"), ScreenKind::Attention, ipodIn(IpodState::MacFormat));
    mac.state.reason = IpodState::MacFormat;
    HomeScreen screen;
    screen.setEntries(
        {entryOf(QStringLiteral("1-2"), ScreenKind::Ready, ipodIn(IpodState::Ready)),
         entryOf(QStringLiteral("1-3"), ScreenKind::NotMounted, ipodIn(IpodState::NoVolume)), mac},
        models(), kHost);
    QCOMPARE(screen.cards().size(), 3);

    QSignalSpy spy(&screen, &HomeScreen::actionRequested);
    const auto press = [&](int card, bool open, const QString &port, HomeAction expected) {
        spy.clear();
        QPushButton *button =
            open ? screen.cards().at(card)->openButton() : screen.cards().at(card)->ejectButton();
        button->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), port);
        QCOMPARE(spy.at(0).at(1).value<HomeAction>(), expected);
    };
    press(0, true, QStringLiteral("1-2"), HomeAction::Open);
    press(0, false, QStringLiteral("1-2"), HomeAction::Eject);
    press(1, true, QStringLiteral("1-3"), HomeAction::Mount);
    press(1, false, QStringLiteral("1-3"), HomeAction::Eject);
    press(2, true, QStringLiteral("1-4"), HomeAction::MountReadOnly);
    press(2, false, QStringLiteral("1-4"), HomeAction::Eject);

    // A disabled button (busy) says nothing.
    ScreenEntry busy = entryOf(QStringLiteral("1-2"), ScreenKind::Busy, ipodIn(IpodState::Ready));
    screen.setEntries({busy}, models(), kHost);
    spy.clear();
    screen.cards().first()->openButton()->click();
    screen.cards().first()->ejectButton()->click();
    QCOMPARE(spy.count(), 0);
}

void TestHome::refreshSignal()
{
    HomeScreen screen;
    QSignalSpy spy(&screen, &HomeScreen::refreshRequested);
    screen.findChild<QPushButton *>("refreshButton")->click();
    QCOMPARE(spy.count(), 1);
    // Still enabled with no iPod and with detection missing.
    screen.setDetectionAvailable(false);
    QVERIFY(screen.findChild<QPushButton *>("refreshButton")->isEnabled());
}

QTEST_MAIN(TestHome)
#include "tst_home.moc"
