// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include "constants.h"
#include "toml.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

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

// The screen of a model (`[model.screen]`).
struct ModelScreen
{
    int width = 0;
    int height = 0;
    bool colour = false;
};

struct Model
{
    QString id;
    QString family;
    QString name;
    // The firmware build target of this model, as an info file names it.
    QString bootloaderTarget;
    // The hidden folder of the firmware at the root of the music volume
    // (`firmware_folder`): the default one when models.toml has none.
    QString firmwareFolder = QString(kFirmwareFolder);
    // USB ids as "vendor:product", four lowercase hexadecimal digits each.
    QStringList usbNormal;
    QStringList usbDiskMode;
    QStringList usbDfu;
    // Sorted by key.
    QList<Variant> variants;
    // None when models.toml gives no size.
    std::optional<ModelScreen> screen;
};

struct ModelList
{
    QList<Model> models;
    // Empty when the list was read without a problem.
    QString error;
};

ModelList modelsFromDocument(const TomlDocument &doc);
ModelList loadModels(const QString &path);

// Every folder a firmware of these models can be in, each once: the models' own
// folders in the order of models.toml, then the default one, which older
// installs of every model use.
QStringList firmwareFolders(const QList<Model> &models);

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
