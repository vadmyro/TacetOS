#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
"""Check the fake library against the recorded reference.

Usage: check_reference.py [--reference-dir DIR]
                          [--device-tree FILE --left-in-inbox FILE]

Without options it replays the generator twice in a temporary folder and
checks that:

  - each run writes the 30 files listed in its manifest, and nothing else;
  - both runs give the same names and the same manifest;
  - the recorded reference is consistent with the generated library: every
    file the reference left in the inbox exists in the library, and every
    other file is on the reference device, with the same extension.

With --device-tree and --left-in-inbox it compares the result of a sync run
(same format as the reference files, see record_reference.sh) with the
reference, line by line. That is how the library code is checked against the
reference.

Needs python3 and ffmpeg. Exits with 77 (skipped) when ffmpeg is missing.
"""

import argparse
import collections
import difflib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXPECTED_FILES = 30
SKIP = 77


def generate(out):
    """Run the generator into the empty folder OUT, return (files, manifest)."""
    run = subprocess.run(
        [sys.executable, str(HERE / "make_fake_library.py"), str(out)],
        capture_output=True, text=True)
    if run.returncode != 0:
        raise SystemExit(f"generator failed:\n{run.stdout}{run.stderr}")
    files = sorted(p.relative_to(out).as_posix()
                   for p in out.rglob("*") if p.is_file())
    manifest = (out / "manifest.tsv").read_text(encoding="utf-8")
    return files, manifest


def read_lines(path):
    return Path(path).read_text(encoding="utf-8").splitlines()


def compare(name, expected, actual):
    """Return a list of error lines when ACTUAL differs from EXPECTED."""
    if expected == actual:
        return []
    diff = difflib.unified_diff(expected, actual, "reference/" + name,
                                "actual/" + name, lineterm="", n=0)
    return [f"{name} differs from the reference:", *diff]


def check_generator(tmp):
    errors = []
    first_files, first_manifest = generate(tmp / "run1")
    second_files, second_manifest = generate(tmp / "run2")

    audio = [f for f in first_files if f != "manifest.tsv"]
    rows = first_manifest.splitlines()[1:]
    listed = sorted(row.split("\t")[0] for row in rows)
    if len(audio) != EXPECTED_FILES:
        errors.append(f"expected {EXPECTED_FILES} audio files, got {len(audio)}")
    if audio != listed:
        errors.append("the files on disk and the manifest do not list the same paths")
    errors += compare("generated names", first_files, second_files)
    errors += compare("manifest.tsv", first_manifest.splitlines(),
                      second_manifest.splitlines())
    return errors, audio


def check_consistency(audio, ref_dir):
    errors = []
    device = read_lines(ref_dir / "reference-device-tree.txt")
    left = read_lines(ref_dir / "reference-left-in-inbox.txt")

    if device != sorted(device) or len(set(device)) != len(device):
        errors.append("reference-device-tree.txt is not sorted or has duplicates")
    if left != sorted(left) or len(set(left)) != len(left):
        errors.append("reference-left-in-inbox.txt is not sorted or has duplicates")

    unknown = [p for p in left if p not in audio]
    if unknown:
        errors.append("left in inbox but not in the library: " + ", ".join(unknown))

    device_files = [p for p in device if not p.endswith("/")]
    placed = [p for p in audio if p not in left]
    if len(device_files) != len(placed):
        errors.append(f"{len(placed)} files should have been placed, "
                      f"the reference device has {len(device_files)}")
    extensions = lambda paths: collections.Counter(Path(p).suffix for p in paths)
    if extensions(device_files) != extensions(placed):
        errors.append("the formats on the reference device do not match the "
                      "formats of the files that were placed")
    return errors


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--reference-dir", type=Path, default=HERE)
    parser.add_argument("--device-tree", type=Path)
    parser.add_argument("--left-in-inbox", type=Path)
    args = parser.parse_args(argv[1:])
    if bool(args.device_tree) != bool(args.left_in_inbox):
        parser.error("--device-tree and --left-in-inbox go together")

    ref_dir = args.reference_dir
    errors = []
    if args.device_tree:
        errors += compare("reference-device-tree.txt",
                          read_lines(ref_dir / "reference-device-tree.txt"),
                          read_lines(args.device_tree))
        errors += compare("reference-left-in-inbox.txt",
                          read_lines(ref_dir / "reference-left-in-inbox.txt"),
                          read_lines(args.left_in_inbox))
    else:
        if shutil.which("ffmpeg") is None:
            print("check_reference.py: ffmpeg not found, skipped")
            return SKIP
        with tempfile.TemporaryDirectory(prefix="tacetos-fixtures-") as tmp:
            errors, audio = check_generator(Path(tmp))
            if not errors:
                errors += check_consistency(audio, ref_dir)

    for line in errors:
        print(line, file=sys.stderr)
    if errors:
        return 1
    print("fixtures match the reference")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except FileNotFoundError as e:
        if e.filename == "ffmpeg":
            print("check_reference.py: ffmpeg not found, skipped")
            sys.exit(SKIP)
        raise
