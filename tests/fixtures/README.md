# fixtures

Generated test data only: tiny audio files with fake tags. No real music, ever.

**make_fake_library.py**

Writes a fake library of 30 files (half a second of silence or a quiet beep) to an empty folder, plus a `manifest.tsv` with the tags of each file.

```
python3 tests/fixtures/make_fake_library.py /path/to/empty/folder
```

It needs `ffmpeg` with the mp3, flac, aac and vorbis encoders, and nothing else. Artists and albums are invented.

**What it covers**

- mp3, flac, m4a and ogg;
- guests written in five ways ("(feat. X)", "[ft. X]", "feat." and "featuring" in the artist, "&");
- accents and other non-ASCII letters (French, Spanish, Nordic, Turkish);
- two discs, a compilation, a name that differs only by case, a slash in an artist, a very long title;
- no artist tag, and no tags at all.

The names and tags are fixed, so every run gives the same tree. The audio bytes depend on the encoder version: compare names and tags, never hashes.

**record_reference.sh**

Runs the reference script on the fake library and records the result.

```
tests/fixtures/record_reference.sh /path/to/reference-script tests/fixtures
```

It works in a private `HOME` under `$TMPDIR`, so the real music folder is never read. The generated files are dropped in the inbox, `artists.tsv` becomes the artist table, and a plain folder stands in for the device (a fake `findmnt` points to it). It writes two files, compared by name only:

- `reference-device-tree.txt`: every path on the fake device, sorted, folders end with `/`;
- `reference-left-in-inbox.txt`: the files the reference could not place. The ogg files are all here, because the reference never reads their tags, and so are the files with no artist tag.

`artists.tsv` lists the invented artists with a genre and a language. Run the script again only when the generator, the artist table or the library rules change, then review the diff.

**check_reference.py**

The CTest `fixtures_reference`. It replays the generator twice in a temporary folder and checks that both runs write the same 30 files and the same manifest, then that the recorded reference fits the generated library: every file left in the inbox exists in the library, and every other file is on the reference device with the same format. It does not run the reference script, so it needs only python3 and ffmpeg. It is reported as skipped when ffmpeg is missing.

```
python3 tests/fixtures/check_reference.py
```

To compare the result of a sync run with the reference, give it the two files the run produced (same format as the reference files, one path per line):

```
python3 tests/fixtures/check_reference.py --device-tree device-tree.txt --left-in-inbox left-in-inbox.txt
```

It prints the differing lines and exits with 1 when they do not match.
