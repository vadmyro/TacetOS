// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "inflate.h"

#include <QByteArray>
#include <QIODevice>
#include <array>
#include <vector>

namespace tacetos::archive {

namespace {

// How much input is read from the device at once, and how much output is
// gathered before it goes to the sink.
constexpr qint64 kReadPiece = 64 * 1024;
constexpr int kWritePiece = 64 * 1024;

// RFC 1951 section 3.2: a match reaches at most this far back.
constexpr int kWindow = 32 * 1024;

// Section 3.2.7: the longest code, the symbols of each alphabet, and the order
// in which the lengths of the code length alphabet are sent.
constexpr int kLongestCode = 15;
constexpr int kLiteralSymbols = 288;
constexpr int kDistanceSymbols = 32;
constexpr int kEndOfBlock = 256;

// What `Code::next()` gives instead of a symbol.
constexpr int kNoCode = -1;
constexpr int kInputEnded = -2;
constexpr std::array<quint8, 19> kLengthCodeOrder = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                     11, 4,  12, 3, 13, 2, 14, 1, 15};

// Section 3.2.5: the base and the extra bits of each length symbol from 257
// and of each distance symbol. The extra bits grow by one every 4 lengths and
// every 2 distances; each base follows the end of the range before it.
struct Range
{
    int base;
    int extra;
};

struct Ranges
{
    std::array<Range, 29> lengths{};
    std::array<Range, 30> distances{};
};

constexpr Ranges ranges()
{
    Ranges r;
    int base = 3;
    for (int i = 0; i < 28; ++i) {
        const int extra = i < 8 ? 0 : i / 4 - 1;
        r.lengths[i] = {base, extra};
        base += 1 << extra;
    }
    // The last length symbol stands for 258 alone.
    r.lengths[28] = {258, 0};
    base = 1;
    for (int i = 0; i < 30; ++i) {
        const int extra = i < 4 ? 0 : i / 2 - 1;
        r.distances[i] = {base, extra};
        base += 1 << extra;
    }
    return r;
}

constexpr Ranges kRanges = ranges();

// The input, read bit by bit, lowest bit of each byte first.
class Bits
{
public:
    Bits(QIODevice &device, qint64 size) : m_device(device), m_left(size) {}

    // The next `count` bits (at most 24) as a number, the first one lowest.
    bool take(int count, quint32 *value)
    {
        if (!fill(count))
            return false;
        *value = quint32(m_held & ((quint64(1) << count) - 1));
        m_held >>= count;
        m_count -= count;
        return true;
    }

    // The next `count` bits without taking them, zeros past the end of the
    // input. Returns how many of them are real.
    int look(int count, quint32 *value)
    {
        fill(count);
        *value = quint32(m_held & ((quint64(1) << count) - 1));
        return std::min(count, m_count);
    }

    void skip(int count)
    {
        m_held >>= count;
        m_count -= count;
    }

    // Section 3.2.4: a stored block starts on a whole byte.
    void toWholeByte() { skip(m_count % 8); }

private:
    bool fill(int count)
    {
        while (m_count < count) {
            if (m_at == m_piece.size()) {
                if (m_left <= 0)
                    return false;
                m_piece = m_device.read(std::min(m_left, kReadPiece));
                if (m_piece.isEmpty()) {
                    m_left = 0;
                    return false;
                }
                m_left -= m_piece.size();
                m_at = 0;
            }
            m_held |= quint64(quint8(m_piece.at(m_at++))) << m_count;
            m_count += 8;
        }
        return true;
    }

    QIODevice &m_device;
    qint64 m_left;
    QByteArray m_piece;
    qsizetype m_at = 0;
    quint64 m_held = 0;
    int m_count = 0;
};

// A prefix code, from the length of the code of each symbol (section 3.2.2).
// Decoding looks the next bits up in a table that has one cell for every
// value of `longest` bits: the cells of a code of n bits repeat every 2^n.
class Code
{
public:
    // False when the lengths make no prefix code: more codes of some length
    // than the shorter ones leave room for.
    bool build(const quint8 *lengths, int symbols)
    {
        std::array<int, kLongestCode + 1> perLength{};
        for (int s = 0; s < symbols; ++s)
            ++perLength[lengths[s]];
        perLength[0] = 0;
        int room = 1;
        m_longest = 0;
        for (int n = 1; n <= kLongestCode; ++n) {
            room = room * 2 - perLength[n];
            if (room < 0)
                return false;
            if (perLength[n] > 0)
                m_longest = n;
        }
        // The first code of each length, as the RFC counts them.
        std::array<quint32, kLongestCode + 2> next{};
        quint32 code = 0;
        for (int n = 1; n <= kLongestCode; ++n) {
            code = (code + quint32(perLength[n - 1])) << 1;
            next[n] = code;
        }
        m_cells.assign(size_t(1) << m_longest, 0);
        for (int s = 0; s < symbols; ++s) {
            const int n = lengths[s];
            if (n == 0)
                continue;
            // The code is sent from its highest bit: in the order the bits
            // are taken, it reads backwards.
            const quint32 sent = reversed(next[n]++, n);
            for (size_t cell = sent; cell < m_cells.size(); cell += size_t(1) << n)
                m_cells[cell] = quint16((s << 4) | n);
        }
        return true;
    }

