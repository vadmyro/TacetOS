// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "toml.h"

#include <QList>
#include <QString>
#include <QStringList>

// The part of models/models.toml that detection needs (docs/specs/detection.md
// section 3.2): which USB ids belong to which models.

namespace tacetos::device {

// One capacity of a model that Apple sold, in one release period (models/
// README.md, `[model.variant.<key>]`; docs/specs/detection.md section 8.4).
struct Variant
{
    QString key;
    // The name shown, with the capacity.
    QString name;
    int capacityGb = 0;
    // As shown to the user: a year, or "late 2008". A hint, never evidence.
    QString released;
    // Apple model numbers, upper case, without the region letter.
    QStringList appleModels;
};

struct Model
{
    QString id;
    QString family;
    QString name;
    // The firmware build target of this model, as an info file names it.
    QString bootloaderTarget;
    // USB ids as "vendor:product", four lowercase hexadecimal digits each.
    QStringList usbNormal;
    QStringList usbDiskMode;
    QStringList usbDfu;
    // Sorted by key.
    QList<Variant> variants;
};

struct ModelList
{
    QList<Model> models;
    // Empty when the list was read without a problem.
    QString error;
};

ModelList modelsFromDocument(const TomlDocument &doc);
ModelList loadModels(const QString &path);

// The `mode` field of a detected iPod. None is also what an id that is in no
// model gives: the caller drops it.
enum class UsbMode { None, Normal, Dfu, Other };

struct UsbMatch
{
    UsbMode mode = UsbMode::None;
    // Ids of the models that have the USB id, in the order of models.toml.
    QStringList candidates;
};

// Maps a USB id to the models it can be (section 3.2). The id is compared in
// lower case, with spaces around it ignored.
UsbMatch matchUsbId(const QList<Model> &models, const QString &usbId);

} // namespace tacetos::device
