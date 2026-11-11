// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/homescreen.h"

#include "style/appfont.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QVBoxLayout>

namespace tacetos {

using namespace device;

namespace {

constexpr int kHeaderHeight = 56;
constexpr int kMargin = 24;
constexpr int kColumnWidth = 720;
constexpr int kCardGap = 12;

QString htr(const char *text)
{
    return QCoreApplication::translate("home", text);
}

QLabel *newLabel(QWidget *parent, const char *name, TextStyle style, bool secondary = false)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setFont(fontFor(style));
    if (secondary)
        label->setProperty("role", QStringLiteral("secondary"));
    return label;
}

// The mark of the logo for the look in use: the dark one on a light window, the white one on
// a dark window (decided from the window colour, so it follows the palette of the app).
void drawMark(QLabel *label, int height, bool onDark)
{
    const QString file = onDark ? QStringLiteral("mark-white") : QStringLiteral("mark");
    QSvgRenderer renderer(QStringLiteral(":/logo/%1.svg").arg(file));
    if (!renderer.isValid())
        return;
    const QSizeF view = renderer.viewBoxF().size();
    const int width = view.height() > 0 ? qRound(height * view.width() / view.height()) : height;
    const qreal ratio = label->devicePixelRatioF();
    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, width, height));
    painter.end();
    label->setPixmap(pixmap);
    label->setFixedSize(width, height);
    label->setProperty("markId", file);
}

} // namespace

HomeScreen::HomeScreen(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("homeScreen"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header (section 1).
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("header"));
    header->setProperty("role", QStringLiteral("header"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(kHeaderHeight);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kMargin, 0, kMargin, 0);
    headerLayout->setSpacing(10);
    headerMark_ = new QLabel(header);
    headerMark_->setObjectName(QStringLiteral("headerMark"));
    headerLayout->addWidget(headerMark_);
    auto *title = newLabel(header, "headerTitle", TextStyle::Title);
    title->setText(QStringLiteral("TacetOS"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    refresh_ = new QPushButton(htr("Refresh"), header);
    refresh_->setObjectName(QStringLiteral("refreshButton"));
    refresh_->setProperty("role", QStringLiteral("secondary"));
    refresh_->setFont(fontFor(TextStyle::Button));
    refresh_->setFocusPolicy(Qt::StrongFocus);
    refresh_->setAccessibleName(htr("Refresh"));
    headerLayout->addWidget(refresh_);
    root->addWidget(header);
    connect(refresh_, &QPushButton::clicked, this, &HomeScreen::refreshRequested);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("body"));
    root->addWidget(pages_, 1);

    // The empty state (section 3), centred in the body.
    empty_ = new QWidget(pages_);
    empty_->setObjectName(QStringLiteral("emptyState"));
    auto *emptyLayout = new QVBoxLayout(empty_);
    emptyLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    emptyLayout->setSpacing(0);
    emptyLayout->addStretch(1);
    emptyMark_ = new QLabel(empty_);
    emptyMark_->setObjectName(QStringLiteral("emptyMark"));
    emptyLayout->addWidget(emptyMark_, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(20);
    emptyTitle_ = newLabel(empty_, "emptyTitle", TextStyle::Large);
    emptyTitle_->setAlignment(Qt::AlignHCenter);
    emptyTitle_->setWordWrap(true);
    emptyTitle_->setText(screenMessage(ScreenState(), std::nullopt, {}));
    emptyLayout->addWidget(emptyTitle_);
    emptyLayout->addSpacing(8);
    emptyHint_ = newLabel(empty_, "emptyHint", TextStyle::Secondary, true);
    emptyHint_->setAlignment(Qt::AlignHCenter);
    emptyHint_->setWordWrap(true);
    emptyLayout->addWidget(emptyHint_);
    emptyLayout->addStretch(1);
    pages_->addWidget(empty_);

    // The list (section 1): a scroll area with a column of at most 720 px, centred.
    auto *scroll = new QScrollArea(pages_);
    scroll->setObjectName(QStringLiteral("cardsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("cardsPage"));
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    pageLayout->setSpacing(0);
    column_ = new QWidget(page);
    column_->setObjectName(QStringLiteral("column"));
    column_->setMaximumWidth(kColumnWidth);
    columnLayout_ = new QVBoxLayout(column_);
    columnLayout_->setContentsMargins(0, 0, 0, 0);
    columnLayout_->setSpacing(kCardGap);
    // The cards stay at the top, so they do not jump when one comes or goes.
    columnLayout_->addStretch(1);
    pageLayout->addStretch(1);
    pageLayout->addWidget(column_, 1000);
    pageLayout->addStretch(1);
    scroll->setWidget(page);
    pages_->addWidget(scroll);

    setDetectionAvailable(true);
    updateMarks();
    pages_->setCurrentWidget(empty_);
}

void HomeScreen::setDetectionAvailable(bool available)
{
    detectionAvailable_ = available;
    emptyHint_->setText(
        available ? htr("TacetOS finds it by itself. Nothing on it changes until you ask.")
                  : htr("Detection on this system is coming."));
}

void HomeScreen::setEntries(const QList<ScreenEntry> &entries, const QList<Model> &models,
                            HostSystem host)
{
    // Keep the card of a port that stays, drop the others, make the new ones.
    QList<IpodCard *> kept;
    for (const ScreenEntry &entry : entries) {
        IpodCard *card = nullptr;
        for (IpodCard *old : std::as_const(cards_)) {
            if (old->portPath() == entry.portPath && !kept.contains(old)) {
                card = old;
                break;
            }
        }
        if (!card) {
            card = new IpodCard(column_);
            connect(card, &IpodCard::actionRequested, this, [this, card](HomeAction action) {
                emit actionRequested(card->portPath(), action);
            });
        }
        card->setEntry(entry, models, host);
        card->setNameInButtons(entries.size() > 1);
        kept.append(card);
    }
    for (IpodCard *old : std::as_const(cards_)) {
        if (!kept.contains(old)) {
            columnLayout_->removeWidget(old);
            old->hide();
            old->deleteLater();
        }
    }
    cards_ = kept;

    // Put the cards in the order received, above the stretch at the end.
    for (IpodCard *card : std::as_const(cards_))
        columnLayout_->removeWidget(card);
    for (int i = 0; i < cards_.size(); ++i) {
        columnLayout_->insertWidget(i, cards_.at(i));
        cards_.at(i)->show();
    }

    pages_->setCurrentIndex(cards_.isEmpty() ? 0 : 1);
    updateTabOrder();
}

void HomeScreen::updateTabOrder()
{
    // Section 9: Refresh, then each card from top to bottom, left to right.
    QWidget *previous = refresh_;
    for (IpodCard *card : std::as_const(cards_)) {
        setTabOrder(previous, card->ejectButton());
        setTabOrder(card->ejectButton(), card->openButton());
        previous = card->openButton();
    }
}

void HomeScreen::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        updateMarks();
}

void HomeScreen::updateMarks()
{
    const bool onDark = palette().color(QPalette::Window).lightness() < 128;
    drawMark(headerMark_, 24, onDark);
    drawMark(emptyMark_, 64, onDark);
}

} // namespace tacetos
