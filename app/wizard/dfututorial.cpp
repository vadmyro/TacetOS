// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/dfututorial.h"

#include "flow.h"
#include "runner.h"
#include "wizarderrors.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>

namespace tacetos {

using namespace wizard;

namespace {

// Section 6.3: how long the countdowns start from.
constexpr int kHoldSeconds = 15;
constexpr int kQuietSeconds = 10;

QString wtr(const char *text)
{
    return QCoreApplication::translate("wizard", text);
}

qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed();
}

QLabel *makeLabel(const QString &name, QWidget *parent, const char *role = nullptr)
{
    auto *l = new QLabel(parent);
    l->setObjectName(name);
    l->setWordWrap(true);
    if (role)
        l->setProperty("role", QString::fromLatin1(role));
    return l;
}

// Section 6.1, the table of the ways: the colour of each named part.
QColor menuColour(DfuWay way, const Palette &p)
{
    return way == DfuWay::Hold ? p.accent : p.border;
}

QColor centerColour(DfuWay way, const Palette &p)
{
    if (way == DfuWay::Hold)
        return p.accent;
    return way == DfuWay::WrongButtons ? p.danger : p.border;
}

QColor playColour(DfuWay way, const Palette &p)
{
    return way == DfuWay::WrongButtons ? p.danger : p.border;
}

QColor screenColour(DfuWay way, const Palette &p)
{
    return way == DfuWay::Hold || way == DfuWay::Release ? QColor(Qt::black) : p.window;
}

// The drawing as a picture, sharp on screens of high density.
void showSvg(QLabel *label, const QByteArray &svg)
{
    QSvgRenderer renderer(svg);
    const qreal ratio = label->devicePixelRatioF();
    QPixmap pixmap((label->size() * ratio).expandedTo(QSize(1, 1)));
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    pixmap.setDevicePixelRatio(ratio);
    label->setPixmap(pixmap);
}

QString accessibleDrawing(DfuWay way)
{
    switch (way) {
    case DfuWay::Rest:
        return wtr("Click wheel of the iPod Classic");
    case DfuWay::Hold:
        return wtr("Click wheel of the iPod Classic, MENU and the center button to hold");
    case DfuWay::Release:
        return wtr("Click wheel of the iPod Classic, buttons to let go");
    case DfuWay::WrongButtons:
        return wtr("Click wheel of the iPod Classic, the center button and Play are the wrong "
                   "buttons");
    }
    return QString();
}

} // namespace

QByteArray DfuTutorialPage::drawingSvg(DfuWay way, const Palette &p, qreal pulse)
{
    QFile file(QStringLiteral(":/dfu/wheel.svg"));
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    const bool pulsing = way == DfuWay::Hold;
    const QString opacity = QString::number(pulsing ? pulse : 1.0);

    // The fill of each named part. The two parts that pulse also get their opacity.
    struct Part
    {
        QColor fill;
        bool pulses;
    };
    const QHash<QString, Part> parts = {
        {QStringLiteral("menu"), {menuColour(way, p), true}},
        {QStringLiteral("center"), {centerColour(way, p), true}},
        {QStringLiteral("play"), {playColour(way, p), false}},
        {QStringLiteral("screen"), {screenColour(way, p), false}},
        {QStringLiteral("case"), {p.card, false}},
        {QStringLiteral("wheel"), {p.button, false}},
        {QStringLiteral("prev"), {p.border, false}},
        {QStringLiteral("next"), {p.border, false}},
    };

    QXmlStreamReader reader(&file);
    QByteArray out;
    QXmlStreamWriter writer(&out);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError())
            return QByteArray();
        const auto it =
            reader.isStartElement()
                ? parts.constFind(reader.attributes().value(QLatin1String("id")).toString())
                : parts.constEnd();
        if (it == parts.constEnd()) {
            writer.writeCurrentToken(reader);
            continue;
        }
        QXmlStreamAttributes attributes;
        for (const QXmlStreamAttribute &a : reader.attributes()) {
            if (a.name() == QLatin1String("fill"))
                attributes.append(QStringLiteral("fill"), it->fill.name(QColor::HexRgb).toUpper());
            else if (a.name() == QLatin1String("opacity") && it->pulses)
                attributes.append(QStringLiteral("opacity"), opacity);
            else
                attributes.append(a);
        }
        writer.writeStartElement(reader.qualifiedName().toString());
        writer.writeAttributes(attributes);
    }
    return out;
}

