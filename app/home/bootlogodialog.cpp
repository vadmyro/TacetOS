// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "home/bootlogodialog.h"

#include "home/homeaction.h"
#include "style/appfont.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

namespace tacetos {

using namespace device;

namespace {

QString htr(const char *text)
{
    return QCoreApplication::translate("bootlogo", text);
}

// Boot screen section 4.1: the size of the logo on each screen.
QSize logoSize(const bootlogo::Screen &screen)
{
    static const struct
    {
        int width, height, logoHeight;
    } sizes[] = {{320, 240, 98}, {220, 176, 68}, {176, 132, 54}, {160, 128, 53}, {138, 110, 46}};
    for (const auto &s : sizes)
        if (s.width == screen.width && s.height == screen.height)
            return {s.width, s.logoHeight};
    // A screen of no row: the proportions of the 320 x 98 logo.
    return {screen.width, screen.width * 98 / 320};
}

QString pictureFilter()
{
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return htr("Pictures (%1)").arg(patterns.join(QLatin1Char(' ')));
}

} // namespace

bootlogo::Logo BootLogoDialog::tacetosLogo(const bootlogo::Screen &screen)
{
    // The dark banner on colour screens, the light one reduced to 4 greys on grey screens.
    const QImage banner(screen.colour ? QStringLiteral(":/logo/banner-dark.png")
                                      : QStringLiteral(":/logo/banner-light.png"));
    QImage canvas(screen.width, screen.height, QImage::Format_RGB32);
    canvas.fill(banner.isNull() ? QColor(Qt::black) : banner.pixelColor(0, 0));
    if (!banner.isNull()) {
        const QSize box = logoSize(screen);
        const QImage scaled = banner.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter painter(&canvas);
        painter.drawImage((screen.width - scaled.width()) / 2,
                          (screen.height - scaled.height()) / 2, scaled);
    }
    if (screen.colour)
        return bootlogo::fromImage(canvas, screen, false);
    // The built-in grey logo takes the nearest level of each pixel, with no dithering.
    bootlogo::Logo logo{bootlogo::Kind::Grey, screen.width, screen.height, {}};
    logo.pixels.resize(qsizetype(screen.width) * screen.height);
    for (int y = 0; y < screen.height; ++y)
        for (int x = 0; x < screen.width; ++x)
            logo.pixels[y * screen.width + x] =
                static_cast<char>((qGray(canvas.pixel(x, y)) + 42) / 85);
    return logo;
}

BootLogoDialog::BootLogoDialog(const Model &model, const QString &mountPoint, QWidget *parent)
    : QDialog(parent), folder_(QDir(mountPoint).filePath(model.firmwareFolder))
{
    if (model.screen)
        screen_ = {model.screen->width, model.screen->height, model.screen->colour};
    setWindowTitle(htr("Boot logo"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(htr("Boot logo"), this);
    title->setFont(fontFor(TextStyle::Title));
    layout->addWidget(title);

    auto *middle = new QHBoxLayout;
    middle->setSpacing(24);
    layout->addLayout(middle);

    auto *choices = new QGridLayout;
    choices->setVerticalSpacing(8);
    choices->setColumnMinimumWidth(0, 24);
    tacetos_ = new QRadioButton(htr("TacetOS logo"), this);
    picture_ = new QRadioButton(htr("My picture"), this);
    choose_ = new QPushButton(htr("Choose"), this);
    choose_->setFont(fontFor(TextStyle::Button));
    current_ = new QLabel(htr("Current picture"), this);
    current_->setProperty("role", QStringLiteral("secondary"));
    current_->setFont(fontFor(TextStyle::Secondary));
    fit_ = new QRadioButton(htr("Fit"), this);
    fill_ = new QRadioButton(htr("Fill"), this);
    fit_->setChecked(true);
    black_ = new QRadioButton(htr("Black screen"), this);
    for (QRadioButton *button : {tacetos_, picture_, fit_, fill_, black_})
        button->setFont(fontFor(TextStyle::Body));

    auto *kind = new QButtonGroup(this);
    kind->addButton(tacetos_);
    kind->addButton(picture_);
    kind->addButton(black_);
    auto *scaling = new QButtonGroup(this);
    scaling->addButton(fit_);
    scaling->addButton(fill_);

    auto *pictureRow = new QHBoxLayout;
    pictureRow->addWidget(picture_);
    pictureRow->addWidget(choose_);
    pictureRow->addStretch(1);
    auto *scalingRow = new QHBoxLayout;
    scalingRow->addWidget(fit_);
    scalingRow->addWidget(fill_);
    scalingRow->addStretch(1);
    choices->addWidget(tacetos_, 0, 0, 1, 2);
    choices->addLayout(pictureRow, 1, 0, 1, 2);
    choices->addWidget(current_, 2, 1);
    choices->addLayout(scalingRow, 3, 1);
    choices->addWidget(black_, 4, 0, 1, 2);
    choices->setRowStretch(5, 1);
    middle->addLayout(choices);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("bootLogoPreview"));
    preview_->setAlignment(Qt::AlignCenter);
    middle->addWidget(preview_, 0, Qt::AlignTop);

    auto *note = new QLabel(htr("The new logo shows the next time the iPod starts."), this);
    note->setProperty("role", QStringLiteral("secondary"));
    note->setFont(fontFor(TextStyle::Secondary));
    note->setWordWrap(true);
    layout->addWidget(note);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("bootLogoMessage"));
    message_->setFont(fontFor(TextStyle::Body));
    message_->setWordWrap(true);
    message_->hide();
    layout->addWidget(message_);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    cancel_ = new QPushButton(htr("Cancel"), this);
    cancel_->setProperty("role", QStringLiteral("secondary"));
    cancel_->setFont(fontFor(TextStyle::Button));
    put_ = new QPushButton(htr("Put on iPod"), this);
    put_->setProperty("role", QStringLiteral("primary"));
    put_->setFont(fontFor(TextStyle::Button));
    put_->setDefault(true);
    buttons->addWidget(cancel_);
    buttons->addWidget(put_);
    layout->addLayout(buttons);

    readIpod();
    refresh();

    for (QRadioButton *button : {tacetos_, picture_, black_})
        connect(button, &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                refresh();
        });
    connect(fit_, &QRadioButton::toggled, this, &BootLogoDialog::convert);
    connect(choose_, &QPushButton::clicked, this, &BootLogoDialog::choose);
    connect(cancel_, &QPushButton::clicked, this, &BootLogoDialog::reject);
    connect(put_, &QPushButton::clicked, this, [this] {
        if (done_)
            accept();
        else
            put();
    });
}

