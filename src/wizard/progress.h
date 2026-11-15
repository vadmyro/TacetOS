// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "flow.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// The progress file of a flow (docs/specs/install-wizard.md section 3).

namespace tacetos::wizard {

// Section 3.2: where the step at `current` is.
enum class StepStatus { Running, Waiting, Failed, Next };

QString statusName(StepStatus status);

struct Progress
{
    QString appVersion;
    QString serial;
    QString model;
    FlowShape shape;
    QStringList steps;
    int current = 0;
    StepStatus status = StepStatus::Next;
    QString error;
    QString backup;
    QString packageManifest;
    QString packageSha256;
    QJsonObject facts;
    QDateTime started;
    QDateTime updated;
};

QJsonObject toJson(const Progress &progress);

// Section 3.4: reads a progress file for `serial`. Returns the reason it is
// refused, or an empty text when `out` holds a progress that can be trusted.
QString parseProgress(const QByteArray &bytes, const QString &serial, Progress *out);

struct ProgressRead
{
    enum class Outcome { None, Ok, Refused };
    Outcome outcome = Outcome::None;
    Progress progress;
    QString reason;
};

// The progress files of one data folder.
class ProgressStore
{
public:
    explicit ProgressStore(const QString &dataDir);

    // Section 5.1: every character that is not a letter, a digit, `-` or `_`
    // becomes `_`.
    static QString safeSerial(const QString &serial);

    QString folder() const;
    QString filePath(const QString &serial) const;
    QString lockPath(const QString &serial) const;

    bool exists(const QString &serial) const;

    // Section 3.1: writes the whole file in one move.
    bool save(const Progress &progress, QString *error = nullptr) const;

    // Section 3.4: a refused file is renamed with the suffix of refused files.
    ProgressRead load(const QString &serial) const;

    bool remove(const QString &serial) const;

private:
    QString m_dataDir;
};

} // namespace tacetos::wizard
