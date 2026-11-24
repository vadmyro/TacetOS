// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "wholeprogress.h"

#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QProgressBar;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The big progress page (docs/specs/boot-screen.md section 10): the TacetOS banner, one bar for
// all the steps of the flow that write to the iPod, the stage in words with the whole percentage,
// and the table of the beeps on the Classic's bootloader step. It follows a runner that it does
// not own; the window chooses when it shows.
class BigProgressPage : public QWidget
{
    Q_OBJECT

public:
    explicit BigProgressPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // A new flow with these steps: the bar is back at 0.
    void setSteps(const QStringList &steps);

    // A clock in milliseconds that never goes back, for the wait of the restart. The tests give
    // theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }
    // `RESTART_WINDOW`, 60 s by default.
    void setRestartWindowMs(qint64 ms) { restartMs_ = ms; }

    int percent() const { return whole_.percent(); }
    QString stageText() const;
    bool beepsShown() const;
    bool isRestarting() const { return restartSince_ >= 0; }

    // Reads the time waited for the restart. A timer calls it; the tests too.
    void refreshRestart();

protected:
    void changeEvent(QEvent *event) override;

private:
    void onStarted(int index, const QString &name);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onEnded();
    void showStage(const QString &words);
    void refreshBar();
    void refreshBanner();

    wizard::WizardRunner *runner_;
    wizard::WholeProgress whole_;
    std::function<qint64()> clock_;
    QTimer tick_;
    qint64 restartMs_;
    qint64 restartSince_ = -1;
    QString step_;
    QString stage_;
    bool sent_ = false;

    QLabel *banner_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *stageLine_ = nullptr;
    QLabel *unplug_ = nullptr;
    QWidget *beeps_ = nullptr;
};

} // namespace tacetos
