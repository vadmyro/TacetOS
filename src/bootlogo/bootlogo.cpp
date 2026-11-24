// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "bootlogo.h"

#include "constants.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace tacetos::bootlogo {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("bootlogo", text);
}

constexpr qint64 kMaxPictureBytes = 64LL * 1024 * 1024;
constexpr int kMaxPictureSide = 16384;
constexpr int kGreyStep = 85;

const std::array<quint32, 256> &crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

int bytesPerPixel(Kind kind)
{
    return kind == Kind::Colour ? 2 : kind == Kind::Grey ? 1 : 0;
}

// Section 8.3 step 3.
quint16 toRgb565(QRgb rgb)
{
    const int r = (qRed(rgb) * 31 + 127) / 255;
    const int g = (qGreen(rgb) * 63 + 127) / 255;
    const int b = (qBlue(rgb) * 31 + 127) / 255;
    return static_cast<quint16>((r << 11) | (g << 5) | b);
}

// A division rounded to the nearest, halves away from zero.
int roundedDiv(int value, int by)
{
    return value >= 0 ? (value + by / 2) / by : -((-value + by / 2) / by);
}

// Section 8.3 step 4: Floyd-Steinberg to the 4 grey levels, every row from left to right. The
// errors are kept in sixteenths, so the weights 7, 3, 5 and 1 stay whole numbers.
QByteArray greyLevels(const QImage &image)
{
    const int w = image.width();
    const int h = image.height();
    QByteArray out(w * h, '\0');
    std::vector<int> row(w + 2, 0);
    std::vector<int> next(w + 2, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        std::fill(next.begin(), next.end(), 0);
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const int grey = (299 * qRed(p) + 587 * qGreen(p) + 114 * qBlue(p)) / 1000;
            const int value = grey + roundedDiv(row[x + 1], 16);
            const int level = std::clamp(roundedDiv(value, kGreyStep), 0, 3);
            const int error = value - level * kGreyStep;
            out[y * w + x] = static_cast<char>(level);
            row[x + 2] += error * 7;
            next[x] += error * 3;
            next[x + 1] += error * 5;
            next[x + 2] += error * 1;
        }
        std::swap(row, next);
    }
    return out;
}

} // namespace

quint32 crc32(QByteArrayView data)
{
    const auto &table = crcTable();
    quint32 crc = 0xFFFFFFFFU;
    for (const char byte : data)
        crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

QByteArray encode(const Logo &logo)
{
    const bool black = logo.kind == Kind::Black;
    QByteArray file(kHeaderSize, '\0');
    uchar *h = reinterpret_cast<uchar *>(file.data());
    memcpy(h, "TLGO", 4);
    h[4] = 1;
    h[5] = static_cast<uchar>(logo.kind);
    qToLittleEndian<quint16>(black ? 0 : logo.width, h + 6);
    qToLittleEndian<quint16>(black ? 0 : logo.height, h + 8);
    qToLittleEndian<quint32>(black ? 0 : crc32(logo.pixels), h + 12);
    if (!black)
        file.append(logo.pixels);
    return file;
}

std::optional<Logo> decode(QByteArrayView data, const Screen &screen)
{
    if (data.size() < kHeaderSize)
        return std::nullopt;
    const uchar *h = reinterpret_cast<const uchar *>(data.data());
    // Rule 2.
    if (memcmp(h, "TLGO", 4) != 0 || h[4] != 1 || qFromLittleEndian<quint16>(h + 10) != 0)
        return std::nullopt;
    // Rule 3.
    if (h[5] > 2)
        return std::nullopt;
    Logo logo;
    logo.kind = static_cast<Kind>(h[5]);
    if (logo.kind == Kind::Colour && !screen.colour)
        return std::nullopt;
    if (logo.kind == Kind::Grey && screen.colour)
        return std::nullopt;
    if (logo.kind == Kind::Black)
        return data.size() == kHeaderSize ? std::optional<Logo>(logo) : std::nullopt;
    // Rule 4.
    logo.width = qFromLittleEndian<quint16>(h + 6);
    logo.height = qFromLittleEndian<quint16>(h + 8);
    if (logo.width != screen.width || logo.height != screen.height)
        return std::nullopt;
    // Rule 5.
    const qsizetype size =
        kHeaderSize + qsizetype(logo.width) * logo.height * bytesPerPixel(logo.kind);
    if (data.size() != size)
        return std::nullopt;
    // Rule 6.
    const QByteArrayView pixels = data.sliced(kHeaderSize);
    if (crc32(pixels) != qFromLittleEndian<quint32>(h + 12))
        return std::nullopt;
    logo.pixels = pixels.toByteArray();
    return logo;
}

QImage toImage(const Logo &logo, const Screen &screen)
{
    QImage image(screen.width, screen.height, QImage::Format_RGB32);
    image.fill(Qt::black);
    if (logo.kind == Kind::Black || logo.width != screen.width || logo.height != screen.height)
        return image;
    const uchar *p = reinterpret_cast<const uchar *>(logo.pixels.constData());
    for (int y = 0; y < logo.height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < logo.width; ++x) {
            const int i = y * logo.width + x;
            if (logo.kind == Kind::Colour) {
                const quint16 v = qFromLittleEndian<quint16>(p + 2 * i);
                line[x] = qRgb(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63,
                               (v & 31) * 255 / 31);
            } else {
                const int g = (p[i] & 3) * kGreyStep;
                line[x] = qRgb(g, g, g);
            }
        }
    }
    return image;
}

