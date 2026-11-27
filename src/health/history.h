// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "files.h"
#include "reading.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

// The readings the app keeps on the computer for one iPod
// (docs/specs/health.md section 7): `health/<key>.json` in the data folder
// given by the caller. Never sent anywhere, never written on the iPod.

namespace tacetos::health {

struct HistoryReading
{
    QDateTime readAt;
    Event event = Event::Usb;
    // Empty for `unset`.
    QString clock;
    std::optional<int> batteryLevel;
    int batteryMv = 0;
    qint64 runtimeS = 0;
    qint64 topRuntimeS = 0;
    Charging charging = Charging::No;
    // The raw values of the ids of section 5.3 the block has. None without a
    // usable block.
    std::optional<QMap<int, quint32>> smart;
    std::optional<double> estimateH;
    // Lower case hexadecimal. The charge one is empty without a usable charge
    // file.
    QString healthSha256;
    QString chargeSha256;
};

struct History
{
    QList<HistoryReading> readings;

    // The estimates of the readings, in the order they were read.
    QList<double> estimates() const;
};

// The first 16 hexadecimal digits of the sha256 of `disk:` and the serial of
// the identify block, else of `usb:` and `usbSerial`. Empty with neither:
// there is no history.
QString historyKey(const HealthReading &reading, const QString &usbSerial);

// Pure: adds `reading` unless its health file is not usable or is the one of
// the last reading. Its estimate is dropped when an earlier reading with an
// estimate had the same charge file. The oldest readings go past
// kHistoryMax. Returns whether a reading was added.
bool addReading(History *history, const HealthReading &reading);

QByteArray historyToJson(const History &history);

struct HistoryParse
{
    bool ok = false;
    History history;
    // Why the text is not a history. Empty when ok.
    QString error;
};

HistoryParse historyFromJson(const QByteArray &data);

// `<dataFolder>/health/<key>.json`.
QString historyPath(const QString &dataFolder, const QString &key);

// Reads the history of `key`: an empty one when there is no file yet. A file
// that is not a history is set aside as `<key>.json.bad`, with one log line,
// and an empty history is returned. None when the file is there but cannot be
// read: it must not be replaced then.
std::optional<History> loadHistory(const QString &dataFolder, const QString &key, QStringList *log);

// Writes the whole history through QSaveFile. One log line when it fails.
bool saveHistory(const QString &dataFolder, const QString &key, const History &history,
                 QStringList *log);

// Loads the history of `key`, adds `reading` and saves it when it was added.
// Returns the history after, or the reading alone in a history that is not
// kept when `key` is empty or the file cannot be read.
History recordReading(const QString &dataFolder, const QString &key, const HealthReading &reading,
                      QStringList *log);

} // namespace tacetos::health
