# Roadmap

Target: **v1.0 at the end of January 2027.** Dates may move; this page is updated as tasks are done. Each task is about two hours of work.

## Overview

| Version | What it brings | Done | Status |
|---|---|---|---|
| Setup | Repository, app skeleton, CI on Linux, Windows and macOS | 4/4 | ✅ done |
| Foundations | Firmware base, model database, library rules, specs of the install formats and protocols | 7/7 | ✅ done |
| **v0.1** | Device detection and live state (plug, unplug, disk mode, DFU) | 1/7 | 🚧 in progress |
| Test devices | Disk image test devices + privileged helper | 0/5 | ⏳ |
| **v0.2** | Install and reset wizard: backup, format, firmware, bootloader, guided DFU, uninstall | 0/12 | ⏳ |
| **v0.3** | Our own firmware build: name, custom boot logo, update channel | 0/9 | ⏳ |
| **v0.4** | Music library and sync: tags, cover art, sorting rules, playlists, conversion | 0/10 | ⏳ |
| **v0.5** | Settings editor, fonts, menu order | 0/6 | ⏳ |
| **v0.6** | Themes: browser, editor with live preview, simple mode, default TacetOS theme | 0/11 | ⏳ |
| Translations | French first, then many more | 0/3 | ⏳ |
| Windows and macOS | Detection, DFU and disk access on every system | 0/4 | ⏳ |
| **beta** | Packages (AppImage, .deb, .rpm, Windows installer, .dmg), public beta | 0/6 | ⏳ |
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

## v0.1 · Device detection 🚧

- [x] Device module: from the USB id to the iPod family
- [ ] Live plug and unplug
- [ ] Linux disks: mount, unmount and eject without root
- [ ] Exact model, and which firmware is installed (Apple or TacetOS, and its version)
- [ ] State machine: unplugged, normal, disk mode, mounted, DFU, rebooting, busy, done, error
- [ ] Home screen: drawing of the model, live state, free space, firmware version
- [ ] Real-device test: normal mode, disk mode, DFU, unplug

## Test devices and helper

- [ ] Disk images that behave like an iPod, for every family
- [ ] Installer for older iPods, checked byte for byte against a reference tool on the disk images
- [ ] Installer for the iPod Classic over DFU, always dual boot, checked byte for byte
- [ ] Privileged helper with a fixed set of commands (format, convert, install, uninstall, backup)
- [ ] Safety tests: refuse a USB stick, power cut in the middle of a write, full disk

## v0.2 · Install and reset wizard

- [ ] Early checks of DFU on Windows and disk writes on macOS
- [ ] Wizard flow that resumes after an interruption
- [ ] Diagnostic screen with blocking checks before any write
- [ ] Automatic backup of settings, themes, playlists and the firmware partition
- [ ] Format or convert (Mac to FAT32) with a strong confirmation
- [ ] Firmware install that keeps your settings
- [ ] Bootloader install on older iPods
- [ ] Built-in DFU guide: buttons to hold, countdown, carries on by itself
- [ ] Bootloader install on the iPod Classic, dual boot
- [ ] Uninstall: back to 100 % Apple software
- [ ] Real-device test: uninstall, reinstall, check
- [ ] Log and a "copy report" button for Discord

## v0.3 · TacetOS firmware

- [ ] TacetOS name in the firmware, applied at build time
- [ ] Custom boot logo read from the iPod (black by default), quiet bootloader
- [ ] CI builds for every model, plus simulators
- [ ] Update channel through GitHub Releases, settings and themes kept
- [ ] Weekly CI job to follow the base firmware
- [ ] Real-device test: boot, logo, update from the app
- [ ] Voice files for the chosen language
- [ ] Selected fixes from newer firmware development, one tested change at a time
- [ ] Technical folder name on the iPod Classic

## v0.4 · Music library and sync

- [ ] Library scan, tags and a local cache
- [ ] Sorting rules from the spec, with reference tests
- [ ] Rules editable in the app: genres, languages, artists
- [ ] Library view: cover art, list, tag editing, "to sort" queue
- [ ] Drag-and-drop playlists, compatible `.m3u8`
- [ ] One-click sync: differences, progress, mirror mode with a preview of what will be deleted
- [ ] Automatic conversion of formats the iPod cannot play, optional space saving
- [ ] Music database built on the computer
- [ ] Playlist link to a "to find" list (no downloads, only links to legal sources)
- [ ] Real-device test with a full library

## v0.5 · Settings, fonts, menus

- [ ] Settings schema for every model
- [ ] Settings file read and written back without losing anything
- [ ] Settings editor: categories, search, explanations, presets
- [ ] Font preview and install
- [ ] Main menu order, quick screen, shortcuts, status bar
- [ ] Real-device test

## v0.6 · Themes

- [ ] Theme browser filtered for your screen size
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

## Translations

- [ ] Every string translatable, language chosen in the app
- [ ] French
- [ ] As many languages as possible, then community translations

## Windows and macOS

- [ ] Windows: detection, eject, helper
- [ ] Windows: DFU
- [ ] macOS: detection, DFU, helper, Mac-formatted iPods
- [ ] Tests on Windows and macOS with a real iPod

## Beta

- [ ] App style guide and stylesheet (light and dark)
- [ ] Packages: AppImage, .deb, .rpm, Windows installer, .dmg, with a built-in update check
- [ ] Signed packages
- [ ] User docs, one page per model, FAQ, website
- [ ] Screenshots and a short demo video
- [ ] Public beta: untested models marked experimental

## v1.0

- [ ] Fixes from the beta, release notes, v1.0

## After 1.0

- More firmware features, one tested patch at a time
- Podcasts, disk and battery health
- Voice files generated by text-to-speech
- Scrobbling of the listening log
- Guides for flash storage mods and battery replacement