Logo fromImage(const QImage &image, const Screen &screen, bool fill)
{
    // Step 2: scaled and centred on black, so the bars of Fit and the transparent pixels are
    // black.
    QImage canvas(screen.width, screen.height, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    if (!image.isNull()) {
        const QSize target =
            image.size().scaled(screen.width, screen.height,
                                fill ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        const QImage scaled = image.scaled(target.expandedTo(QSize(1, 1)), Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation);
        QPainter painter(&canvas);
        painter.drawImage((screen.width - scaled.width()) / 2,
                          (screen.height - scaled.height()) / 2, scaled);
    }

    Logo logo;
    logo.width = screen.width;
    logo.height = screen.height;
    if (screen.colour) {
        logo.kind = Kind::Colour;
        logo.pixels.resize(qsizetype(screen.width) * screen.height * 2);
        uchar *out = reinterpret_cast<uchar *>(logo.pixels.data());
        for (int y = 0; y < screen.height; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
            for (int x = 0; x < screen.width; ++x)
                qToLittleEndian<quint16>(toRgb565(line[x]), out + 2 * (y * screen.width + x));
        }
    } else {
        logo.kind = Kind::Grey;
        logo.pixels = greyLevels(canvas);
    }
    return logo;
}

Picture readPicture(const QString &path)
{
    Picture picture;
    const QString unreadable = tr("This file is not a picture the app can read");
    const QFileInfo info(path);
    if (!info.isFile()) {
        picture.error = unreadable;
        return picture;
    }
    if (info.size() > kMaxPictureBytes) {
        picture.error = tr("This picture is too large");
        return picture;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.width() > kMaxPictureSide || size.height() > kMaxPictureSide) {
        picture.error = tr("This picture is too large");
        return picture;
    }
    // The first frame only.
    QImage image = reader.read();
    if (image.isNull()) {
        picture.error = unreadable;
        return picture;
    }
    if (image.width() > kMaxPictureSide || image.height() > kMaxPictureSide) {
        picture.error = tr("This picture is too large");
        return picture;
    }
    picture.image = image;
    return picture;
}

QString logoPath(const QString &firmwareFolder)
{
    return QDir(firmwareFolder).filePath(device::kBootLogoFile);
}

OnIpod readLogo(const QString &firmwareFolder, const Screen &screen)
{
    OnIpod result;
    QFile file(logoPath(firmwareFolder));
    if (!file.exists())
        return result;
    // Rule 1: the largest file is 153 616 bytes, so a few more say it is not ours.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        result.found = Found::Unusable;
        return result;
    }
    const auto logo = decode(file.readAll(), screen);
    if (!logo)
        result.found = Found::Unusable;
    else if (logo->kind == Kind::Black)
        result.found = Found::Black;
    else {
        result.found = Found::Picture;
        result.logo = logo;
    }
    return result;
}

bool writeLogo(const QString &firmwareFolder, const QByteArray &file, QString *error)
{
    const QString path = logoPath(firmwareFolder);
    const QString temporary = path + QLatin1String(".new");
    const auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        QFile::remove(temporary);
        return false;
    };
    if (!QFileInfo(firmwareFolder).isDir())
        return fail(tr("the firmware folder is not on the iPod"));
    if (QFileInfo::exists(temporary) && !QFile::remove(temporary))
        return fail(tr("a file left by an earlier write cannot be removed"));

    QFile out(temporary);
    if (!out.open(QIODevice::WriteOnly) || out.write(file) != file.size() || !out.flush())
        return fail(out.errorString());
    out.close();
    if (out.error() != QFileDevice::NoError)
        return fail(out.errorString());

    // FAT has no rename over a file: the old one goes first. A cut here leaves the TacetOS logo.
    if (QFileInfo::exists(path) && !QFile::remove(path))
        return fail(tr("the old logo file cannot be replaced"));
    if (!QFile::rename(temporary, path))
        return fail(tr("the new logo file cannot be renamed"));

    QFile back(path);
    if (!back.open(QIODevice::ReadOnly) || back.readAll() != file) {
        if (error)
            *error = tr("the logo file read back is not the one written");
        return false;
    }
    return true;
}

bool removeLogo(const QString &firmwareFolder, QString *error)
{
    const QString path = logoPath(firmwareFolder);
    for (const QString &name : {path + QLatin1String(".new"), path}) {
        if (QFileInfo::exists(name) && !QFile::remove(name)) {
            if (error)
                *error = tr("the logo file cannot be removed");
            return false;
        }
    }
    if (!QFileInfo(firmwareFolder).isDir()) {
        if (error)
            *error = tr("the firmware folder is not on the iPod");
        return false;
    }
    return true;
}

} // namespace tacetos::bootlogo
