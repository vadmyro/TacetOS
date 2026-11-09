// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "models.h"

#include <QList>
#include <QString>
#include <QStringList>

// The exact iPod inside a model (docs/specs/detection.md section 8.4): "iPod
// Classic 120 GB (late 2008)" and not only "iPod Classic". Pure functions over
// the variants of models/models.toml. A variant never changes the model and
// never enables or blocks anything that writes.

namespace tacetos::device {

// Which clue gave the variant. Only the logs and the support see it.
enum class VariantSource { None, SysInfo, Capacity };

struct VariantMatch
{
    VariantSource source = VariantSource::None;
    // Empty when the clue left several variants of one name (the Classic 160 GB),
    // or none.
    QString key;
    // The name of the variant, or the one name that the variants left have in
    // common. Empty when source is None.
    QString name;
    // Release period of `key`. Empty when key is empty.
    QString released;
    // The Apple model number names a variant but the disk is not of its size
    // (rule 2 of section 8.4): the storage was swapped.
    bool storageChanged = false;
    // Why, for the logs.
    QStringList log;
};

// Section 8.4 clue 2: the model number string of the Apple model file (a
// region letter, then four characters) as the number of a variant: first
// letter dropped, upper case. Empty when the string has nothing after it.
QString appleModelNumber(const QString &modelNumStr);

// Section 8.4 clue 3: the disk size in bytes is within the window of a
// capacity in GB. Never computed from sectors.
bool capacityMatches(int capacityGb, qint64 diskBytes);

// Sections 8.4 clues 2 and 3, for the variants of one decided model. Empty
// `modelNumStr` means no clue from the Apple file, 0 `diskBytes` no clue from
// the disk.
VariantMatch identifyVariant(const Model &model, const QString &modelNumStr, qint64 diskBytes);

// Section 8.4 rule 3: among the candidate models of an undecided iPod, the one
// and only whose variants match the disk size. Empty when none or several do.
QString modelHintFromCapacity(const QList<Model> &models, const QStringList &candidates,
                              qint64 diskBytes);

} // namespace tacetos::device
