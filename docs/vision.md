# Vision

> **SOUND is at the end of silence.**

## The idea

Putting an open-source firmware on a classic iPod works well today, but it is still a job for patient people: an installer for the firmware, a terminal for some models, a text file for the settings, a separate tool for themes, and scripts or a music manager that was never designed for it.

TacetOS puts all of that into **one desktop app**. You plug in your iPod, and the app:

1. **detects it** and shows its state live (normal mode, disk mode, DFU, rebooting, mounted...);
2. **prepares it**: backup, format or conversion to FAT32, firmware, bootloader, checks;
3. **guides you** through the steps that must be done by hand, with a clear picture of the buttons and a countdown;
4. **lets you personalise everything**: music, playlists, settings, menus, fonts, themes, boot logo.

The firmware on the iPod is **our own build**, kept as a short stack of patches on top of a pinned base, so that updates stay easy.

## What makes it different

Each piece exists somewhere already. Nobody puts them together on all three desktop systems:

- **all three systems**: Linux, Windows and macOS, from version 1.0;
- **guided install for every model**, including the iPod Classic and its DFU mode, with a live device state;
- **safety first**: blocking checks before writing, mandatory backup of the firmware partition, one-click uninstall;
- **a real settings editor**, generated from the firmware's own list of settings, per model;
- **a theme editor with live preview**, aware of the screen of the plugged-in model and of the firmware version;
- **a music library** with sorting rules you can change, and a database prepared on the computer;
- **firmware and app made for each other**: the app updates the firmware and keeps your settings and themes.

## Principles

- **Never** ship or distribute Apple firmware.
- **Never** write to a disk that is not an identified iPod. Every disk write goes through one small privileged helper.
- **Untested models are marked experimental.** Risky operations stay limited to what is proven on real hardware.
- **Clear and sober style**, in the spirit of the device: light (with a dark mode), lots of space, sharp type, nothing taken from Apple.
- **Every sentence of the interface is translatable** from day one.