DfuTutorialPage::DfuTutorialPage(WizardRunner *runner, QWidget *parent)
    : QWidget(parent), runner_(runner)
{
    setObjectName(QStringLiteral("dfuTutorialPage"));
    clock_ = monotonicMs;
    const QByteArray reduced = qgetenv("TACETOS_REDUCED_MOTION");
    reduced_ = !reduced.isEmpty() && reduced != "0";
    palette_ =
        this->palette().window().color().lightness() < 128 ? Palette::dark() : Palette::light();

    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, &DfuTutorialPage::refreshCountdown);
    // Once a second the opacity goes down and up again: one change each half second.
    pulse_.setInterval(500);
    connect(&pulse_, &QTimer::timeout, this, &DfuTutorialPage::pulseOnce);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(32);
    auto *left = new QVBoxLayout;

    title_ = makeLabel(QStringLiteral("dfuTitle"), this);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    title_->setFont(big);
    left->addWidget(title_);

    // Section 6.2: what to do before Start.
    steps_ = new QWidget(this);
    steps_->setObjectName(QStringLiteral("dfuSteps"));
    auto *list = new QVBoxLayout(steps_);
    list->setContentsMargins(0, 0, 0, 0);
    reasonLine_ = makeLabel(QStringLiteral("dfuReason"), steps_);
    reasonLine_->hide();
    list->addWidget(reasonLine_);
    const QString lines[] = {
        wtr("1. Keep the iPod plugged into this computer."),
        wtr("2. Press Start. TacetOS ejects the iPod."),
        wtr("3. Then hold MENU and the center button together, and keep holding until TacetOS "
            "says to let go (about 15 seconds). The screen goes black: that is normal."),
    };
    int n = 1;
    for (const QString &line : lines) {
        QLabel *step = makeLabel(QStringLiteral("dfuStep%1").arg(n++), steps_);
        step->setText(line);
        list->addWidget(step);
    }
    warning_ = new QWidget(steps_);
    warning_->setObjectName(QStringLiteral("dfuWarning"));
    auto *warn = new QHBoxLayout(warning_);
    warn->setContentsMargins(0, 8, 0, 0);
    wrongDrawing_ = new QLabel(warning_);
    wrongDrawing_->setObjectName(QStringLiteral("dfuWrongDrawing"));
    wrongDrawing_->setFixedSize(38, 64);
    wrongDrawing_->setAccessibleName(accessibleDrawing(DfuWay::WrongButtons));
    auto *warnText = new QVBoxLayout;
    auto *disk = makeLabel(QStringLiteral("dfuDiskModeWarning"), warning_, "danger");
    disk->setText(wtr("Not the center button and Play: that is Disk Mode."));
    auto *nothing = makeLabel(QStringLiteral("dfuNothingSent"), warning_);
    nothing->setText(wtr("Nothing is sent to the iPod before you let go."));
    warnText->addWidget(disk);
    warnText->addWidget(nothing);
    warn->addWidget(wrongDrawing_);
    warn->addLayout(warnText, 1);
    list->addWidget(warning_);
    left->addWidget(steps_);

    // Section 6.3: the stages that follow Start.
    running_ = new QWidget(this);
    running_->setObjectName(QStringLiteral("dfuRunning"));
    auto *run = new QVBoxLayout(running_);
    run->setContentsMargins(0, 0, 0, 0);
    bar_ = new QProgressBar(running_);
    bar_->setObjectName(QStringLiteral("stepProgress"));
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    bar_->setAccessibleName(wtr("Progress of the iPod bootloader"));
    countdown_ = makeLabel(QStringLiteral("dfuCountdown"), running_);
    QFont large = countdown_->font();
    large.setPointSizeF(large.pointSizeF() * 3);
    large.setBold(true);
    countdown_->setFont(large);
    under_ = makeLabel(QStringLiteral("dfuUnder"), running_);
    run->addWidget(bar_);
    run->addWidget(countdown_);
    run->addWidget(under_);
    left->addWidget(running_);

    start_ = new QPushButton(wtr("Start"), this);
    start_->setObjectName(QStringLiteral("dfuStart"));
    start_->setProperty("role", QStringLiteral("primary"));
    start_->setAccessibleName(wtr("Start"));
    connect(start_, &QPushButton::clicked, this, &DfuTutorialPage::onStartClicked);
    left->addStretch();
    left->addWidget(start_, 0, Qt::AlignRight);
    layout->addLayout(left, 1);

    drawing_ = new QLabel(this);
    drawing_->setObjectName(QStringLiteral("dfuDrawing"));
    drawing_->setFixedSize(144, 240);
    layout->addWidget(drawing_, 0, Qt::AlignTop);

    showSvg(wrongDrawing_, drawingSvg(DfuWay::WrongButtons, palette_));
    steps_->hide();
    running_->hide();
    start_->hide();
    redraw();

    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int, const QString &name) { onStarted(name); });
    connect(runner_, &WizardRunner::stepWaiting, this,
            [this](const QString &name, WaitKind kind, const QJsonObject &info) {
                onWaiting(name, int(kind), info);
            });
    connect(runner_, &WizardRunner::stepProgress, this,
            [this](const QString &name, const QString &stage, qint64 done, qint64 total) {
                onProgress(name, stage, done, total);
            });
    connect(runner_, &WizardRunner::stepDone, this,
            [this](int, const QString &name) { onDone(name); });
    connect(runner_, &WizardRunner::stepFailed, this,
            [this](int, const QString &name, const QString &code, const QString &) {
                onFailed(name, code);
            });
    connect(runner_, &WizardRunner::flowStopped, this, [this](const QString &) {
        tick_.stop();
        pulse_.stop();
        active_ = false;
    });
}

