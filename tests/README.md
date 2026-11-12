# tests

Qt Test suites, plus:

- `fakepod/`: disk images that look like an iPod, to test formatting, install and sync without a real device;
- `fixtures/`: a generated fake music library (never real music);
- `reference/`: tests that run a reference tool beside our own code (see below).

## Portable tests

The CI runs every suite on Linux, Windows and macOS. To keep them green everywhere:

- **No `QFile::link`**: on Windows it makes `.lnk` shortcuts, not real links. A test that needs real links starts with `TACETOS_NEEDS_SYMLINKS()` (`device/symlinks.h`), which skips it on Windows only.
- **No `QTEST_APPLESS_MAIN`**: clang warns about it with `-Wpedantic`, and the CI builds with `-Werror`. Use `QTEST_GUILESS_MAIN`.
- **No Linux-only paths or case**: no test that reads `/proc`, `/sys` or `/dev`, and none that counts on a case-sensitive file system, unless it sits under `#ifdef Q_OS_LINUX`. Build fake trees in a `QTemporaryDir`.
- **No file name Windows refuses**: no `:`, `*`, `?`, `"`, `<`, `>` or `|`, and no name that ends with a space or a dot. A test that needs one (a sysfs interface folder such as `1-2:1.0`) is skipped on Windows with `QSKIP`.

## Reference tools

`reference/build-reference-tools.sh` builds the partition tool that the comparison tests run. Nothing of it is stored here: it reads `firmware/base.lock`, takes the sources from `--sources DIR` or fetches the two tool directories at the pinned commit, and builds outside the tree (default `~/.cache/tacetos/reference-tools`). Then configure with `-DTACETOS_REFERENCE_TOOLS_DIR=<that directory>`. Without it every comparison test is skipped, never failed.

The tool is never changed. It reads a plain image file with sectors of 512 bytes, so the 2nd generation nano (2048) goes through a loop disk: it runs only on Linux with `TACETOS_REFERENCE_LOOP=1` and a sudo that asks for no password, which is the CI. Everywhere else that test is skipped, with the reason.

`reference/tst_installer_reference.cpp` runs install, update and uninstall on two copies of each fake iPod, one through our installer and one through the tool, and compares the two files whole. Only the zones listed in the test may differ, each with its section of the spec (the moved images of section 14.1, the place of the new image of the 2nd generation nano in section 15 step 2); any other byte fails. For the 2nd generation nano the uninstall is compared with the clean image from before the install, not with the tool, because the reference tool's command line cannot uninstall on the 2nd generation nano; only the bytes of our bootloader left after the last image (section 17 of the spec) may differ.

`reference/build-dfu-parts.sh` makes the generated parts of the Classic DFU image: the two fixed blocks, the two programs for the iPod and a reference tool that builds images from the same two programs. It takes the sources like the script above (`--sources DIR`, else a sparse fetch at the pinned commit), and the ARM compiler from `--toolchain DIR` (the prefix of an existing one) or, when that folder has none, builds it from the pinned sources there, once (CI keeps it in its cache). Everything goes outside the tree (default `~/.cache/tacetos/dfu-parts`) with a `manifest.txt` of names, sizes and sha256. Then configure with `-DTACETOS_DFU_PARTS_DIR=<that directory>`; the same folder gives the app build its Classic parts. `reference/tst_dfu_reference.cpp` builds the installer image for several synthetic bootloader files (the largest that fits included) and the uninstaller image through our code and through the tool, and compares them whole, with no difference allowed. The tool's single boot option is never used. Without the folder the test is skipped.
