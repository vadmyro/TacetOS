// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "check.h"

#include "constants.h"
#include "journal.h"
#include "version.h"

namespace tacetos::update {

UpdateCheckResult resultOfRead(const ChannelRead &read, const QString &target,
                               const QString &ipodVersion)
{
    UpdateCheckResult result;
    if (read.outcome == ChannelRead::Outcome::Missing) {
        result.logLine =
            QStringLiteral("No update: the channel has no index (%1)").arg(read.reason);
        return result;
    }
    if (read.outcome == ChannelRead::Outcome::Failed) {
        result.logLine =
            QStringLiteral("No update: the index could not be read: %1").arg(read.reason);
        return result;
    }

    const IndexReading reading = parseIndex(read.data);
    if (!reading.ok) {
        result.logLine = QStringLiteral("No update: the index is refused: %1").arg(reading.error);
        return result;
    }
    const Index &index = reading.index;
    const IndexPackage *package = index.package(target);
    if (!package) {
        result.logLine = QStringLiteral("No update: the index of TacetOS %1 has no package for %2")
                             .arg(index.version, target);
        return result;
    }
    QString why;
    if (!isNewer(index.version, ipodVersion, &why)) {
        result.logLine =
            why.isEmpty()
                ? QStringLiteral("No update: TacetOS %1 of the channel is not newer than %2 on "
                                 "the iPod")
                      .arg(index.version, ipodVersion)
                : QStringLiteral("No update: %1").arg(why);
        return result;
    }

    result.update = true;
    result.version = index.version;
    result.package = *package;
    result.logLine = QStringLiteral("Update TacetOS %1 for %2 (the iPod has %3, index built %4)")
                         .arg(index.version, target, ipodVersion, index.built);
    return result;
}

UpdateCheck::UpdateCheck(QNetworkAccessManager *network, journal::Journal *journal, QObject *parent)
    : QObject(parent), m_reader(network), m_journal(journal)
{
    connect(&m_reader, &ChannelReader::finished, this, &UpdateCheck::onRead);
}

void UpdateCheck::check(const ChannelAddress &address, const QString &target,
                        const QString &ipodVersion)
{
    m_target = target;
    m_ipodVersion = ipodVersion;
    m_reader.read(address, QString(device::kUpdateIndexFile), device::kIndexMaxBytes);
}

void UpdateCheck::onRead(const ChannelRead &read)
{
    const UpdateCheckResult result = resultOfRead(read, m_target, m_ipodVersion);
    if (m_journal)
        m_journal->add(journal::Level::Info, journal::Domain::App, QString(), result.logLine);
    emit finished(result);
}

} // namespace tacetos::update