bool DfuTutorialPage::mine(const QString &name) const
{
    return name == QLatin1String(step::classicBootloader) ||
           name == QLatin1String(step::classicRemove);
}

qint64 DfuTutorialPage::now() const
{
    return clock_ ? clock_() : monotonicMs();
}

QString DfuTutorialPage::titleText() const
{
    return title_->text();
}

void DfuTutorialPage::setReducedMotion(bool on)
{
    reduced_ = on;
    if (on) {
        pulse_.stop();
        dim_ = false;
        redraw();
    } else if (stage_ == Stage::Hold) {
        pulse_.start();
    }
}

void DfuTutorialPage::setDrawingPalette(const Palette &palette)
{
    palette_ = palette;
    showSvg(wrongDrawing_, drawingSvg(DfuWay::WrongButtons, palette_));
    redraw();
}

void DfuTutorialPage::redraw()
{
    svg_ = drawingSvg(way_, palette_, dim_ ? 0.6 : 1.0);
    showSvg(drawing_, svg_);
    drawing_->setAccessibleName(accessibleDrawing(way_));
}

void DfuTutorialPage::setWay(DfuWay way)
{
    way_ = way;
    dim_ = false;
    if (way == DfuWay::Hold && !reduced_)
        pulse_.start();
    else
        pulse_.stop();
    redraw();
}

void DfuTutorialPage::announce()
{
    QAccessibleEvent alert(title_, QAccessible::Alert);
    QAccessible::updateAccessibility(&alert);
}

void DfuTutorialPage::setStage(Stage stage, DfuWay way, const QString &title)
{
    stage_ = stage;
    title_->setText(title);
    setWay(way);
    const bool asking = stage == Stage::Asking;
    steps_->setVisible(asking);
    start_->setVisible(asking);
    running_->setVisible(!asking && stage != Stage::Idle);
    bar_->setVisible(stage == Stage::Preparing || stage == Stage::Sending ||
                     stage == Stage::Restarting);
    countdown_->setVisible(stage == Stage::Hold || stage == Stage::Release);
    if (stage != Stage::Hold && stage != Stage::Release) {
        tick_.stop();
        countdownSince_ = -1;
        seconds_ = -1;
    }
    announce();
}

