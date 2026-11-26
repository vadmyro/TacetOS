// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "address.h"

#include "constants.h"

#include <QDir>
#include <QFileInfo>
#include <optional>

namespace tacetos::update {

namespace {

QUrl asFolderUrl(QUrl url)
{
    if (!url.path().endsWith(QLatin1Char('/')))
        url.setPath(url.path() + QLatin1Char('/'));
    return url;
}

// The address of `value`, or nothing when it is none of the kinds of section 4.
std::optional<ChannelAddress> readValue(const QString &value)
{
    ChannelAddress address;
    // A path first: `C:\updates` would read as an address with the scheme `c`.
    QString path;
    if (QDir::isAbsolutePath(value)) {
        path = value;
    } else {
        const QUrl url(value, QUrl::StrictMode);
        if (!url.isValid() || url.hasQuery() || url.hasFragment())
            return std::nullopt;
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1String("file")) {
            path = url.toLocalFile();
        } else if (scheme == QLatin1String("https") && !url.host().isEmpty()) {
            address.kind = ChannelAddress::Kind::Remote;
            address.url = asFolderUrl(url);
            return address;
        } else if (scheme == QLatin1String("http") && isThisComputer(url)) {
            address.kind = ChannelAddress::Kind::Local;
            address.url = asFolderUrl(url);
            return address;
        } else {
            return std::nullopt;
        }
    }

    const QFileInfo folder(path);
    if (path.isEmpty() || !folder.isAbsolute() || !folder.isDir())
        return std::nullopt;
    address.kind = ChannelAddress::Kind::Folder;
    address.folder = folder.absoluteFilePath();
    return address;
}

} // namespace

QString ChannelAddress::text() const
{
    return kind == Kind::Folder ? QDir::toNativeSeparators(folder) : url.toString();
}

ChannelAddress defaultChannel()
{
    ChannelAddress address;
    address.kind = ChannelAddress::Kind::Remote;
    address.url = QUrl(QString(device::kUpdateChannel));
    return address;
}

bool isThisComputer(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("::1") ||
           host == QLatin1String("localhost");
}

ChannelChoice chooseChannel(const QString &value, bool packageBuild)
{
    ChannelChoice choice;
    choice.address = defaultChannel();
    const QString name = QString(device::kUpdateChannelVariable);
    const QString given = value.trimmed();
    if (!given.isEmpty()) {
        if (packageBuild) {
            choice.log.append(QStringLiteral("%1 is ignored by a packaged build").arg(name));
        } else if (const std::optional<ChannelAddress> address = readValue(given)) {
            choice.address = *address;
        } else {
            choice.log.append(QStringLiteral("%1 is ignored: \"%2\" is not an https address, an "
                                             "http address on this computer or a folder")
                                  .arg(name, given));
        }
    }
    choice.log.append(QStringLiteral("Update channel: %1").arg(choice.address.text()));
    return choice;
}

ChannelChoice channelOfThisBuild()
{
#ifdef TACETOS_PACKAGE_BUILD
    constexpr bool packageBuild = true;
#else
    constexpr bool packageBuild = false;
#endif
    return chooseChannel(qEnvironmentVariable(device::kUpdateChannelVariable.data()), packageBuild);
}

} // namespace tacetos::update
