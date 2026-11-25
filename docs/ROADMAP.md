# Roadmap

Target: **v1.0 at the end of January 2027.** Dates may move; this page is updated as tasks are done. Each task is about two hours of work.

## Overview

| Version | What it brings | Done | Status |
|---|---|---|---|
| Setup | Repository, app skeleton, CI on Linux, Windows and macOS | 4/4 | ✅ done |
| Foundations | Firmware base, model database, library rules, specs of the install formats and protocols | 7/7 | ✅ done |
| **v0.1** | Device detection and live state (plug, unplug, disk mode, DFU) | 7/7 | ✅ done |
| Test devices | Disk image test devices + privileged helper | 6/6 | ✅ done |
| **v0.2** | Install and reset wizard: backup, format, firmware, bootloader, guided DFU, uninstall | 12/12 | ✅ done |
| **v0.3** | Our own firmware build: name, custom boot logo, update channel, health page | 8/12 | ⏳ |
| **v0.4** | Music library and sync: tags, cover art, sorting rules, playlists, conversion, import folder | 0/12 | ⏳ |
| **v0.5** | Settings editor, fonts, menu order, game library, listening recap | 0/8 | ⏳ |
| **v0.6** | Themes: browser, editor with live preview, simple mode, default TacetOS theme | 0/11 | ⏳ |
| **v0.7** | Translations: language picked at first launch and in Settings, French, then many more | 0/3 | ⏳ |
| **v0.8** | Windows, macOS and Linux: detection, DFU and disk access, tested on every major system | 0/6 | ⏳ |
| **v0.9** | Public beta: packages (AppImage, .deb, .rpm, Windows installer, .dmg), one-command install, install guide | 0/6 | ⏳ |
| **v1.0** | Fixes from the beta | 0/1 | ⏳ |

## Setup ✅

- [x] Repository, license and public docs
- [x] Qt 6 app skeleton with CMake presets and a first test
- [x] CI: formatting check, build and tests on Linux, Windows and macOS
- [x] Logo and icon

## Foundations ✅

- [x] Firmware base: pinned source version, patch set, apply and build scripts
- [x] Library sorting rules written down as a spec ([library-rules.md](specs/library-rules.md))
- [x] Model database (`models/models.toml`) and one page per model in `docs/models/`
- [x] Fake music library (no real music) and a reference sync result for tests
- [x] Spec: bootloader install on older iPods ([bootloader-legacy.md](specs/bootloader-legacy.md))
- [x] Spec: bootloader install on the iPod Classic ([bootloader-classic.md](specs/bootloader-classic.md))
- [x] Specs: device detection and theme file format ([detection.md](specs/detection.md), [theme-format.md](specs/theme-format.md))

## v0.1 · Device detection ✅

