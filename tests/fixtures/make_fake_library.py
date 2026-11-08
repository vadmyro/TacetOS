#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
"""Generate the fake music library used by the sync and library tests.

Usage: make_fake_library.py OUTPUT_DIR

Writes 30 tiny audio files (half a second of silence or a quiet beep, never
real music) with invented artists and fake tags, then a `manifest.tsv` that
lists every file with the tags it was given. The names, folders and tags are
fixed, so two runs give the same tree. The audio bytes depend on the encoder
version, so tests must compare names and tags, not file hashes.

It covers what the sync code has to survive: "feat." in the title or in the
artist, accents and other non-ASCII letters, mp3 / flac / m4a / ogg, several
discs, a compilation, and files with no artist (or no tags at all).

Needs ffmpeg with libmp3lame, flac, aac and libvorbis. Nothing else.
"""

import shutil
import subprocess
import sys
from pathlib import Path

DURATION = "0.5"
SAMPLE_RATE = "22050"

# Tag names understood by ffmpeg's -metadata, in the order of the manifest.
TAG_ORDER = ("title", "artist", "album_artist", "album", "track", "disc",
             "date", "genre", "compilation")

# Per format: file extension and the encoder options.
FORMATS = {
    "mp3": (".mp3", ["-c:a", "libmp3lame", "-b:a", "48k",
                     "-id3v2_version", "3", "-write_id3v1", "0"]),
    "flac": (".flac", ["-c:a", "flac"]),
    "m4a": (".m4a", ["-c:a", "aac", "-b:a", "48k"]),
    "ogg": (".ogg", ["-c:a", "libvorbis", "-q:a", "0"]),
}


def t(path, fmt, sound, **tags):
    return {"path": path, "fmt": fmt, "sound": sound, "tags": tags}


