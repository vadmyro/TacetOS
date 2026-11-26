// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <optional>

// The versions of TacetOS (docs/specs/update-channel.md section 5): `X.Y.Z`,
// then optionally `-dev`, then optionally `+g` and the commit.

namespace tacetos::update {

struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    // A build outside a tag.
    bool development = false;
    // 12 lowercase hexadecimal digits, or empty. Never compared.
    QString commit;
};

// The version written in `text`, or nothing when it cannot be compared: a
// leading zero, a number over 65535, a missing or extra part, a commit that is
// not 12 lowercase hexadecimal digits, a space anywhere.
std::optional<Version> parseVersion(const QString &text);

// Below zero when `a` is older than `b`, zero when they are equal, above zero
// when `a` is newer. The commit never counts.
int compareVersions(const Version &a, const Version &b);

// Section 5 "Newer": true when `channel` is newer than `ipod`. A version that
// cannot be compared, on either side, is never newer: `why` then says which.
bool isNewer(const QString &channel, const QString &ipod, QString *why = nullptr);

} // namespace tacetos::update
