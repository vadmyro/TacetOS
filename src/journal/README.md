# journal

What the app saw and did, kept so that a problem can be explained afterwards. Qt Core only, no user interface, and the device module does not depend on it. The rules are in `docs/specs/device-state.md` section 3.

- `Journal`: adds an entry (`add(level, domain, port, text)`), keeps the last 1000 in a ring (`entries()`), announces each one once (`entryAdded`), and writes it to a file when one is attached (`attachFile(folder)`).
- `JournalFile`: one line per entry, flushed at once, rotated at 1 MiB over 3 files. The caller gives the data folder, the code never looks for it.
- `cleanText()`, `cleanPort()` and `formatEntry()`: pure functions. The text is made one line, the home folder becomes `~`, and the account name as a path component becomes `<user>`.

The journal never stops the app: when the folder or the file fails, one warning goes to the ring and everything goes on in memory.