    // The next symbol, `kNoCode` when the bits are no code, `kInputEnded`
    // when the input ends inside a code.
    int next(Bits &bits) const
    {
        if (m_longest == 0)
            return kNoCode;
        quint32 value = 0;
        const int real = bits.look(m_longest, &value);
        const quint16 cell = m_cells[value];
        const int n = cell & 0xF;
        if (n == 0)
            return real < m_longest ? kInputEnded : kNoCode;
        if (n > real)
            return kInputEnded;
        bits.skip(n);
        return cell >> 4;
    }

private:
    static quint32 reversed(quint32 code, int n)
    {
        quint32 out = 0;
        for (int i = 0; i < n; ++i) {
            out = (out << 1) | (code & 1u);
            code >>= 1;
        }
        return out;
    }

    int m_longest = 0;
    // The symbol times 16, plus the length of its code. 0: no code here.
    std::vector<quint16> m_cells;
};

// The output: the last `kWindow` bytes for the matches, and the bytes not yet
// given to the sink.
class Output
{
public:
    Output(qint64 limit, const Sink &sink) : m_limit(limit), m_sink(sink), m_ring(kWindow)
    {
        m_waiting.reserve(kWritePiece);
    }

    bool put(char byte)
    {
        if (m_total >= m_limit)
            return fail(QStringLiteral("the data unpacks to more than its declared size"));
        m_ring[size_t(m_total % kWindow)] = byte;
        ++m_total;
        m_waiting.append(byte);
        return m_waiting.size() < kWritePiece || flush();
    }

    // Section 3.2.3: `length` bytes copied from `distance` bytes back.
    bool repeat(int distance, int length)
    {
        if (distance > m_total || distance > kWindow)
            return fail(QStringLiteral("a match reaches before the start of the data"));
        for (int i = 0; i < length; ++i) {
            if (!put(m_ring[size_t((m_total - distance) % kWindow)]))
                return false;
        }
        return true;
    }

    bool flush()
    {
        if (m_waiting.isEmpty())
            return true;
        if (m_sink && !m_sink(m_waiting.constData(), m_waiting.size()))
            return fail(QStringLiteral("stopped"));
        m_waiting.clear();
        return true;
    }

    bool fail(const QString &why)
    {
        if (m_error.isEmpty())
            m_error = why;
        return false;
    }

    QString error() const { return m_error; }

private:
    qint64 m_limit;
    const Sink &m_sink;
    std::vector<char> m_ring;
    qint64 m_total = 0;
    QByteArray m_waiting;
    QString m_error;
};

class Decoder
{
public:
    Decoder(Bits &bits, Output &out) : m_bits(bits), m_out(out) {}

    bool run()
    {
        quint32 last = 0;
        do {
            quint32 type = 0;
            if (!m_bits.take(1, &last) || !m_bits.take(2, &type))
                return ended();
            bool ok = false;
            if (type == 0)
                ok = stored();
            else if (type == 1)
                ok = fixedCodes() && codes();
            else if (type == 2)
                ok = ownCodes() && codes();
            else
                ok = m_out.fail(QStringLiteral("a block of an unknown type"));
            if (!ok)
                return false;
        } while (last == 0);
        return m_out.flush();
    }

private:
    bool ended() { return m_out.fail(QStringLiteral("the data ends in the middle")); }
    bool broken() { return m_out.fail(QStringLiteral("the data holds no valid code here")); }

    // Section 3.2.4.
    bool stored()
    {
        m_bits.toWholeByte();
        quint32 length = 0;
        quint32 check = 0;
        if (!m_bits.take(16, &length) || !m_bits.take(16, &check))
            return ended();
        if ((length ^ 0xFFFFu) != check)
            return m_out.fail(QStringLiteral("a stored block with a wrong length"));
        for (quint32 i = 0; i < length; ++i) {
            quint32 byte = 0;
            if (!m_bits.take(8, &byte))
                return ended();
            if (!m_out.put(char(byte)))
                return false;
        }
        return true;
    }

