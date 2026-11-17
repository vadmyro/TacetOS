// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QString>
#include <QtGlobal>
#include <functional>

class QIODevice;

// The decoder of raw deflate data, written from the public description of the
// format (RFC 1951): stored blocks, and blocks with the fixed or their own
// Huffman codes.

namespace tacetos::archive {

// Takes each piece of output. Returning false stops the decoding.
using Sink = std::function<bool(const char *data, qint64 size)>;

// Reads at most `packedSize` bytes of `input` from its current position, and
// gives the bytes they stand for to `sink`, by pieces. More than `limit` bytes
// of output is an error. Returns false with the reason in `error`.
bool inflate(QIODevice &input, qint64 packedSize, qint64 limit, const Sink &sink, QString *error);

} // namespace tacetos::archive
