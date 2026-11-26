// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

// The address of the channel (docs/specs/update-channel.md section 4).

namespace tacetos::update {

struct ChannelAddress
{
    enum class Kind {
        // An `https://` address, the default one included.
        Remote,
        // An `http://` address on the computer itself: the fake server of the tests.
        Local,
        // A folder on the computer: the local channel.
        Folder,
    };

    Kind kind = Kind::Remote;
    // Remote and Local: the address of the folder, ending with `/`.
    QUrl url;
    // Folder: its absolute path.
    QString folder;

    // As written in the log.
    QString text() const;
};

struct ChannelChoice
{
    ChannelAddress address;
    // What to log, once per launch: why a value was ignored, then the address
    // in use.
    QStringList log;
};

// The default address, the latest release of the repository.
ChannelAddress defaultChannel();

// True for `127.0.0.1`, `::1` and `localhost`.
bool isThisComputer(const QUrl &url);

// The address given by `value`, the value of kUpdateChannelVariable (empty when
// it is not set). A packaged build always keeps the default address. A value
// that is not an `https://` address, an `http://` address on the computer, the
// absolute path of a folder that exists or a `file://` address of one, is
// ignored.
ChannelChoice chooseChannel(const QString &value, bool packageBuild);

// chooseChannel with the variable of the environment and the kind of this build
// (the CMake option TACETOS_PACKAGE_BUILD).
ChannelChoice channelOfThisBuild();

} // namespace tacetos::update
