#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro

"""Writes the health and charge files of the tests (docs/specs/health.md
section 10).

The identify and SMART blocks are built from the public layout of section 5.1,
with their checksums, and every value in them is invented: nothing comes from a
disk tool or a real iPod.

    make_health_fixtures.py OUTDIR        write the files
    make_health_fixtures.py --check DIR   exit 1 when DIR differs
"""

import sys
from pathlib import Path

BLOCK = 512


def checksum_last(block, at):
    """Sets the byte at `at` so that the 512 bytes add up to 0 modulo 256."""
    block[at] = 0
    block[at] = (-sum(block)) % 256


def identify_block(model, serial, revision):
    block = bytearray(BLOCK)

    def put(text, first_word, words):
        raw = text.encode("ascii").ljust(2 * words, b" ")
        for i in range(words):
            # The first character of a word is its high byte.
            hi, lo = raw[2 * i], raw[2 * i + 1]
            block[2 * (first_word + i)] = lo
            block[2 * (first_word + i) + 1] = hi

    put(serial, 10, 10)
    put(revision, 23, 4)
    put(model, 27, 20)
    # Word 255: signature 0xA5 in the low byte, checksum in the high byte.
    block[510] = 0xA5
    checksum_last(block, 511)
    return block


def smart_block(attributes):
    """`attributes` is a list of (id, current, worst, raw)."""
    block = bytearray(BLOCK)
    block[0:2] = (16).to_bytes(2, "little")
    for i, (ident, current, worst, raw) in enumerate(attributes):
        at = 2 + 12 * i
        block[at] = ident
        block[at + 1:at + 3] = (0x0032).to_bytes(2, "little")
        block[at + 3] = current
        block[at + 4] = worst
        block[at + 5:at + 11] = raw.to_bytes(6, "little")
    checksum_last(block, 511)
    return block


def clean_attributes(reallocated=0, pending=0):
    return [
        (1, 100, 100, 0),
        (5, 100, 100, reallocated),
        (9, 98, 98, 2140),
        (10, 100, 100, 0),
        (12, 99, 99, 1873),
        (191, 100, 100, 41),
        (193, 95, 95, 52310),
        # 31 °C in the low byte, the lowest and highest seen above it.
        (194, 100, 100, 0x0000_2D12_001F),
        (197, 100, 100, pending),
        (198, 100, 100, 0),
    ]


IDENTIFY = identify_block("TEST HDD 80GB", "TST0000001", "TF01")


def health_file(event="usb", disk_kind="hdd", smart="ok", attributes=None, level="74",
                identify=True):
    lines = [
        "format: 1",
        f"event: {event}",
        "clock: 2026-11-27 14:03:00",
        f"battery_level: {level}",
        "battery_mv: 3912",
        "charging: no",
        "runtime_s: 18720",
        "top_runtime_s: 114000",
        f"disk_kind: {disk_kind}",
    ]
    if identify:
        lines.append("identify_data: " + IDENTIFY.hex())
    lines.append(f"smart: {smart}")
    if smart == "ok":
        lines.append("smart_data: " + smart_block(attributes or clean_attributes()).hex())
    lines.append("end: ok")
    return "\n".join(lines) + "\n"


def charge_file(level):
    return "\n".join([
        "format: 1",
        "clock: 2026-11-26 09:12:40",
        f"battery_level: {level}",
        "battery_mv: 4105" if level == 100 else "battery_mv: 3990",
        "end: ok",
    ]) + "\n"


FILES = {
    "health-good.txt": health_file(),
    "health-watch.txt": health_file(attributes=clean_attributes(reallocated=3)),
    "health-failing.txt": health_file(attributes=clean_attributes(pending=12)),
    "health-unsupported.txt": health_file(smart="unsupported", event="shutdown"),
    "health-flash.txt": health_file(disk_kind="flash", attributes=clean_attributes(reallocated=3)),
    "charge-100.txt": charge_file(100),
    "charge-80.txt": charge_file(80),
}


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        folder = Path(argv[2])
        wrong = [name for name, text in FILES.items()
                 if not (folder / name).is_file()
                 or (folder / name).read_bytes() != text.encode("ascii")]
        for name in wrong:
            print(f"{name} differs from the script: run it again", file=sys.stderr)
        return 1 if wrong else 0
    if len(argv) == 2:
        folder = Path(argv[1])
        folder.mkdir(parents=True, exist_ok=True)
        for name, text in FILES.items():
            (folder / name).write_bytes(text.encode("ascii"))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