// Section 6.1 and 6.3.
void DfuTutorialPage::pulseOnce()
{
    if (way_ != DfuWay::Hold || reduced_) {
        if (dim_) {
            dim_ = false;
            redraw();
        }
        return;
    }
    dim_ = !dim_;
    redraw();
}

void DfuTutorialPage::onStarted(const QString &name)
{
    if (!mine(name))
        return;
    step_ = name;
    active_ = true;
    asking_ = false;
    failed_ = false;
    done_ = false;
    retry_ = false;
    bar_->setRange(0, 0);
    under_->clear();
    setStage(Stage::Preparing, DfuWay::Rest, wtr("Getting the iPod ready"));
}

// Section 6.2.
void DfuTutorialPage::showAsking(const QString &reason, const QString &hint)
{
    asking_ = true;
    const bool uninstall = step_ == QLatin1String(step::classicRemove);
    QString title = uninstall ? wtr("Put the iPod in DFU mode to remove TacetOS")
                              : wtr("Put the iPod in DFU mode");
    DfuWay way = DfuWay::Rest;
    QString line;
    if (reason == QLatin1String(error::diskModeInstead)) {
        title = wtr("The iPod started in Disk Mode");
        line = wtr("You held the center button and Play. Hold MENU and the center button instead.");
        way = DfuWay::WrongButtons;
    } else if (reason == QLatin1String(error::noDisk)) {
        title = wtr("The iPod did not come back");
        line = wtr("If its screen is black, it is still in DFU mode: hold MENU and the center "
                   "button for about 6 seconds, and let go as soon as the screen changes. Wait "
                   "for its menu, then press Start.");
    } else if (reason == QLatin1String(error::dfuFailed)) {
        title = wtr("The iPod did not appear in DFU mode");
        line = hint;
    } else if (reason == QLatin1String(error::backInDfu)) {
        title = wtr("The iPod keeps leaving DFU mode");
        line = wtr("The iPod left DFU mode again and again. Unplug it, plug it in again, then "
                   "try once more.");
    }
    reasonLine_->setText(line);
    reasonLine_->setVisible(!line.isEmpty());
    start_->setEnabled(true);
    setStage(Stage::Asking, way, title);
}

void DfuTutorialPage::onWaiting(const QString &name, int kind, const QJsonObject &info)
{
    if (name != step_ || !mine(name) || kind != int(WaitKind::ReadyForDfu))
        return;
    showAsking(info.value(QStringLiteral("reason")).toString(),
               info.value(QStringLiteral("hint")).toString());
}

void DfuTutorialPage::onStartClicked()
{
    if (retry_) {
        retry_ = false;
        failed_ = false;
        emit retryRequested();
        return;
    }
    if (!asking_)
        return;
    asking_ = false;
    start_->setEnabled(false);
    bar_->setRange(0, 0);
    under_->clear();
    setStage(Stage::Preparing, DfuWay::Rest, wtr("Getting the iPod ready"));
    runner_->answer(UserAnswer());
}

