// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "style/palette.h"

#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;

namespace tacetos::wizard {
class WizardRunner;
}

namespace tacetos {

// The four ways of the drawing of the wheel (docs/specs/install-screens.md section 6.1).
enum class DfuWay { Rest, Hold, Release, WrongButtons };

// The page of the wait `ReadyForDfu` and of the stages of `classic-dfu` that follow it
// (docs/specs/install-screens.md section 6): the drawing of the wheel, the steps before Start, then
// the countdown to hold, the moment to let go, the transfer. It follows the runner by itself,
// without any click after Start. It is wired to a runner that it does not own.
class DfuTutorialPage : public QWidget
{
    Q_OBJECT

public:
    explicit DfuTutorialPage(wizard::WizardRunner *runner, QWidget *parent = nullptr);

    // Section 6.1: the SVG of the wheel, `:/dfu/wheel.svg`, with the colours of its named parts
    // (`menu`, `center`, `play`, `screen`) set for `way`. `pulse` is the opacity of the parts that
    // pulse in the way Hold (1 or 0.6).
    static QByteArray drawingSvg(DfuWay way, const Palette &palette, qreal pulse = 1.0);

    // A clock in milliseconds that never goes back, for the countdowns. The tests give theirs.
    void setClock(const std::function<qint64()> &clock) { clock_ = clock; }

    // Section 12: no pulse when the system asks for less motion. The app sets it at start from
    // the system; by default only the environment variable TACETOS_REDUCED_MOTION sets it.
    void setReducedMotion(bool on);
    bool reducedMotion() const { return reduced_; }

    // The colours of the drawing. By default the light or the dark palette, from the window colour.
    void setDrawingPalette(const Palette &palette);

    // The step of this page runs now, or waits.
    bool isActive() const { return active_; }
    // Before Start, the steps are shown and Start waits for a click.
    bool isAsking() const { return asking_; }
    bool isFailed() const { return failed_; }
    bool isDone() const { return done_; }

    DfuWay way() const { return way_; }
    QString titleText() const;

    // The drawing as it is now.
    QByteArray currentSvg() const { return svg_; }

    // The seconds shown by the countdown, -1 when there is none.
    int countdownSeconds() const { return seconds_; }

    QPushButton *startButton() const { return start_; }

    // The countdown of the stage `wait-device` (15 s, then "Keep holding") and the one of `quiet`
    // (10 s). A timer calls it each second; the tests call it with their clock.
    void refreshCountdown();

    // Moves the pulse of the way Hold by one half second. A timer calls it; the tests too.
    void pulseOnce();

signals:
    // The user pressed Start after a failure that ends the step (`back-in-dfu`): the window
    // resumes the flow.
    void retryRequested();

private:
    enum class Stage { Idle, Asking, Preparing, Hold, Release, Sending, Restarting };

    bool mine(const QString &name) const;
    void onStarted(const QString &name);
    void onWaiting(const QString &name, int kind, const QJsonObject &info);
    void onProgress(const QString &name, const QString &stage, qint64 done, qint64 total);
    void onDone(const QString &name);
    void onFailed(const QString &name, const QString &code);
    void onStartClicked();
    void showAsking(const QString &reason, const QString &hint, const QString &failedStage = {});
    void setStage(Stage stage, DfuWay way, const QString &title);
    void setWay(DfuWay way);
    void announce();
    void redraw();
    qint64 now() const;

    wizard::WizardRunner *runner_;
    std::function<qint64()> clock_;
    QTimer tick_;
    QTimer pulse_;
    Palette palette_;

    QString step_;
    Stage stage_ = Stage::Idle;
    DfuWay way_ = DfuWay::Rest;
    bool active_ = false;
    bool asking_ = false;
    bool failed_ = false;
    bool done_ = false;
    bool retry_ = false;
    bool reduced_ = false;
    bool dim_ = false;
    qint64 countdownSince_ = -1;
    int seconds_ = -1;
    QByteArray svg_;

    QLabel *title_ = nullptr;
    QLabel *drawing_ = nullptr;
    QWidget *steps_ = nullptr;
    QLabel *intro_ = nullptr;
    QLabel *reasonLine_ = nullptr;
    QWidget *warning_ = nullptr;
    QLabel *wrongDrawing_ = nullptr;
    QWidget *running_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *countdown_ = nullptr;
    QLabel *under_ = nullptr;
    QLabel *startHint_ = nullptr;
    QPushButton *start_ = nullptr;
};

} // namespace tacetos