    // Section 3.2.6.
    bool fixedCodes()
    {
        std::array<quint8, kLiteralSymbols> literal{};
        for (int s = 0; s < kLiteralSymbols; ++s)
            literal[size_t(s)] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
        std::array<quint8, kDistanceSymbols> distance{};
        distance.fill(5);
        return m_literal.build(literal.data(), kLiteralSymbols) &&
               m_distance.build(distance.data(), kDistanceSymbols);
    }

    // Section 3.2.7.
    bool ownCodes()
    {
        quint32 literals = 0;
        quint32 distances = 0;
        quint32 lengthCodes = 0;
        if (!m_bits.take(5, &literals) || !m_bits.take(5, &distances) ||
            !m_bits.take(4, &lengthCodes))
            return ended();
        literals += 257;
        distances += 1;
        lengthCodes += 4;
        if (literals > 286 || distances > 30)
            return m_out.fail(QStringLiteral("a block with too many codes"));

        std::array<quint8, 19> lengthOfLength{};
        for (quint32 i = 0; i < lengthCodes; ++i) {
            quint32 n = 0;
            if (!m_bits.take(3, &n))
                return ended();
            lengthOfLength[kLengthCodeOrder[i]] = quint8(n);
        }
        Code lengthCode;
        if (!lengthCode.build(lengthOfLength.data(), 19))
            return broken();

        std::array<quint8, 286 + 30> lengths{};
        const quint32 total = literals + distances;
        quint32 at = 0;
        while (at < total) {
            const int symbol = lengthCode.next(m_bits);
            if (symbol < 0)
                return symbol == kInputEnded ? ended() : broken();
            if (symbol < 16) {
                lengths[at++] = quint8(symbol);
                continue;
            }
            // 16 repeats the length before, 17 and 18 give runs of zeros.
            quint8 value = 0;
            quint32 times = 0;
            bool ok = false;
            if (symbol == 16) {
                if (at == 0)
                    return broken();
                value = lengths[at - 1];
                ok = m_bits.take(2, &times);
                times += 3;
            } else if (symbol == 17) {
                ok = m_bits.take(3, &times);
                times += 3;
            } else {
                ok = m_bits.take(7, &times);
                times += 11;
            }
            if (!ok)
                return ended();
            if (at + times > total)
                return broken();
            for (quint32 i = 0; i < times; ++i)
                lengths[at++] = value;
        }
        if (lengths[kEndOfBlock] == 0)
            return m_out.fail(QStringLiteral("a block with no end code"));
        if (!m_literal.build(lengths.data(), int(literals)) ||
            !m_distance.build(lengths.data() + literals, int(distances)))
            return broken();
        return true;
    }

    // Section 3.2.5: literals, and matches of a length and a distance, until
    // the end of the block.
    bool codes()
    {
        for (;;) {
            const int symbol = m_literal.next(m_bits);
            if (symbol < 0)
                return symbol == kInputEnded ? ended() : broken();
            if (symbol < kEndOfBlock) {
                if (!m_out.put(char(symbol)))
                    return false;
                continue;
            }
            if (symbol == kEndOfBlock)
                return true;
            const int lengthIndex = symbol - 257;
            if (lengthIndex >= int(kRanges.lengths.size()))
                return broken();
            const Range length = kRanges.lengths[size_t(lengthIndex)];
            quint32 lengthExtra = 0;
            if (!m_bits.take(length.extra, &lengthExtra))
                return ended();
            const int distanceIndex = m_distance.next(m_bits);
            if (distanceIndex == kInputEnded)
                return ended();
            if (distanceIndex < 0 || distanceIndex >= int(kRanges.distances.size()))
                return broken();
            const Range distance = kRanges.distances[size_t(distanceIndex)];
            quint32 distanceExtra = 0;
            if (!m_bits.take(distance.extra, &distanceExtra))
                return ended();
            if (!m_out.repeat(distance.base + int(distanceExtra), length.base + int(lengthExtra)))
                return false;
        }
    }

    Bits &m_bits;
    Output &m_out;
    Code m_literal;
    Code m_distance;
};

} // namespace

bool inflate(QIODevice &input, qint64 packedSize, qint64 limit, const Sink &sink, QString *error)
{
    Bits bits(input, packedSize);
    Output out(limit, sink);
    Decoder decoder(bits, out);
    const bool ok = decoder.run();
    if (!ok && error)
        *error = out.error();
    return ok;
}

} // namespace tacetos::archive