BootLogoDialog::~BootLogoDialog()
{
    if (writer_)
        writer_->wait();
}

// Section 8.2, "When it opens".
void BootLogoDialog::readIpod()
{
    const bootlogo::OnIpod found = bootlogo::readLogo(folder_, screen_);
    switch (found.found) {
    case bootlogo::Found::None:
        tacetos_->setChecked(true);
        break;
    case bootlogo::Found::Black:
        black_->setChecked(true);
        break;
    case bootlogo::Found::Picture:
        picture_->setChecked(true);
        pictureLogo_ = found.logo;
        pictureOnIpod_ = true;
        break;
    case bootlogo::Found::Unusable:
        tacetos_->setChecked(true);
        setMessage(htr("The picture on the iPod cannot be read. The iPod shows the TacetOS logo"),
                   true);
        break;
    }
}

void BootLogoDialog::choose()
{
    const QString path =
        QFileDialog::getOpenFileName(this, htr("Choose a picture"), QString(), pictureFilter());
    if (!path.isEmpty())
        usePicture(path);
}

bool BootLogoDialog::usePicture(const QString &path)
{
    const bootlogo::Picture picture = bootlogo::readPicture(path);
    if (!picture.error.isEmpty()) {
        setMessage(picture.error, true);
        return false;
    }
    source_ = picture.image;
    pictureOnIpod_ = false;
    setMessage(QString(), false);
    picture_->setChecked(true);
    convert();
    return true;
}

