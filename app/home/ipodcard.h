// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "home/homeaction.h"
#include "messages.h"
#include "screenstate.h"

#include <QFrame>
#include <QList>

class QLabel;
class QProgressBar;
class QPushButton;

namespace tacetos {

// The card of one iPod (docs/specs/home-screen.md section 4): the drawing of its model, its
// name, the state, the firmware line, the space, and the buttons that the state offers
// (section 6). It only shows what it is given and says what the user asked for.
class IpodCard : public QFrame
{
    Q_OBJECT

public:
    explicit IpodCard(QWidget *parent = nullptr);

    void setEntry(const device::ScreenEntry &entry, const QList<device::Model> &models,
                  device::HostSystem host = device::currentHostSystem());

    // With several iPods the buttons say which one ("Eject iPod Classic 120 GB").
    void setNameInButtons(bool on);

    QString portPath() const { return portPath_; }
    QPushButton *ejectButton() const { return eject_; }
    QPushButton *openButton() const { return open_; }

signals:
    void actionRequested(tacetos::HomeAction action);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void drawModel();
    void updateButtonNames();

    QString portPath_;
    QString name_;
    QString drawingId_;
    bool nameInButtons_ = false;
    // What the open button asks for when it is pressed.
    HomeAction openAction_ = HomeAction::Open;

    QLabel *drawing_;
    QLabel *nameLabel_;
    QFrame *dot_;
    QLabel *state_;
    QLabel *firmware_;
    QProgressBar *spaceBar_;
    QProgressBar *busyBar_;
    QLabel *spaceLabel_;
    QFrame *banner_;
    QLabel *bannerText_;
    QPushButton *eject_;
    QPushButton *open_;
};

} // namespace tacetos
