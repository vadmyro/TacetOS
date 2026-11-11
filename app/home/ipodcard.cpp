// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/ipodcard.h"

#include "style/appfont.h"

#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <algorithm>

namespace tacetos {

using namespace device;

namespace {

constexpr int kDrawingHeight = 112;
constexpr int kBarHeight = 6;

QString htr(const char *text)
{
    return QCoreApplication::translate("home", text);
}

// The detection state under a displayed state that has none of its own (`busy`,
// `done`, `failed`): what the card offers is what that state offers.
std::optional<IpodState> underlyingState(const ScreenEntry &entry)
{
    if (!entry.ipod)
        return std::nullopt;
    return entry.ipod->state;
}

struct Buttons
{
    bool eject = false;
    bool open = false;
    QString openText;
    HomeAction openAction = HomeAction::Open;
};

// Section 6.
Buttons buttonsFor(const ScreenEntry &entry, HostSystem host)
{
    Buttons buttons;
    std::optional<IpodState> detection;
    switch (entry.state.kind) {
    case ScreenKind::Ready:
        detection = IpodState::Ready;
        break;
    case ScreenKind::NotMounted:
        detection = IpodState::NoVolume;
        break;
    case ScreenKind::Attention:
        detection = entry.state.reason;
        break;
    case ScreenKind::Failed:
    case ScreenKind::Busy:
        // The buttons of the state underneath (a busy card shows them disabled, so
        // the card does not change shape).
        detection = underlyingState(entry);
        break;
    default:
        break;
    }
    if (!detection)
        return buttons;

    switch (*detection) {
    case IpodState::Ready:
        buttons.eject = buttons.open = true;
        buttons.openText = htr("Open");
        buttons.openAction = HomeAction::Open;
        break;
    case IpodState::NoVolume:
        if (host == HostSystem::Linux) {
            buttons.eject = buttons.open = true;
            buttons.openText = htr("Open");
            buttons.openAction = HomeAction::Mount;
        }
        break;
    case IpodState::MacFormat:
        buttons.eject = buttons.open = true;
        buttons.openText = htr("Open read only");
        buttons.openAction = HomeAction::MountReadOnly;
        break;
    default:
        break;
    }
    return buttons;
}

// The state that detection alone would show for `ipod`: the card of a `done` or a `failed`
// keeps saying that, under the banner.
ScreenState detectionScreen(const DetectedIpod &ipod)
{
    ScreenState state;
    switch (ipod.state) {
    case IpodState::Ready:
        state.kind = ScreenKind::Ready;
        break;
    case IpodState::NoVolume:
        state.kind = ScreenKind::NotMounted;
        break;
    case IpodState::Dfu:
        state.kind = ScreenKind::Dfu;
        break;
    case IpodState::Waiting:
        state.kind = ScreenKind::Reading;
        break;
    default:
        state.kind = ScreenKind::Attention;
        state.reason = ipod.state;
        break;
    }
    return state;
}

const char *dotRole(ScreenKind kind)
{
    switch (kind) {
    case ScreenKind::Ready:
    case ScreenKind::Done:
        return "dot-accent";
    case ScreenKind::Attention:
    case ScreenKind::Failed:
        return "dot-danger";
    default:
        return "dot-secondary";
    }
}

QString gigabytes(qint64 bytes)
{
    return QLocale().toString(static_cast<double>(bytes) / 1e9, 'f', 1);
}

// The name on top of the card: the name of detection (variant, else model), else "iPod".
QString nameOf(const std::optional<DetectedIpod> &ipod, const QList<Model> &models)
{
    const QString name = ipod ? ipodName(*ipod, models) : QString();
    return name.isEmpty() ? htr("iPod") : name;
}

// The drawing of a model that is known for sure, else the generic one.
QString drawingIdOf(const std::optional<DetectedIpod> &ipod)
{
    if (ipod && !ipod->model.isEmpty() &&
        QFile::exists(QStringLiteral(":/models/%1.svg").arg(ipod->model))) {
        return ipod->model;
    }
    return QStringLiteral("generic");
}

} // namespace

IpodCard::IpodCard(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("ipodCard"));
    setProperty("role", QStringLiteral("card"));
    setAttribute(Qt::WA_StyledBackground);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    banner_ = new QFrame(this);
    banner_->setObjectName(QStringLiteral("banner"));
    banner_->setAttribute(Qt::WA_StyledBackground);
    auto *bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(20, 10, 20, 10);
    bannerText_ = new QLabel(banner_);
    bannerText_->setObjectName(QStringLiteral("bannerText"));
    bannerText_->setWordWrap(true);
    bannerText_->setFont(fontFor(TextStyle::Body));
    bannerLayout->addWidget(bannerText_);
    banner_->hide();
    outer->addWidget(banner_);

