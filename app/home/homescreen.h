// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "home/homeaction.h"
#include "home/ipodcard.h"
#include "messages.h"
#include "screenstate.h"

#include <QList>
#include <QWidget>

class QBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace tacetos {

// The first screen of the app (docs/specs/home-screen.md): a header, and one card per iPod, or
// the empty state when there is none. It shows the list it is given and reports what the user
// asks for; it starts nothing itself.
class HomeScreen : public QWidget
{
    Q_OBJECT

public:
    explicit HomeScreen(QWidget *parent = nullptr);

    // Cards in the order of `entries`. A card is kept while its port stays in the list.
    void setEntries(const QList<device::ScreenEntry> &entries, const QList<device::Model> &models,
                    device::HostSystem host = device::currentHostSystem());

    // False on a system where the app does not detect iPods yet (section 3).
    void setDetectionAvailable(bool available);

    // The cards, top to bottom.
    QList<IpodCard *> cards() const { return cards_; }

signals:
    void actionRequested(const QString &port, tacetos::HomeAction action);
    void refreshRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateMarks();
    void updateTabOrder();

    QLabel *headerMark_;
    QPushButton *refresh_;
    QStackedWidget *pages_;
    QWidget *empty_;
    QLabel *emptyMark_;
    QLabel *emptyTitle_;
    QLabel *emptyHint_;
    QWidget *column_;
    QBoxLayout *columnLayout_;
    QList<IpodCard *> cards_;
    bool detectionAvailable_ = true;
};

} // namespace tacetos
