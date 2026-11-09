// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "variant.h"

#include "constants.h"

namespace tacetos::device {

QString appleModelNumber(const QString &modelNumStr)
{
    QString text = modelNumStr.trimmed();
    if (!text.isEmpty() && text.at(0).isLetter())
        text.remove(0, 1);
    return text.toUpper();
}

bool capacityMatches(int capacityGb, qint64 diskBytes)
{
    if (capacityGb <= 0 || diskBytes <= 0)
        return false;
    const double gigabytes = static_cast<double>(diskBytes) / 1e9;
    return gigabytes >= kCapacityLow * capacityGb && gigabytes <= kCapacityHigh * capacityGb;
}

VariantMatch identifyVariant(const Model &model, const QString &modelNumStr, qint64 diskBytes)
{
    VariantMatch match;

    // Clue 2: the model number of the Apple file.
    const QString number = appleModelNumber(modelNumStr);
    const Variant *bySysInfo = nullptr;
    if (!number.isEmpty()) {
        for (const Variant &v : model.variants) {
            if (v.appleModels.contains(number)) {
                bySysInfo = &v;
                break;
            }
        }
        if (!bySysInfo) {
            match.log << QStringLiteral("variant: model number %1 is in no variant of %2")
                             .arg(number, model.id);
        }
    }

    // Clue 3: the capacity. Computed even when clue 2 decided, for the logs and
    // for rule 2.
    QList<const Variant *> byCapacity;
    for (const Variant &v : model.variants) {
        if (capacityMatches(v.capacityGb, diskBytes))
            byCapacity.append(&v);
    }

    if (bySysInfo) {
        match.source = VariantSource::SysInfo;
        match.key = bySysInfo->key;
        match.name = bySysInfo->name;
        match.released = bySysInfo->released;
        match.log << QStringLiteral("variant: %1 by model number %2").arg(match.key, number);
        if (diskBytes > 0 && !capacityMatches(bySysInfo->capacityGb, diskBytes)) {
            match.storageChanged = true;
            match.log << QStringLiteral("variant: the disk (%1 bytes) is not of %2 GB, storage "
                                        "changed")
                             .arg(diskBytes)
                             .arg(bySysInfo->capacityGb);
        }
        return match;
    }

    if (byCapacity.size() == 1) {
        const Variant &v = *byCapacity.first();
        match.source = VariantSource::Capacity;
        match.key = v.key;
        match.name = v.name;
        match.released = v.released;
        match.log << QStringLiteral("variant: %1 by capacity").arg(v.key);
    } else if (byCapacity.size() > 1) {
        bool sameName = true;
        for (const Variant *v : byCapacity)
            sameName = sameName && v->name == byCapacity.first()->name;
        if (sameName) {
            // Showing the name without a release period is right whatever the
            // year is.
            match.source = VariantSource::Capacity;
            match.name = byCapacity.first()->name;
            match.log << QStringLiteral("variant: %1 variants of one name, no release period")
                             .arg(byCapacity.size());
        } else {
            match.log << QStringLiteral("variant: %1 variants of other names fit the disk")
                             .arg(byCapacity.size());
        }
    } else if (diskBytes > 0) {
        match.log << QStringLiteral("variant: no variant of %1 fits a disk of %2 bytes")
                         .arg(model.id)
                         .arg(diskBytes);
    }
    return match;
}

QString modelHintFromCapacity(const QList<Model> &models, const QStringList &candidates,
                              qint64 diskBytes)
{
    QString hint;
    for (const Model &m : models) {
        if (!candidates.contains(m.id))
            continue;
        bool fits = false;
        for (const Variant &v : m.variants)
            fits = fits || capacityMatches(v.capacityGb, diskBytes);
        if (!fits)
            continue;
        if (!hint.isEmpty())
            return QString();
        hint = m.id;
    }
    return hint;
}

} // namespace tacetos::device
