# Decisions

The main choices behind TacetOS, and why. A decision changes only for a real new reason; when it does, it is updated here with the date and the reason.

## The product

### Stack: C++17 + Qt 6 (Widgets) + CMake
Qt 6 runs on Linux, Windows and macOS from one code base, with good USB, disk and translation support, and C++ talks directly to the low-level code that installs bootloaders. The downside, Qt Widgets looking dated, is handled with a custom style sheet (light and dark).

### License: GPL-3.0-or-later
For the app and the firmware. Free for everyone, and every modified version must stay free.

### Name: TacetOS
*Tacet* marks silence on a musical score, which fits the tagline **"SOUND is at the end of silence"**.

### Linux, Windows and macOS from version 1.0

### Style: clear and sober, in the spirit of the device
White and silver, space, sharp type, nothing taken from Apple. A dark mode too.

## The firmware

### Pinned base
- **The firmware on the iPod** is built from a pinned stable base, plus a fix for disk corruption, and our own patches on top.
- **The app's tools** (bootloader install, theme parser, device detection) are written for TacetOS from documented formats and protocols, and tested to write exactly the same bytes as the reference tools.
- Useful newer features are then brought in **one at a time**, each as a tested patch.

### Testing without a drawer full of iPods
The base firmware and tools have been tested on real devices for years. What we add is tested with disk images that look like an iPod, the firmware simulator, one real iPod Classic, and beta testers for the other models.

## The repository

### English everywhere in the product
App, code, docs and commits are in English. French is the second interface language; many more come later.

### Public from the start
Anyone can follow the progress and fork the code. The project does not take outside contributions; questions, ideas and bug reports go to the Discord server. Safeguards: the README says "in development", and development builds refuse to write to a real iPod until the beta.

### Ideas adopted from similar projects
1. blocking checks before writing to the iPod;
2. automatic, mandatory backup of the firmware partition before any write;
3. music database prepared on the computer;
4. "missing tracks" list from a streaming playlist link;
5. themes filtered by the screen size of the plugged-in model;
6. Linux AppImage tested in CI on several distributions.
