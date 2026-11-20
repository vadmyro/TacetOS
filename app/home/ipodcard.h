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

    // Screens section 1.1: a progress file exists for the iPod, and a wizard window is open.
    void setWizardState(bool hasProgress, bool windowOpen);

    QString portPath() const { return portPath_; }
    QPushButton *wizardButton() const { return wizard_; }
    QPushButton *uninstallButton() const { return uninstall_; }
    QPushButton *ejectButton() const { return eject_; }
    QPushButton *openButton() const { return open_; }

signals:
    void actionRequested(tacetos::HomeAction action);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void drawModel();
    void updateButtonNames();
    void updateWizardButtons();

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
    QPushButton *uninstall_;
    QPushButton *wizard_;
    // What the state of the card lets the wizard offer.
    bool wizardShown_ = false;
    bool firmwareIsOurs_ = false;
    bool isClassic_ = false;
    bool hasProgress_ = false;
    bool windowOpen_ = false;
    bool ejectShown_ = false;
    bool openShown_ = false;
    HomeAction wizardAction_ = HomeAction::InstallWizard;
};

} // namespace tacetos
