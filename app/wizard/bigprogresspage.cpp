// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "wizard/bigprogresspage.h"

#include "flow.h"
#include "runner.h"
#include "wizard/installpage.h"
#include "wizardconstants.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QProgressBar>
#include <QVBoxLayout>

namespace tacetos {

using namespace wizard;

namespace {

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

// Screens section 13.2: the stage of the step in words. A stage the table does not know shows no
// text, never its code.
QString stageWords(const QString &step, const QString &stage)
{
    if (stage == QLatin1String("unpack"))
        return wtr("Unpacking the firmware");
    if (stage == QLatin1String("keep"))
        return wtr("Keeping your settings");
    if (stage == QLatin1String("swap"))
        return wtr("Switching to the new version");
    if (stage == QLatin1String("check") && step == QLatin1String(step::firmware))
        return wtr("Checking the files");
    if (stage == QLatin1String("unmount"))
        return wtr("Closing the iPod's volume");
    if (stage == QLatin1String("open"))
        return wtr("Waiting for your permission");
    if (stage == QLatin1String("check"))
        return wtr("Checking the iPod");
    if (stage == QLatin1String("backup"))
        return wtr("Saving the firmware partition");
    if (stage == QLatin1String("write"))
        return wtr("Writing");
    if (stage == QLatin1String("verify"))
        return wtr("Reading back and checking");
    if (stage == QLatin1String("record"))
        return wtr("Keeping a record");
    return QString();
}

// Section 10.2: the banner is 64 px high, the bar 70 % of the page width.
constexpr int kBannerHeight = 64;
constexpr int kBarPercentOfWidth = 70;

} // namespace

BigProgressPage::BigProgressPage(WizardRunner *runner, QWidget *parent)
    : QWidget(parent), runner_(runner), restartMs_(kRestartWindowMs)
{
    setObjectName(QStringLiteral("bigProgressPage"));
    clock_ = monotonicMs;

    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("bigProgressBanner"));
    banner_->setAlignment(Qt::AlignCenter);
    banner_->setAccessibleName(QStringLiteral("TacetOS"));