# (relative path without extension, format, "silence" or "beep", tags)
LIBRARY = [
    # Plain album, mp3, accents in artist, album and titles.
    t("Clara Vendôme/Éclats d'hiver/01 Neige", "mp3", "beep",
      title="Neige", artist="Clara Vendôme", album_artist="Clara Vendôme",
      album="Éclats d'hiver", track="1/4", date="2019", genre="Folk"),
    t("Clara Vendôme/Éclats d'hiver/02 Été indien", "mp3", "silence",
      title="Été indien", artist="Clara Vendôme", album_artist="Clara Vendôme",
      album="Éclats d'hiver", track="2/4", date="2019", genre="Folk"),
    t("Clara Vendôme/Éclats d'hiver/03 Où va le vent", "mp3", "beep",
      title="Où va le vent", artist="Clara Vendôme",
      album_artist="Clara Vendôme", album="Éclats d'hiver", track="3/4",
      date="2019", genre="Folk"),
    t("Clara Vendôme/Éclats d'hiver/04 Cœur de glace", "mp3", "silence",
      title="Cœur de glace", artist="Clara Vendôme",
      album_artist="Clara Vendôme", album="Éclats d'hiver", track="4/4",
      date="2019", genre="Folk"),

    # flac, tilde and cedilla.
    t("Los Hermanos Ñandú/Bailongo/01 Mañana", "flac", "beep",
      title="Mañana", artist="Los Hermanos Ñandú",
      album_artist="Los Hermanos Ñandú", album="Bailongo", track="1",
      date="2021", genre="Latin"),
    t("Los Hermanos Ñandú/Bailongo/02 Corazón", "flac", "silence",
      title="Corazón", artist="Los Hermanos Ñandú",
      album_artist="Los Hermanos Ñandú", album="Bailongo", track="2",
      date="2021", genre="Latin"),
    t("Los Hermanos Ñandú/Bailongo/03 Façade", "flac", "beep",
      title="Façade", artist="Los Hermanos Ñandú",
      album_artist="Los Hermanos Ñandú", album="Bailongo", track="3",
      date="2021", genre="Latin"),

    # ogg, Nordic letters.
    t("Björn Åkesson/Skymning/01 Älvdans", "ogg", "silence",
      title="Älvdans", artist="Björn Åkesson", album_artist="Björn Åkesson",
      album="Skymning", track="1", date="2017", genre="Ambient"),
    t("Björn Åkesson/Skymning/02 Höstljus", "ogg", "beep",
      title="Höstljus", artist="Björn Åkesson", album_artist="Björn Åkesson",
      album="Skymning", track="2", date="2017", genre="Ambient"),
    t("Björn Åkesson/Skymning/03 Ørsted", "ogg", "silence",
      title="Ørsted", artist="Björn Åkesson", album_artist="Björn Åkesson",
      album="Skymning", track="3", date="2017", genre="Ambient"),

    # m4a, Turkish letters.
    t("Mina Çelik/Işık/01 Gözyaşı", "m4a", "beep",
      title="Gözyaşı", artist="Mina Çelik", album_artist="Mina Çelik",
      album="Işık", track="1", date="2022", genre="Pop"),
    t("Mina Çelik/Işık/02 Şafak", "m4a", "silence",
      title="Şafak", artist="Mina Çelik", album_artist="Mina Çelik",
      album="Işık", track="2", date="2022", genre="Pop"),
    t("Mina Çelik/Işık/03 İstanbul", "m4a", "beep",
      title="İstanbul", artist="Mina Çelik", album_artist="Mina Çelik",
      album="Işık", track="3", date="2022", genre="Pop"),

    # Guests, written five different ways. The album artist stays "Nova Kell".
    t("Nova Kell/Orbits/01 Launch (feat. Ida Rowe)", "mp3", "beep",
      title="Launch (feat. Ida Rowe)", artist="Nova Kell",
      album_artist="Nova Kell", album="Orbits", track="1", date="2023",
      genre="Electronic"),
    t("Nova Kell/Orbits/02 Drift", "flac", "silence",
      title="Drift", artist="Nova Kell feat. Ida Rowe",
      album_artist="Nova Kell", album="Orbits", track="2", date="2023",
      genre="Electronic"),
    t("Nova Kell/Orbits/03 Perigee [ft. Tomás Ibarra]", "m4a", "beep",
      title="Perigee [ft. Tomás Ibarra]", artist="Nova Kell",
      album_artist="Nova Kell", album="Orbits", track="3", date="2023",
      genre="Electronic"),
    t("Nova Kell/Orbits/04 Apogee", "ogg", "silence",
      title="Apogee", artist="Nova Kell featuring Ida Rowe & Tomás Ibarra",
      album_artist="Nova Kell", album="Orbits", track="4", date="2023",
      genre="Electronic"),
    t("Nova Kell/Orbits/05 Re-entry", "mp3", "beep",
      title="Re-entry", artist="Nova Kell & Ida Rowe",
      album_artist="Nova Kell", album="Orbits", track="5", date="2023",
      genre="Electronic"),

    # Two discs, plus a name that differs only by case from "Nova Kell".
    t("nova kell/Orbits Remixed/1-01 Launch (Ida Rowe Remix)", "flac", "beep",
      title="Launch (Ida Rowe Remix)", artist="nova kell",
      album_artist="nova kell", album="Orbits Remixed", track="1", disc="1/2",
      date="2024", genre="Electronic"),
    t("nova kell/Orbits Remixed/2-01 Drift (Night Version)", "flac", "silence",
      title="Drift (Night Version)", artist="nova kell",
      album_artist="nova kell", album="Orbits Remixed", track="1", disc="2/2",
      date="2024", genre="Electronic"),

    # Compilation: one album, four artists, four formats.
    t("Various Artists/Test Card Vol. 1/01 Opening", "mp3", "beep",
      title="Opening", artist="The Quiet Rooms",
      album_artist="Various Artists", album="Test Card Vol. 1", track="1",
      date="2020", genre="Indie", compilation="1"),
    t("Various Artists/Test Card Vol. 1/02 Interlude", "flac", "silence",
      title="Interlude", artist="Zoë & Les Pianos",
      album_artist="Various Artists", album="Test Card Vol. 1", track="2",
      date="2020", genre="Indie", compilation="1"),
    t("Various Artists/Test Card Vol. 1/03 Gold Hour", "m4a", "beep",
      title="Gold Hour", artist="Dario Fontaine feat. Mirela",
      album_artist="Various Artists", album="Test Card Vol. 1", track="3",
      date="2020", genre="Indie", compilation="1"),
    t("Various Artists/Test Card Vol. 1/04 Closing", "ogg", "silence",
      title="Closing", artist="Ünal Demir", album_artist="Various Artists",
      album="Test Card Vol. 1", track="4", date="2020", genre="Indie",
      compilation="1"),

    # Awkward characters: a slash in the artist, a very long title.
    t("Pilot-Copilot/Black Box/01 Mayday", "mp3", "beep",
      title="Mayday", artist="Pilot/Copilot", album_artist="Pilot/Copilot",
      album="Black Box", track="1", date="2018", genre="Rock"),
    t("Pilot-Copilot/Black Box/02 " + "A very long title " * 5 + "end", "ogg",
      "silence",
      title="A very long title " * 5 + "end", artist="Pilot/Copilot",
      album_artist="Pilot/Copilot", album="Black Box", track="2",
      date="2018", genre="Rock"),

    # Unknown artist: no artist tag at all, in each format.
    t("_unknown/Field Recording 1", "mp3", "silence",
      title="Field Recording 1", album="Loose Files", track="1"),
    t("_unknown/Field Recording 2", "flac", "beep",
      title="Field Recording 2", album="Loose Files", track="2"),
    t("_unknown/Field Recording 3", "m4a", "silence",
      title="Field Recording 3", album="Loose Files", track="3"),

    # No tags whatsoever: the file name is all there is.
    t("_unknown/untagged", "ogg", "beep"),
]