// Section 6.3, the table of the stages.
void DfuTutorialPage::onProgress(const QString &name, const QString &stage, qint64 done,
                                 qint64 total)
{
    if (name != step_ || !mine(name))
        return;
    if (stage == QLatin1String("check") || stage == QLatin1String("eject") ||
        stage == QLatin1String("spin-down") || stage == QLatin1String("pause")) {
        // After `wait-disk`, the same stage with its own title: the iPod is back.
        if (stage_ != Stage::Preparing || !under_->text().isEmpty()) {
            bar_->setRange(0, 0);
            under_->clear();
            setStage(Stage::Preparing, DfuWay::Rest, wtr("Getting the iPod ready"));
        }
    } else if (stage == QLatin1String("wait-disk")) {
        // Wizard section 9.4: Start came before the iPod was there as a disk. The step sends
        // again by itself once it is back.
        const QString text = wtr("If its screen is black, it is in DFU mode: hold MENU and the "
                                 "center button for about 6 seconds to leave it. TacetOS goes on "
                                 "by itself when the iPod is back.");
        if (stage_ == Stage::Preparing && under_->text() == text)
            return;
        bar_->setRange(0, 0);
        under_->setText(text);
        setStage(Stage::Preparing, DfuWay::Rest, wtr("Waiting for the iPod"));
    } else if (stage == QLatin1String("wait-device")) {
        if (stage_ == Stage::Hold)
            return;
        countdownSince_ = now();
        setStage(Stage::Hold, DfuWay::Hold, wtr("Hold MENU and the center button now"));
        tick_.start();
        refreshCountdown();
    } else if (stage == QLatin1String("quiet") || stage == QLatin1String("dfu-detected")) {
        // The device is there: the buttons can go. `quiet` after `dfu-detected` is the same
        // moment, the countdown is not restarted.
        if (stage_ == Stage::Release || stage_ == Stage::Sending || stage_ == Stage::Restarting)
            return;
        countdownSince_ = now();
        under_->clear();
        setStage(Stage::Release, DfuWay::Release, wtr("Let go of the buttons now"));
        tick_.start();
        refreshCountdown();
    } else if (stage == QLatin1String("send")) {
        if (stage_ != Stage::Sending) {
            under_->setText(wtr("Do not unplug the iPod"));
            setStage(Stage::Sending, DfuWay::Release, wtr("Sending TacetOS to the iPod"));
        }
        if (total > 0) {
            bar_->setRange(0, 1000);
            bar_->setValue(int(std::clamp<qint64>(done * 1000 / total, 0, 1000)));
        } else {
            bar_->setRange(0, 0);
        }
    } else if (stage == QLatin1String("resume")) {
        // Every try ends with `resume`: the iPod only restarts after a transfer.
        if (stage_ == Stage::Sending) {
            bar_->setRange(0, 0);
            under_->setText(wtr("Do not unplug the iPod. Wait for it to start."));
            setStage(Stage::Restarting, DfuWay::Release, wtr("The iPod restarts"));
        }
    } else if (stage == QLatin1String("disk-mode-instead")) {
        // Not a failure: the step waits again, and the next wait gives the reason. The page
        // already shows what went wrong.
        showAsking(QString(error::diskModeInstead), QString());
    }
}

// Section 6.3: the countdowns, one tick each second.
void DfuTutorialPage::refreshCountdown()
{
    if (countdownSince_ < 0 || (stage_ != Stage::Hold && stage_ != Stage::Release))
        return;
    const int elapsed = int((now() - countdownSince_) / 1000);
    if (stage_ == Stage::Hold) {
        seconds_ = std::max(0, kHoldSeconds - elapsed);
        countdown_->setText(QString::number(seconds_));
        under_->setText(seconds_ > 0 ? wtr("Keep holding")
                                     : wtr("Keep holding. It can take a few more seconds"));
    } else {
        seconds_ = std::max(0, kQuietSeconds - elapsed);
        countdown_->setText(wtr("Sending in %1 s").arg(seconds_));
    }
}

void DfuTutorialPage::onDone(const QString &name)
{
    if (name != step_ || !mine(name))
        return;
    done_ = true;
    active_ = false;
    tick_.stop();
    pulse_.stop();
}

void DfuTutorialPage::onFailed(const QString &name, const QString &code)
{
    if (name != step_ || !mine(name))
        return;
    failed_ = true;
    active_ = false;
    pulse_.stop();
    if (code == QLatin1String(error::backInDfu)) {
        // Section 6.3: its message, then Start (the window resumes the flow).
        retry_ = true;
        showAsking(code, QString());
        return;
    }
    under_->setText(wtr("The bootloader could not be sent to the iPod."));
    bar_->hide();
    setStage(Stage::Idle, DfuWay::Rest, wtr("The iPod could not be set up"));
    running_->show();
    countdown_->hide();
}

} // namespace tacetos