    bar_ = new QProgressBar(this);
    bar_->setObjectName(QStringLiteral("bigProgressBar"));
    bar_->setRange(0, 100);
    bar_->setValue(0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(12);
    bar_->setAccessibleName(wtr("Install progress"));

    stageLine_ = new QLabel(this);
    stageLine_->setObjectName(QStringLiteral("bigProgressStage"));
    stageLine_->setProperty("role", QStringLiteral("secondary"));
    stageLine_->setWordWrap(true);

    unplug_ = new QLabel(wtr("Do not unplug the iPod"), this);
    unplug_->setObjectName(QStringLiteral("bigProgressUnplug"));
    unplug_->setWordWrap(true);

    // Screens section 7.3: the table of tones, as on the page of the restart.
    auto *beeps = new QLabel(this);
    beeps->setObjectName(QStringLiteral("bigProgressBeeps"));
    beeps->setProperty("role", QStringLiteral("secondary"));
    beeps->setWordWrap(true);
    QString table = wtr("What the beeps mean");
    for (const auto &row : InstallPage::beepTable())
        table += QStringLiteral("\n%1: %2").arg(row.first, row.second);
    beeps->setText(table);
    beeps->hide();
    beeps_ = beeps;

    // The bar and the lines under it share the 70 % column, centred.
    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(bar_);
    column->addSpacing(12);
    column->addWidget(stageLine_);
    column->addWidget(unplug_);
    column->addSpacing(16);
    column->addWidget(beeps_);
    auto *row = new QHBoxLayout;
    row->addStretch((100 - kBarPercentOfWidth) / 2);
    row->addLayout(column, kBarPercentOfWidth);
    row->addStretch((100 - kBarPercentOfWidth) / 2);

    // The banner at a third of the height: one share of the room above, two below.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(0);
    layout->addStretch(1);
    layout->addWidget(banner_);
    layout->addSpacing(32);
    layout->addLayout(row);
    layout->addStretch(2);

    refreshBanner();
    refreshBar();

    tick_.setInterval(500);
    connect(&tick_, &QTimer::timeout, this, &BigProgressPage::refreshRestart);
    connect(runner_, &WizardRunner::stepStarted, this,
            [this](int index, const QString &name) { onStarted(index, name); });
    connect(runner_, &WizardRunner::stepProgress, this, &BigProgressPage::onProgress);
    connect(runner_, &WizardRunner::stepDone, this,
            [this](int, const QString &name) { onDone(name); });
    connect(runner_, &WizardRunner::stepFailed, this, [this] { onEnded(); });
    connect(runner_, &WizardRunner::flowStopped, this, [this] { onEnded(); });
    connect(runner_, &WizardRunner::flowFinished, this, [this] { onEnded(); });
}

void BigProgressPage::setSteps(const QStringList &steps)
{
    whole_.reset(steps);
    step_.clear();
    stage_.clear();
    onEnded();
    refreshBar();
}

QString BigProgressPage::stageText() const
{
    return stageLine_->text();
}

bool BigProgressPage::beepsShown() const
{
    return !beeps_->isHidden();
}

void BigProgressPage::onStarted(int index, const QString &name)
{
    step_ = name;
    stage_.clear();
    sent_ = false;
    tick_.stop();
    restartSince_ = -1;
    whole_.stepStarted(index, name);
    beeps_->setVisible(name == QLatin1String(step::classicBootloader));
    refreshBar();
}

void BigProgressPage::onProgress(const QString &name, const QString &stage, qint64 done,
                                 qint64 total)
{
    if (name != step_)
        return;
    if (WholeProgress::isRestart(name)) {
        // The helper says `resume` at the end of every try: only a try that reached `send`
        // restarts the iPod.
        if (stage == QLatin1String("check"))
            sent_ = false;
        else if (stage == QLatin1String("send"))
            sent_ = true;
        if (stage == QLatin1String("resume") && sent_ && restartSince_ < 0) {
            restartSince_ = clock_();
            stage_ = wtr("The iPod restarts");
            tick_.start();
            refreshRestart();
        }
        return;
    }
    stage_ = stageWords(name, stage);
    whole_.stepProgress(name, done, total);
    refreshBar();
}

void BigProgressPage::onDone(const QString &name)
{
    if (name != step_)
        return;
    tick_.stop();
    restartSince_ = -1;
    whole_.stepDone(name);
    refreshBar();
}

void BigProgressPage::onEnded()
{
    tick_.stop();
    restartSince_ = -1;
}

void BigProgressPage::refreshRestart()
{
    if (restartSince_ < 0)
        return;
    whole_.restartWaited(clock_() - restartSince_, restartMs_);
    refreshBar();
}

void BigProgressPage::refreshBar()
{
    const int percent = whole_.percent();
    if (bar_->value() != percent)
        bar_->setValue(percent);
    const QString number = wtr("%1 %").arg(QLocale().toString(percent));
    stageLine_->setText(stage_.isEmpty() ? number : QStringLiteral("%1 · %2").arg(stage_, number));
}

// Section 10.2: the light banner in the light look, the dark one in the dark look.
void BigProgressPage::refreshBanner()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QPixmap source(dark ? QStringLiteral(":/logo/banner-dark.png")
                              : QStringLiteral(":/logo/banner-light.png"));
    if (source.isNull()) {
        banner_->setPixmap(QPixmap());
        banner_->setFixedHeight(kBannerHeight);
        return;
    }
    const qreal ratio = devicePixelRatioF();
    QPixmap scaled = source.scaledToHeight(qRound(kBannerHeight * ratio), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(ratio);
    banner_->setPixmap(scaled);
    banner_->setFixedHeight(kBannerHeight);
}

void BigProgressPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        refreshBanner();
}

} // namespace tacetos