assert len(LIBRARY) == 30, len(LIBRARY)
assert len({e["path"] + FORMATS[e["fmt"]][0] for e in LIBRARY}) == 30


def encode(entry, target):
    ext, codec_args = FORMATS[entry["fmt"]]
    if entry["sound"] == "beep":
        # A different pitch per file, so a wrong match is easy to hear.
        pitch = 330 + 20 * (hash_index(entry["path"]) % 20)
        source = f"sine=frequency={pitch}:sample_rate={SAMPLE_RATE}"
        gain = ["-af", "volume=0.2"]
    else:
        source = f"anullsrc=r={SAMPLE_RATE}:cl=mono"
        gain = []
    cmd = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
           "-f", "lavfi", "-i", source, "-t", DURATION, *gain, "-ac", "1",
           "-map_metadata", "-1", "-fflags", "+bitexact",
           "-flags:a", "+bitexact", *codec_args]
    for key in TAG_ORDER:
        if key in entry["tags"]:
            cmd += ["-metadata", f"{key}={entry['tags'][key]}"]
    cmd.append(str(target))
    subprocess.run(cmd, check=True)


def hash_index(text):
    # Not hash(): that changes between runs.
    return sum(ord(c) * (i + 1) for i, c in enumerate(text))


def main(argv):
    if len(argv) != 2 or argv[1] in ("-h", "--help"):
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        print("Usage: make_fake_library.py OUTPUT_DIR", file=sys.stderr)
        return 2
    if shutil.which("ffmpeg") is None:
        print("make_fake_library.py: ffmpeg not found", file=sys.stderr)
        return 1
    out = Path(argv[1])
    if out.exists() and any(out.iterdir()):
        print(f"make_fake_library.py: {out} is not empty", file=sys.stderr)
        return 1
    out.mkdir(parents=True, exist_ok=True)

    rows = []
    for entry in LIBRARY:
        rel = Path(entry["path"] + FORMATS[entry["fmt"]][0])
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        encode(entry, target)
        rows.append([rel.as_posix(), entry["fmt"], entry["sound"]]
                    + [entry["tags"].get(k, "") for k in TAG_ORDER])

    rows.sort()
    header = ["path", "format", "sound", *TAG_ORDER]
    with open(out / "manifest.tsv", "w", encoding="utf-8", newline="\n") as f:
        for row in [header, *rows]:
            f.write("\t".join(row) + "\n")
    print(f"{len(rows)} files written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
