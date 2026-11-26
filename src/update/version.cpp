// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "version.h"

#include <QRegularExpression>

namespace tacetos::update {

namespace {

constexpr int kNumberMax = 65535;

} // namespace

std::optional<Version> parseVersion(const QString &text)
{
    // At most 5 digits each, so that the value fits before it is compared with
    // the largest number.
    static const QRegularExpression shape(QRegularExpression::anchoredPattern(
        QStringLiteral("(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})"
                       "(-dev)?(?:\\+g([0-9a-f]{12}))?")));
    const QRegularExpressionMatch match = shape.match(text);
    if (!match.hasMatch())
        return std::nullopt;

    Version version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).toInt();
    version.patch = match.captured(3).toInt();
    if (version.major > kNumberMax || version.minor > kNumberMax || version.patch > kNumberMax)
        return std::nullopt;
    version.development = !match.captured(4).isEmpty();
    version.commit = match.captured(5);
    return version;
}

int compareVersions(const Version &a, const Version &b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    // With the same three numbers, the version without `-dev` is the newer.
    if (a.development != b.development)
        return a.development ? -1 : 1;
    return 0;
}

bool isNewer(const QString &channel, const QString &ipod, QString *why)
{
    const std::optional<Version> fromChannel = parseVersion(channel);
    const std::optional<Version> onIpod = parseVersion(ipod);
    if (!fromChannel || !onIpod) {
        if (why) {
            *why =
                !fromChannel
                    ? QStringLiteral("the version of the channel \"%1\" cannot be compared")
                          .arg(channel)
                    : QStringLiteral("the version of the iPod \"%1\" cannot be compared").arg(ipod);
        }
        return false;
    }
    return compareVersions(*fromChannel, *onIpod) > 0;
}

} // namespace tacetos::update