- [x] Device module: from the USB id to the iPod family
- [x] Live plug and unplug
- [x] Linux disks: mount, unmount and eject without root
- [x] Exact model, and which firmware runs (Apple's or another one, and its version)
- [x] State machine: unplugged, normal, mounted, DFU, rebooting, busy, done, error
- [x] Home screen: drawing of the model, live state, free space, firmware version
- [x] Real-device test: normal mode, disk mode, DFU, unplug

## Test devices and helper ✅

- [x] Disk images that behave like an iPod, for every family
- [x] Installer for older iPods, checked byte for byte against a reference tool on the disk images
- [x] Installer for the iPod Classic over DFU, always dual boot, checked byte for byte
- [x] Privileged helper with a fixed set of commands (format, convert, install, uninstall, backup)
- [x] Safety tests: refuse a USB stick, power cut in the middle of a write, full disk
- [x] Real-device read-only check on an iPod Classic

## v0.2 · Install and reset wizard ✅

- [x] Wizard flow that resumes after an interruption
- [x] Diagnostic screen with blocking checks before any write, and a clear message when something is missing on the computer
- [x] Automatic backup of settings, themes, playlists and the firmware partition
- [x] Format or convert (Mac to FAT32) with a strong confirmation
- [x] Firmware install that keeps your settings
- [x] Bootloader install on older iPods
- [x] Built-in DFU guide: buttons to hold, countdown, carries on by itself
- [x] Bootloader install on the iPod Classic, dual boot
- [x] Uninstall: back to 100 % Apple software
- [x] Real-device test: uninstall, reinstall, check
- [x] Fixes from the real-device test: one password, automatic mount, clearer messages
- [x] Log and a "copy report" button for Discord

## v0.3 · TacetOS firmware

- [x] TacetOS name in the firmware, applied at build time
- [x] Lighter firmware: no test or developer plugins, games, emulators and visual demos kept
- [x] TacetOS boot logo, replaceable from the app by your own image or a black screen, quiet bootloader
- [x] Install progress on the iPod screen and in the app: logo and a progress bar
- [x] CI builds for every model, plus simulators
- [ ] Update channel through GitHub Releases (public from the beta), settings and themes kept
- [x] Weekly CI job to follow the base firmware
- [x] Language screen on the iPod at first start, English first, ready for more languages
- [ ] Real-device test: boot, logo, update from the app
- [ ] Selected fixes from newer firmware development, one tested change at a time
- [x] Technical folder name on the iPod Classic
- [ ] Health page: disk and battery state, with a warning when they wear out

## v0.4 · Music library and sync

- [ ] Library scan, tags and a local cache
- [ ] Sorting rules from the spec, with reference tests
- [ ] Rules editable in the app: genres, languages, artists
- [ ] Library view: cover art, list, tag editing, "to sort" queue
- [ ] Drag-and-drop playlists, compatible `.m3u8`, smart playlists, playlist folders
- [ ] One-click sync: differences, progress, mirror mode with a preview of what will be deleted
- [ ] Automatic conversion of formats the iPod cannot play, optional space saving
- [ ] Music database built on the computer
- [ ] Playlist link to a "to find" list (no downloads, only links to legal sources)
- [ ] Import folder and drag and drop: TacetOS tags, sorts and readies your own files, opens `.zip` albums, spots duplicates
- [ ] iPod card: a faithful drawing of the model and its colour, a storage bar by type, a clear summary
- [ ] Real-device test with a full library

## v0.5 · Settings, fonts, menus

- [ ] Settings schema for every model
- [ ] Settings file read and written back without losing anything
- [ ] Settings editor: categories, search, explanations, presets
- [ ] Font preview and install
- [ ] Main menu order, quick screen, shortcuts, status bar
- [ ] Game library: pick the games and emulators to put on the iPod
- [ ] Listening recap: your year in music, computed on your computer, no account needed
- [ ] Real-device test

## v0.6 · Themes

- [ ] Theme browser filtered for your screen size, sorted by model, color, style and popularity
- [ ] Our own theme parser and renderer
- [ ] Warnings for tags your firmware version does not support
- [ ] Simulated playback (title, artist, cover, progress, battery, clock)
- [ ] Screens: now playing, status bar, menu backgrounds, icons
- [ ] Code editor with highlighting, errors and live preview
- [ ] Simple mode: colors, background, font, cover position
- [ ] Theme export and import as `.zip`, install on the iPod
- [ ] Preview checked against the simulator
- [ ] Real-device test with three themes
- [ ] Default TacetOS theme with the boot logo

## v0.7 · Translations

- [ ] Every string translatable, language picked at first launch, changed anytime in Settings
- [ ] French
- [ ] As many languages as possible, in the app and on the iPod (each written in its own words), then community translations

## v0.8 · Windows, macOS and Linux

- [ ] Windows: detection, eject, helper
- [ ] Windows: DFU
- [ ] macOS: detection, DFU, helper, Mac-formatted iPods
- [ ] Linux: the same checks on every major distribution (udisks2, udev rule, helper)
- [ ] Tests with a real iPod on Windows, macOS and Linux
- [ ] CI on every supported system: Windows 10 and 11, several macOS versions on Intel and Apple Silicon, Ubuntu, Debian, Fedora and Arch, ARM64 included

## v0.9 · Public beta

- [ ] App style guide and stylesheet (light and dark)
- [ ] Packages: AppImage, .deb, .rpm, Windows installer, .dmg, a one-command install for each system, and a built-in update check
- [ ] Signed packages
- [ ] User docs: install guide in the README, one page per model, FAQ, website
- [ ] Screenshots and a short demo video
- [ ] Public beta: untested models marked experimental

## v1.0

- [ ] Fixes from the beta, release notes, v1.0

## After 1.0

- More firmware features, one tested patch at a time
- Podcasts
- Scrobbling of the listening log
- Guides for flash storage mods and battery replacement
