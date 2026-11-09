<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo/banner-dark.png">
    <img src="docs/assets/logo/banner-light.png" alt="TacetOS" width="640">
  </picture>
</h1>

<p align="center">
  <a href="https://github.com/vadmyro/TacetOS/actions/workflows/ci.yml"><img src="https://github.com/vadmyro/TacetOS/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
  <a href="https://discord.gg/XfXtZeUFYs"><img src="https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white" alt="Discord"></a>
  <a href="docs/ROADMAP.md"><img src="https://img.shields.io/badge/status-in%20development-orange" alt="Status: in development"></a>
  <a href="https://github.com/vadmyro/TacetOS/commit/main"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2Fvadmyro%2FTacetOS%2Fcommits%2Fmain&query=%24.commit.committer.date&label=last%20commit" alt="Last commit"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey" alt="Platforms: Linux, Windows, macOS">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/CMake-064F8C?logo=cmake&logoColor=white" alt="CMake">
</p>

> **SOUND is at the end of silence.**

**TacetOS is a desktop app (Linux, Windows, macOS) and a custom firmware for the classic iPod.** Plug in your iPod, and the app sets everything up: it prepares the disk, installs the bootloader and the firmware, then lets you manage your music, settings and themes, all from one place.

> [!WARNING]
> **In development. Nothing works yet.** Do not use it on a real iPod. Until the public beta, development builds refuse to write to a real device. Follow the progress in the [roadmap](docs/ROADMAP.md).

## Why "TacetOS"?

*Tacet* is the word written on a musical score when an instrument stays silent. The music only starts when the silence ends.

## What it will do

- **Install, step by step.** The app detects your iPod and shows its state live: normal mode, disk mode, DFU, rebooting, copying... When a step has to be done by hand (like entering DFU mode on an iPod Classic), it shows a clear guide with the buttons to hold and a countdown, then carries on by itself.
- **Safe by design.** Blocking checks before any write, an automatic backup of the firmware partition, and a one-click way back to the original Apple software.
- **Your music library.** Cover art, tags, automatic sorting, drag-and-drop playlists, one-click sync, automatic conversion of formats the iPod cannot play.
- **Settings editor.** Every firmware setting in a clear form, with explanations, instead of editing a text file.
- **Theme editor.** Browse and install themes for your screen size, or make your own with a live preview.
- **Its own firmware.** With its own name, boot logo and default theme, updated from the app.
- **Translated.** English first, French next, then as many languages as possible.

## Progress

| Version | Focus | Done |
|---|---|---|
| Setup | Repository, app skeleton, CI | ✅ 4/4 |
| Foundations | Firmware base, model database, specs | ✅ 7/7 |
| **v0.1** | Device detection and live state | 🚧 2/7 |
| Test devices | Disk image test devices, privileged helper | ⏳ 0/5 |
| **v0.2** | Install and reset wizard | ⏳ 0/12 |
| **v0.3** | TacetOS firmware | ⏳ 0/9 |
| **v0.4** | Music library and sync | ⏳ 0/10 |
| **v0.5** | Settings, fonts, menus | ⏳ 0/6 |
| **v0.6** | Themes | ⏳ 0/11 |
| Translations | French, then many more | ⏳ 0/3 |
| Windows and macOS | Detection, DFU, disk access | ⏳ 0/4 |
| **beta** | Packages, public beta | ⏳ 0/6 |
| **v1.0** | Fixes from the beta | ⏳ 0/1 |

### Now: device detection

- [x] Device module: from the USB id to the iPod family
- [x] Live plug and unplug
- [ ] Linux disks: mount, unmount and eject without root
- [ ] Exact model, and which firmware is installed (Apple or TacetOS, and its version)
- [ ] State machine: unplugged, normal, disk mode, mounted, DFU, rebooting, busy, done, error
- [ ] Home screen: drawing of the model, live state, free space, firmware version
- [ ] Real-device test: normal mode, disk mode, DFU, unplug

Every task is listed in the [roadmap](docs/ROADMAP.md). News is posted on the [Discord server](https://discord.gg/XfXtZeUFYs).

## Supported models

The goal is the 6 classic iPod families, 14 models in all:

- **iPod (1G to 4G):**
  - 1G (FireWire only)
  - 2G (FireWire only)
  - 3G
  - 4G

- **iPod Color / Photo:**
  - Color / Photo

- **iPod Mini:**
  - Mini 1G
  - Mini 2G

- **iPod Nano:**
  - Nano 1G
  - Nano 2G

- **iPod Video:**
  - Video 5G
  - Video 5.5G

- **iPod Classic:**
  - Classic 6G
  - Classic 6.5G
  - Classic 7G

Only the iPod Classic is tested on real hardware during development. Other models will be marked *experimental* until beta testers confirm they work.

## Building

The app is written in C++17 with Qt 6 (6.8 or newer) and CMake (3.21 or newer). It does nothing useful yet: for now it only opens an empty window.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/app/tacetos
```

Use the `release` preset for an optimized build.

## Community

- Discord server: https://discord.gg/XfXtZeUFYs
- Contact me on Discord: vadmyro

Questions, ideas and bug reports go to the Discord server. TacetOS does not accept outside contributions: there are no issues or pull requests on GitHub. You are free to read the code and to fork it under the terms of the GPL.

## License

Copyright (C) 2026 vadmyro

TacetOS (app and firmware) is free software, released under the [GPL-3.0-or-later](LICENSE).

The TacetOS name and logo are not covered by the GPL: forks are welcome, but under another name and logo.

TacetOS is not affiliated with Apple. iPod is a trademark of Apple Inc. No Apple firmware is ever included or distributed.
