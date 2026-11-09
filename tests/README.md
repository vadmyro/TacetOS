# tests

Qt Test suites, plus:

- `fakepod/`: disk images that look like an iPod, to test formatting, install and sync without a real device;
- `fixtures/`: a generated fake music library (never real music).

## Portable tests

The CI runs every suite on Linux, Windows and macOS. To keep them green everywhere:

- **No `QFile::link`**: on Windows it makes `.lnk` shortcuts, not real links. A test that needs real links starts with `TACETOS_NEEDS_SYMLINKS()` (`device/symlinks.h`), which skips it on Windows only.
- **No `QTEST_APPLESS_MAIN`**: clang warns about it with `-Wpedantic`, and the CI builds with `-Werror`. Use `QTEST_GUILESS_MAIN`.
- **No Linux-only paths or case**: no test that reads `/proc`, `/sys` or `/dev`, and none that counts on a case-sensitive file system, unless it sits under `#ifdef Q_OS_LINUX`. Build fake trees in a `QTemporaryDir`.
- **No file name Windows refuses**: no `:`, `*`, `?`, `"`, `<`, `>` or `|`, and no name that ends with a space or a dot. A test that needs one (a sysfs interface folder such as `1-2:1.0`) is skipped on Windows with `QSKIP`.