    auto *content = new QVBoxLayout;
    content->setContentsMargins(19, 19, 19, 19);
    content->setSpacing(16);
    outer->addLayout(content);

    auto *top = new QHBoxLayout;
    top->setSpacing(20);
    content->addLayout(top);

    drawing_ = new QLabel(this);
    drawing_->setObjectName(QStringLiteral("drawing"));
    top->addWidget(drawing_, 0, Qt::AlignTop);

    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    top->addLayout(text, 1);

    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("ipodName"));
    nameLabel_->setFont(fontFor(TextStyle::Title));
    nameLabel_->setWordWrap(true);
    text->addWidget(nameLabel_);

    auto *stateRow = new QHBoxLayout;
    stateRow->setSpacing(8);
    dot_ = new QFrame(this);
    dot_->setObjectName(QStringLiteral("stateDot"));
    dot_->setAttribute(Qt::WA_StyledBackground);
    dot_->setFixedSize(8, 8);
    // Level with the middle of the first line of text.
    auto *dotColumn = new QVBoxLayout;
    dotColumn->setContentsMargins(0, 7, 0, 0);
    dotColumn->addWidget(dot_, 0, Qt::AlignTop);
    stateRow->addLayout(dotColumn);
    state_ = new QLabel(this);
    state_->setObjectName(QStringLiteral("ipodState"));
    state_->setFont(fontFor(TextStyle::Body));
    state_->setWordWrap(true);
    stateRow->addWidget(state_, 1);
    text->addLayout(stateRow);

    firmware_ = new QLabel(this);
    firmware_->setObjectName(QStringLiteral("ipodFirmware"));
    firmware_->setProperty("role", QStringLiteral("secondary"));
    firmware_->setFont(fontFor(TextStyle::Secondary));
    text->addWidget(firmware_);

    spaceBar_ = new QProgressBar(this);
    spaceBar_->setObjectName(QStringLiteral("spaceBar"));
    spaceBar_->setRange(0, 1000);
    spaceBar_->setTextVisible(false);
    spaceBar_->setFixedHeight(kBarHeight);
    busyBar_ = new QProgressBar(this);
    busyBar_->setObjectName(QStringLiteral("busyBar"));
    busyBar_->setRange(0, 0);
    busyBar_->setTextVisible(false);
    busyBar_->setFixedHeight(kBarHeight);
    spaceLabel_ = new QLabel(this);
    spaceLabel_->setObjectName(QStringLiteral("spaceLabel"));
    spaceLabel_->setProperty("role", QStringLiteral("secondary"));
    spaceLabel_->setFont(fontFor(TextStyle::Secondary));
    spaceLabel_->setWordWrap(true);
    text->addSpacing(8);
    text->addWidget(spaceBar_);
    text->addWidget(busyBar_);
    text->addSpacing(2);
    text->addWidget(spaceLabel_);
    text->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    // Created in the order of the Tab key: Eject, then Open.
    eject_ = new QPushButton(htr("Eject"), this);
    eject_->setObjectName(QStringLiteral("ejectButton"));
    eject_->setProperty("role", QStringLiteral("secondary"));
    eject_->setFont(fontFor(TextStyle::Button));
    eject_->setFocusPolicy(Qt::StrongFocus);
    open_ = new QPushButton(htr("Open"), this);
    open_->setObjectName(QStringLiteral("openButton"));
    open_->setProperty("role", QStringLiteral("primary"));
    open_->setFont(fontFor(TextStyle::Button));
    open_->setFocusPolicy(Qt::StrongFocus);
    buttons->addWidget(eject_);
    buttons->addWidget(open_);
    content->addLayout(buttons);

    connect(eject_, &QPushButton::clicked, this,
            [this] { emit actionRequested(HomeAction::Eject); });
    connect(open_, &QPushButton::clicked, this, [this] { emit actionRequested(openAction_); });

    setEntry(ScreenEntry(), {});
}

