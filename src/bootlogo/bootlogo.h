// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QImage>
#include <QString>
#include <optional>

// The boot logo file of the firmware folder (docs/specs/boot-screen.md section 3), the
// conversion of a picture into it (section 8.3) and its writing (section 8.4). No window here:
// the tests run it without a screen.

namespace tacetos::bootlogo {

// Section 3.2, the byte at offset 5.
enum class Kind : quint8 { Black = 0, Colour = 1, Grey = 2 };

// The screen a logo is made for (`[model.screen]` of models.toml).
struct Screen
{
    int width = 0;
    int height = 0;
    bool colour = false;
};

inline constexpr int kHeaderSize = 16;

// What a logo file holds. `pixels` are the bytes of the file after the header.
struct Logo
{
    Kind kind = Kind::Black;
    int width = 0;
    int height = 0;
    QByteArray pixels;
};

// The CRC-32 of zip (polynomial 0xEDB88320, start 0xFFFFFFFF, result inverted).
quint32 crc32(QByteArrayView data);

// The whole file of section 3.2, header and pixels.
QByteArray encode(const Logo &logo);

// The logo of a file, or nothing when one of the rules of section 3.3 refuses it on `screen`.
std::optional<Logo> decode(QByteArrayView data, const Screen &screen);

// The picture a logo shows on the iPod, at the size of its screen: what the preview draws.
QImage toImage(const Logo &logo, const Screen &screen);

// Section 8.3, steps 2 to 4: a picture made into a logo for `screen`, Fit or Fill.
Logo fromImage(const QImage &image, const Screen &screen, bool fill);

// Section 8.3, step 1: a picture file read with its rotation applied, or the reason it is
// refused.
struct Picture
{
    QImage image;
    QString error;
};
Picture readPicture(const QString &path);

// Where the logo file is in a firmware folder.
QString logoPath(const QString &firmwareFolder);

// What the firmware folder holds when the dialog opens (section 8.2).
enum class Found { None, Black, Picture, Unusable };
struct OnIpod
{
    Found found = Found::None;
    // For Picture only.
    std::optional<Logo> logo;
};
OnIpod readLogo(const QString &firmwareFolder, const Screen &screen);

// Section 8.4. Both remove a `.new` left by an earlier cut, and give the reason when they fail.
bool writeLogo(const QString &firmwareFolder, const QByteArray &file, QString *error);
bool removeLogo(const QString &firmwareFolder, QString *error);

} // namespace tacetos::bootlogo