// Fit and Fill change the preview at once.
void BootLogoDialog::convert()
{
    if (source_.isNull())
        return;
    pictureLogo_ = bootlogo::fromImage(source_, screen_, fill_->isChecked());
    refresh();
}

void BootLogoDialog::refresh()
{
    bootlogo::Logo shown;
    if (tacetos_->isChecked())
        shown = tacetosLogo(screen_);
    else if (picture_->isChecked() && pictureLogo_)
        shown = *pictureLogo_;
    // The preview is read back from the converted pixels, not from the picture.
    previewImage_ = bootlogo::toImage(shown, screen_);
    const int zoom = screen_.width < 200 ? 2 : 1;
    preview_->setPixmap(QPixmap::fromImage(previewImage_.scaled(
        previewImage_.size() * zoom, Qt::IgnoreAspectRatio, Qt::FastTransformation)));

    const bool idle = !writer_ && !done_;
    const bool pictureChosen = picture_->isChecked();
    current_->setVisible(pictureChosen && pictureOnIpod_);
    fit_->setEnabled(idle && pictureChosen && !source_.isNull());
    fill_->setEnabled(idle && pictureChosen && !source_.isNull());
    for (QWidget *widget : {static_cast<QWidget *>(tacetos_), static_cast<QWidget *>(picture_),
                            static_cast<QWidget *>(black_), static_cast<QWidget *>(choose_)})
        widget->setEnabled(idle);
    cancel_->setEnabled(!writer_);
    put_->setEnabled(done_ || (idle && (!pictureChosen || pictureLogo_)));
}

// Section 8.4, in a worker thread.
void BootLogoDialog::put()
{
    const bool remove = tacetos_->isChecked();
    QByteArray file;
    if (black_->isChecked())
        file = bootlogo::encode(bootlogo::Logo{});
    else if (picture_->isChecked() && pictureLogo_)
        file = bootlogo::encode(*pictureLogo_);
    else if (!remove)
        return;

    auto error = std::make_shared<QString>();
    const QString folder = folder_;
    writer_ = QThread::create([remove, file, folder, error] {
        QString reason;
        const bool ok = remove ? bootlogo::removeLogo(folder, &reason)
                               : bootlogo::writeLogo(folder, file, &reason);
        if (!ok)
            *error = reason.isEmpty() ? htr("unknown error") : reason;
    });
    connect(writer_, &QThread::finished, this, [this, error] {
        writer_->deleteLater();
        writer_ = nullptr;
        finishWrite(*error);
    });
    setMessage(QString(), false);
    writer_->start();
    refresh();
}

void BootLogoDialog::finishWrite(const QString &error)
{
    if (error.isEmpty()) {
        done_ = true;
        setMessage(htr("Done. The new logo shows the next time the iPod starts."), false);
        put_->setText(htr("Close"));
        cancel_->hide();
        if (picture_->isChecked())
            pictureOnIpod_ = true;
    } else {
        setMessage(htr("The logo could not be written to the iPod: %1").arg(error), true);
    }
    refresh();
    emit writeFinished(error);
}

void BootLogoDialog::reject()
{
    // The write goes to its end: closing now would not stop it.
    if (writer_)
        return;
    QDialog::reject();
}

QString BootLogoDialog::message() const
{
    return message_->isHidden() ? QString() : message_->text();
}

void BootLogoDialog::setMessage(const QString &text, bool danger)
{
    message_->setText(text);
    message_->setHidden(text.isEmpty());
    setRole(message_, danger ? "danger" : "");
}

} // namespace tacetos