void IpodCard::setEntry(const ScreenEntry &entry, const QList<Model> &models, HostSystem host)
{
    portPath_ = entry.portPath;
    name_ = nameOf(entry.ipod, models);
    nameLabel_->setText(name_);

    const ScreenKind kind = entry.state.kind;

    // The state line. After `done` and `failed` the banner says the news and the line keeps
    // saying what the iPod is.
    QString message;
    if (kind == ScreenKind::Done || kind == ScreenKind::Failed) {
        if (entry.ipod) {
            const ScreenState detected = detectionScreen(*entry.ipod);
            message = detected.kind == ScreenKind::Ready
                          ? htr("Ready.")
                          : screenMessage(detected, entry.ipod, models, host);
        }
    } else if (kind == ScreenKind::Ready) {
        // The name is above and the format tells the user nothing.
        message = htr("Ready.");
    } else {
        message = screenMessage(entry.state, entry.ipod, models, host);
    }
    state_->setText(message);
    state_->setVisible(!message.isEmpty());
    setRole(state_, kind == ScreenKind::Attention ? "danger" : "");
    setRole(dot_, dotRole(kind));

    // No firmware line once the iPod is released: it cannot be read any more.
    const QString firmware =
        entry.ipod && kind != ScreenKind::Released ? firmwareMessage(*entry.ipod) : QString();
    firmware_->setText(firmware);
    firmware_->setVisible(!firmware.isEmpty());

    // Section 4: the space, only for a mounted volume of known size.
    const bool busy = kind == ScreenKind::Busy;
    const bool offline = kind == ScreenKind::Released || kind == ScreenKind::Rebooting ||
                         kind == ScreenKind::Unplugged;
    const VolumeInfo *volume = entry.ipod && entry.ipod->volume ? &*entry.ipod->volume : nullptr;
    const bool spaceKnown =
        !offline && volume && !volume->mountPoint.isEmpty() && volume->totalBytes > 0;
    if (spaceKnown) {
        const qint64 total = volume->totalBytes;
        const qint64 free = std::clamp<qint64>(volume->freeBytes, 0, total);
        const qint64 used = total - free;
        spaceBar_->setValue(static_cast<int>(used * 1000 / total));
        spaceLabel_->setText(htr("%1 GB used of %2 GB, %3 GB free")
                                 .arg(gigabytes(used), gigabytes(total), gigabytes(free)));
    }
    spaceBar_->setVisible(spaceKnown && !busy);
    busyBar_->setVisible(busy);
    spaceLabel_->setVisible(spaceKnown);

    // The banner (section 4.1).
    const bool banner = kind == ScreenKind::Done || kind == ScreenKind::Failed;
    if (banner) {
        bannerText_->setText(screenMessage(entry.state, entry.ipod, models, host));
        const bool failed = kind == ScreenKind::Failed;
        setRole(banner_, failed ? "banner-failed" : "banner-done");
        setRole(bannerText_, failed ? "danger" : "");
    }
    banner_->setVisible(banner);

    // The buttons (section 6).
    const Buttons buttons = buttonsFor(entry, host);
    openAction_ = buttons.openAction;
    eject_->setVisible(buttons.eject);
    open_->setVisible(buttons.open);
    open_->setText(buttons.openText.isEmpty() ? htr("Open") : buttons.openText);
    eject_->setEnabled(!busy);
    open_->setEnabled(!busy);
    updateButtonNames();

    setAccessibleName(message.isEmpty() ? name_ : QStringLiteral("%1, %2").arg(name_, message));

    drawingId_ = drawingIdOf(entry.ipod);
    drawing_->setProperty("drawingId", drawingId_);
    drawModel();
}

void IpodCard::setNameInButtons(bool on)
{
    nameInButtons_ = on;
    updateButtonNames();
}

void IpodCard::updateButtonNames()
{
    // Section 9: the accessible name says the iPod when several are plugged in.
    const auto named = [this](const QString &text) {
        return nameInButtons_ ? QStringLiteral("%1 %2").arg(text, name_) : text;
    };
    eject_->setAccessibleName(named(eject_->text()));
    open_->setAccessibleName(named(open_->text()));
}

void IpodCard::drawModel()
{
    QSvgRenderer renderer(QStringLiteral(":/models/%1.svg").arg(drawingId_));
    if (!renderer.isValid())
        return;
    const qreal ratio = devicePixelRatioF();
    const QSizeF view = renderer.viewBoxF().size();
    const int width =
        view.height() > 0 ? qRound(kDrawingHeight * view.width() / view.height()) : kDrawingHeight;
    QPixmap pixmap(QSize(width, kDrawingHeight) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, width, kDrawingHeight));
    painter.end();
    drawing_->setPixmap(pixmap);
    drawing_->setFixedSize(width, kDrawingHeight);
}

void IpodCard::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    // The pixel ratio is known once the card is on a screen.
    drawModel();
}

} // namespace tacetos
