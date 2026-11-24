// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "bootlogo.h"
#include "models.h"

#include <QDialog>
#include <QImage>
#include <optional>

class QLabel;
class QPushButton;
class QRadioButton;
class QThread;

namespace tacetos {

// The dialog of the boot logo (docs/specs/boot-screen.md section 8.2): TacetOS logo, the user's
// picture or a black screen, a preview of the pixels that go to the iPod, and the write in a
// worker thread (section 8.4).
class BootLogoDialog : public QDialog
{
    Q_OBJECT

public:
    // `model` must have a screen. `mountPoint` is the mounted volume of the iPod.
    BootLogoDialog(const device::Model &model, const QString &mountPoint,
                   QWidget *parent = nullptr);
    ~BootLogoDialog() override;

    // What Choose does with the file the user picked. False when the picture is refused.
    bool usePicture(const QString &path);

    // The logo of the TacetOS banner for a screen, as section 4.1 draws it.
    static bootlogo::Logo tacetosLogo(const bootlogo::Screen &screen);

    QRadioButton *tacetosButton() const { return tacetos_; }
    QRadioButton *pictureButton() const { return picture_; }
    QRadioButton *blackButton() const { return black_; }
    QRadioButton *fitButton() const { return fit_; }
    QRadioButton *fillButton() const { return fill_; }
    QPushButton *putButton() const { return put_; }
    QPushButton *cancelButton() const { return cancel_; }
    QString message() const;
    // The pixels the preview shows, at the size of the screen.
    QImage previewImage() const { return previewImage_; }
    bool writing() const { return writer_ != nullptr; }

signals:
    // The write ended; `error` is empty on success.
    void writeFinished(const QString &error);

public slots:
    void reject() override;

private:
    void readIpod();
    void choose();
    void convert();
    void refresh();
    void put();
    void finishWrite(const QString &error);
    void setMessage(const QString &text, bool danger);

    bootlogo::Screen screen_;
    QString folder_;
    // The picture chosen in this dialog, before its conversion.
    QImage source_;
    // What My picture would write: the chosen picture converted, or the one on the iPod.
    std::optional<bootlogo::Logo> pictureLogo_;
    bool pictureOnIpod_ = false;
    bool done_ = false;
    QImage previewImage_;

    QRadioButton *tacetos_;
    QRadioButton *picture_;
    QPushButton *choose_;
    QLabel *current_;
    QRadioButton *fit_;
    QRadioButton *fill_;
    QRadioButton *black_;
    QLabel *preview_;
    QLabel *message_;
    QPushButton *cancel_;
    QPushButton *put_;
    QThread *writer_ = nullptr;
};

} // namespace tacetos
