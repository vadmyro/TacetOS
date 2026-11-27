# health

The health of the disk and the battery of an iPod (`docs/specs/health.md`): the parsing of the two files the firmware writes in its folder, the decoding of the identify and SMART blocks of the disk, the disk verdict, the battery estimate and verdict, and the history kept on the computer in `health/<key>.json` of the data folder given by the caller. `readHealthFiles` only reads the files from the music volume and `loadHistory`, `saveHistory` and `recordReading` only touch the history file; `makeReading`, `addReading` and the verdicts are pure, so the tests feed them bytes directly.
