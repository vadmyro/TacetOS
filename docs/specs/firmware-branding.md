# Firmware branding

How the TacetOS firmware makes sure that nothing on the iPod shows the name or the logo of the firmware base, and shows TacetOS instead. It also covers which plugins the firmware ships and the name of the firmware folder on the iPod Classic.

Section 2 gives the rule. Section 3 lists every place found in the base where its name or its logo can be seen, and section 4 says which tool fixes each one: the language script, the images, or a patch. Section 5 lists the technical names that stay, and why nobody sees them. Section 6 is the check run on every build. Section 7 is the list of plugins and section 8 the firmware folder of the Classic. Section 9 says which task does what.

The places of section 3 come from a study of the base sources at the commit of `firmware/base.lock` and of a normal build for the iPod Classic, written in our own words. No line of the base is copied here or in our scripts. The exact spellings of the base's name, and every file name that carries it, are only written in `firmware/base.lock` (project rules, D20, D23): this document names them by their key in that file.

## 1. Words used here

- **The base**: the firmware the TacetOS firmware is built on, at the commit of `firmware/base.lock`.
- **The name**: the name of the base, in any case, and its spellings in other alphabets. Key `BRAND_NAMES` of `base.lock`.
- **The new name**: `TacetOS`. Key `BRAND_NEW` of `base.lock`.
- **The logo**: the logo of the base, and its mark (a treble clef on yellow), in any picture of the base.
- **Seen**: drawn on the iPod screen, shown in the file browser of the iPod (with any value of the "Show files" setting), written into a text file that the iPod can open, sent over USB in a name string, or sent to an accessory (car radio, dock) that can display it.
- **Firmware folder**: the hidden folder at the root of the music volume that holds the firmware, its settings and its themes. Its name is a technical name, in the constants file of the app.
- **Boot file**: the firmware file that the bootloader loads, inside the firmware folder. A technical name too.
- **Language script**: `firmware/brand.sh`, run by `apply.sh` after the patches.
- **Build check**: `firmware/check-build.sh`, section 6.

## 2. The rule

Decisions D23 and D54: **nothing that can be seen shows the name, the logo, or the word "rock" in a name**. Where the base shows its name, TacetOS shows `TacetOS`. Where the base shows its logo, TacetOS shows its own logo (`docs/assets/logo/`, D24) at the same size.

- **The word "rock"** is part of the rule for names only: the name of a plugin, the title a plugin gives itself, the name of a theme file. It is not part of the rule for other text, because "rock" is a common word (music genres, "box of rockets" in a game).
- **The technical names** of section 5 stay in the build because the bootloader or the firmware looks for them. None of them can be seen: section 4.4 hides them from the file browser.
- **A new place found later** (a backport, a new plugin) is fixed the same way, and the build check (section 6) fails until it is.

## 3. Where the base shows its name or its logo

### 3.1 Language files

The base has 48 language files. The name is seen in three phrases, in every language: the title of the main menu, the title of the info screen (`<name> Info`), and the title of the menu order screen. Seven languages spell it in their own alphabet (Arabic in two ways, Hebrew, Hindi, Japanese, Korean, Ukrainian), and a few add a grammatical ending to it (Czech, Finnish, Polish, Slovak...). Two more phrases (the "Show files: supported" filter and the version line of the info screen) carry it in one or two languages. The same phrases also have voice lines, which TacetOS does not use (D55) but which are replaced all the same.

The title phrase is also written by the firmware into a small text file of the firmware folder at first start, so that file says `TacetOS!` once the phrase is replaced.

### 3.2 Text in the core firmware

| Where | What is seen | Fix |
|---|---|---|
| USB stack (`firmware/usbstack/usb_core.c`) | The manufacturer and product strings of the USB device, shown by the computer | `TacetOS` and `TacetOS media player` |
| iPod accessory protocol (`apps/iap/iap-lingo0.c`, `apps/iap/iap-lingo4.c`) | The device name sent to accessories, in capitals | `TACETOS` |
| Settings saver (`apps/settings.c`) | The first comment line of every `.cfg` file the firmware writes (a saved theme, saved settings): the name and the base's web address | The line names TacetOS and its version, with no address |
| Debug menu (`apps/debug_menu.c`) | One line shows the full path of the boot file, so the folder and the file name | The line is removed |
| File browser (`apps/filetree.c`) | The firmware folder and the boot file, when "Show files" is `all`; the extension of plugin files, when "Show file extensions" is `on` | Section 4.4 |

The version shown at boot and in the info screen is made by the base from the build's commit and date: it never contains the name. The crash screens show a fixed word and the version: nothing to fix.

### 3.3 Bootloaders

**iPod Classic** (`bootloader/ipod-s5l87xx.c`). Four messages name the base: the title line, "Loading ...", "... loaded.", and the line that tells how to reboot into the custom firmware. One error message prints the boot file name. Fix: the name becomes `TacetOS`, and the error says `Can't load the firmware file:` followed by the reason, with no file name. The boot screen spec (`boot-screen.md`) makes the bootloader silent in normal use; these texts are still fixed, since errors and the verbose mode show them.

**Older iPods** (`bootloader/ipod.c`). The same three messages (title, loading, loaded). Same fix. A comparison with an 8-byte magic word that starts with the name is a technical check of the loaded image, never shown: it stays (section 5).

### 3.4 Pictures

| Picture | Where | Fix |
|---|---|---|
| The logo, one image per screen kind (key `BRAND_LOGO_IMAGES`) | Boot screen of the firmware, logo of some bootloaders | Our logo, same size and depth (section 4.2) |
| The mark, the 32nd icon of each main icon set (the icon named after the base in `apps/gui/icon.h`), key `BRAND_ICON_SETS` | Next to the main menu title, and in plugins that use it | Our mark at the icon size (section 4.2) |
| The logo in the backdrops of the default theme: bottom right of the menu backdrops (`backdrops/`), bottom left of the Now Playing and radio backdrops (`wps/` folder of the theme), key `BRAND_BACKDROPS` | Behind the menus and the Now Playing screen | The logo is painted over with the backdrop's gradient (section 4.2) |
| The logo in the credits plugin | Credits screen | The credits screen is removed (section 4.3) |
| The logo in the "logo" demo plugin | That plugin | The plugin is removed (section 7) |

The picture of the USB screen is a USB plug with no name or mark: it stays.

Logo sizes on the models of TacetOS (width x height x bits per pixel, the size the base uses for each screen):

| Screen | Models | Logo |
|---|---|---|
| 320 x 240, colour | Classic, Video | 320 x 98 x 16 |
| 220 x 176, colour | Color/Photo | 220 x 68 x 16 |
| 176 x 132, colour | Nano 1G and 2G | 176 x 54 x 16 |
| 160 x 128, 4 greys | 1G to 4G | 160 x 53 x 2 |
| 138 x 110, 4 greys | Mini 1G and 2G | 138 x 46 x 2 |

### 3.5 Plugins

Strings in the plugins that are kept (section 7) and that show the name or a "rock" title:

- **Help and menus**: the key help of the CHIP-8 emulator, one line of robotfindskitten, the option "use the system setting" of the clock (worded with the name), the title of the Go game menu, the title screens and menus of the renamed plugins (section 7.3).
- **Animations**: the two texts of the "bounce" demo.
- **Files written**: the comment line of playlists made from a `.cue` file, the client line of the scrobbler log, the program line of saved Go games, the comment line of saved settings.
- **Debug output**: one message of the MIDI player.

Each becomes `TacetOS`, or a neutral word where the name was a label ("system setting", "Go"). Strings inside the game and emulator engines that are internal identifiers (driver names, lookup keys) are never seen and go to the tolerated list (section 5) with their reason.

### 3.6 Theme files shipped with the firmware

Two themes of the base carry the name in their file names: the default icons theme and the failsafe theme (`.cfg`, and the screen files of the failsafe theme). They are listed in "Browse Theme Files" and in the screen lists of the theme menu. Fix: they are renamed `default_icons` and `failsafe`, and the firmware code that loads the failsafe theme by name follows (`apps/gui/skin_engine/skin_engine.c`, the theme remover plugin). The comment line at the top of these files says their new name.

## 4. The fixes

### 4.1 The language script

`firmware/brand.sh`, run by `apply.sh` after the patches, on the sources it made.

- It reads `BRAND_NAMES` and `BRAND_NEW` from `base.lock`.
- In every language file (`apps/lang/*.lang`), it replaces each spelling of the name by the new name, **only in the displayed text and the voice text of each phrase**. Phrase ids, descriptions and comments are left as they are. Latin spellings are matched without regard to case. A grammatical ending stuck to the name stays after the new name (an ending `-u` gives `TacetOSu`): the wording of each language is improved with the translations of P10, not here. Languages without one of the phrases fall back to English, which is replaced too.
- Running it twice gives the same files (idempotent).
- `brand.sh --check DIR` changes nothing. It reads the language files under `DIR` and exits with an error, listing `file:line` for each displayed or voice text that still holds a spelling of the name. With no remains it prints nothing and exits 0.

The script does not touch C code: code text is changed by patches (section 4.3), which are precise and fail loudly when the base changes.

### 4.2 The images

The images are made at build time from `docs/assets/logo/`, by `firmware/brand.sh` after the language step, with Python 3 and Pillow (the CI job installs them). No image of the base is stored in the repository, and no image made from one.

- **Logo**: each file of `BRAND_LOGO_IMAGES` in the sources is overwritten by our banner, scaled to fit the size of that file and centred, at the same width, height and bit depth, in the same BMP format. Colour screens use the banner made for a dark background, greyscale screens the one made for a light background, reduced to 4 grey levels. The base's makefiles then build our logo with no change.
- **Mark in the icon sets**: in each main icon set of the base that the build ships or compiles in (key `BRAND_ICON_SETS`), the 32nd icon is replaced by our mark (`docs/assets/logo/mark.svg`, through `icon-512.png`) at the icon size of that set, with the set's colours and depth. The other icons are left as they are.
- **Backdrops**: each entry of `BRAND_BACKDROPS` is a file and the box of the logo in it, measured once on the base's files. Each row of the box takes the pixel of that row outside the box (the median of each channel, or the most common palette entry), so the gradient goes on where the logo was and the rest of the image (bars, icons, titles) stays as it was. Only the backdrops that show the logo are listed: some sizes have none.
- Running it twice gives the same files. `brand.sh --check DIR` also fails if an image listed in `BRAND_LOGO_IMAGES`, `BRAND_ICON_SETS` or `BRAND_BACKDROPS` under `DIR` is byte for byte the base's file (the SHA-256 of each original is in `base.lock`, key `BRAND_ORIGINAL_SHA256`).
- The icon sets and the backdrops are changed in place: only the pixels of the mark's slot or of the logo's box are written, in the file's own pixel format, so the header, the palette and the other icons stay as they were. Sets without transparency get the icon laid on white, then the nearest colour of the set.

The boot screen spec (`boot-screen.md`, P6.2a) decides how the boot logo is drawn and replaced by the user. This section only makes sure that every logo of the base is ours.

### 4.3 The patches

One patch per subject in `firmware/patches/`, numbered after the ones that exist, each with a short `Subject:` line in English. No patch may name the base in its subject or its added lines.

| Patch subject | Section |
|---|---|
| USB and accessory names | 3.2 |
| Text of written files (settings comment line, playlists, scrobbler, Go games, MP3 header) | 3.2, 3.5 |
| Bootloader messages, Classic and older iPods | 3.3 |
| Debug menu boot path line | 3.2 |
| File browser hides technical names | 4.4 |
| Credits screen removed (the menu item; the plugin leaves through section 7) | 3.4 |
| Theme file names | 3.6 |
| Plugin texts and titles | 3.5, 7.3 |
| Classic firmware folder | 8 |

### 4.4 Hiding technical names

A patch of the file browser makes it skip, whatever "Show files" says:

- at the root of the volume: the firmware folder of the build, the firmware folder name of the base (on the Classic, where the two differ), and a boot file left at the root;
- inside the plugin browser: nothing more, but the extension of plugin files (`.rock`, `.ovl`) is never shown, whatever "Show file extensions" says.

The plugin browser opens inside the firmware folder but never shows the name of the folder that holds the plugins: its title is the category (Games, Applications, Demos), then the name of a sub folder, and going back from a category leaves the browser. No patch is needed for it.

## 5. Technical names that stay

They stay because the boot chain, the firmware, or a game looks for them by name. None is seen: the folders and files are hidden (section 4.4), and the strings are never drawn or written. The list is key `BRAND_TOLERATED` of `base.lock`, one entry per line, each with its reason in a comment. An entry is a piece of text, optionally preceded by a file pattern and a colon (`*.ipod:text`) to allow it only in matching output files or zip paths. A string found by the check is tolerated when, once every tolerated piece that applies to its file is taken out of it, no spelling of the name is left. A string that reads as text (two words or more with letters) is never tolerated, whatever it holds: a technical name is never part of a message, so a message that names a technical folder is a branding fault. The first entries:

- the firmware folder name of the base, on every model except the Classic (its bootloader looks for it, D53);
- the boot file name and the info file name;
- the names of a fallback screen file and of a fonts list file that the firmware builds paths with;
- the header line of the database navigation file (the parser compares it);
- a lookup key of the Doom engine that happens to contain the name (it means "box of rockets").

One more is expected when the builds of the other models run: the magic word of the older iPods' bootloader (section 3.3). The game engines need none: the plugin texts patch gives their audio and video drivers the TacetOS name. The plugins folder and the plugin extensions need no entry: they hold "rock", not the name, and rule 3 of section 6 only looks at plugin names without their extension.

**Adding an entry** is allowed only for a string that is a file or folder name the boot chain or the firmware looks for, or an internal identifier that is never drawn, written to a file, or sent over USB or to an accessory. Each new entry gets its reason, and the task that adds it lists it in its final message.

## 6. The build check

`firmware/check-build.sh --target MODEL --types N,B,S` reads the outputs in `firmware/work/out` and fails, listing each problem, when:

1. an output of a requested type is missing, or older than `base.lock` or than any file of `patches/`;
2. a string of the firmware file, the bootloader file, any plugin or codec in the zip, or any compiled language file, contains a spelling of `BRAND_NAMES` that is not inside a `BRAND_TOLERATED` entry, or that reads as text: two words or more with letters, which a technical name never is (strings of 4 printable characters or more, matched without regard to case);
3. a path in the zip contains a spelling of the name outside a `BRAND_TOLERATED` entry, or a plugin file name (without its extension) contains "rock";
4. an image of `BRAND_LOGO_IMAGES`, `BRAND_ICON_SETS` or `BRAND_BACKDROPS` in the sources used by the build still has the SHA-256 of the base's file (`BRAND_ORIGINAL_SHA256`);
5. with `--plugins`: the zip holds a plugin that is not in the plugin list, or lacks one that is (section 7);
6. with a model whose firmware folder is set (section 8): a path of the zip is not inside that folder.

`--self-test` proves each rule on small files made on the fly, and needs no build. The CI `firmware` job runs the check after each build.

## 7. Plugins

### 7.1 The rule

Decisions D51 and D55, and the choice of 2026-11-23 (D57): only the test and developer tools, the Lua demo scripts and the voice announcer leave. **All games, emulators, applications, viewers and visual demos stay**, except the demo that only shows the base's logo and the credits screen. The list below is made from a normal build of the base for the iPod Classic: 166 plugins and their Lua scripts.

### 7.2 What leaves

| Plugin | Why |
|---|---|
| `rb_info` | Debug information tool |
| `settings_dumper` | Developer tool |
| `md5sum` | Developer tool |
| `battery_bench` | Battery test tool |
| `remote_control` | Test tool that sends button presses |
| the `lua_scripts` folder of the demos (17 scripts) | Lua demos and tests |
| `announce_status` | Speaks the player state: voices are not used (D55) |
| `logo` | Only shows the base's logo |
| `credits` | The credits screen is removed (D57) |

### 7.3 What is renamed

The title the plugin shows on its own screens follows the new name (patch of section 4.3). The file associations of `viewers.config` follow too.

| Base name | TacetOS name | Kind |
|---|---|---|
| `rockboy` | `gameboy` | Emulator |
| `rockpaint` | `paint` | Application |
| `rockblox` | `blocks` | Game |
| `rockblox1d` | `blocks1d` | Game |
| `rocklife` | `life` | Demo |
| `spacerocks` | `meteors` | Game |

### 7.4 What stays

Everything else of the build, in its category of the base:

- **Applications** (27): alarmclock, calculator, calendar, chessclock, clock, dart_scorer, db_commit, dict, disktidy, keybox, keyremap, lamp, lastfm_scrobbler, lrcplayer, main_menu_config, metronome, otp, paint, periodic_table, pitch_detector, playing_time, random_folder_advance_config, resistor, stats, stopwatch, text_editor, and `tagcache` at the top of the plugins folder.
- **Demos** (16): bounce, cube, demystify, fft, fire, fireworks, fractals, life, matrix, mosaique, oscilloscope, pictureflow, plasma, snow, starfield, vu_meter.
- **Games**, self-contained (section 7.5).
- **Emulators and game engines** (section 7.5).
- **Viewers**: bmp, cue_playlist, db_folder_select, file_picker, gif, imageviewer, jpeg, jpegp, lastfm_scrobbler_viewer, lua (and its library folder, needed by the Lua games), midi, mikmod, mp3_encoder, mpegplayer, open_plugins, pdbox, pitch_screen, png, ppm, properties, search, shopper, shortcuts_append, shortcuts_view, sort, speedread, text_viewer, theme_remove, vbrfix, view_text, wav2wv, wavview, windows_lnk.

### 7.5 Games and emulators

For the game library of v0.5 (D55, P8.5c): games are checked by default, emulators and engines are unchecked by default with the note "needs the game file".

**Games** (self-contained): 2048, amaze, blackjack, blocks, blocks1d, boomshine (Lua), brickmania, bubbles, chessbox, chopper, clix, codebuster, dice, flipit, goban, invadrox, jackpot, jewels, maze, mazezam, meteors, minesweeper, pegbox, picross (Lua), pixel-painter (Lua), pong, reversi, robotfindskitten, sliding_puzzle, snake, snake2, sokoban, solitaire, star, sudoku, superdom, wormlet, xobox, and the 42 puzzles of the `sgt_puzzles` folder.

**Emulators and engines** (each needs a file the user provides):

| Plugin | Plays | Needs |
|---|---|---|
| `doom` | Doom | A WAD file |
| `quake` | Quake | The game's data files |
| `wolf3d` | Wolfenstein 3D | The game's data files |
| `duke3d` | Duke Nukem 3D | The game's data file |
| `xworld` | Another World | The game's data files |
| `xrick` | Rick Dangerous | The game's data file |
| `pacbox` | Pac-Man arcade | The arcade ROM |
| `gameboy` | Game Boy and Game Boy Color | A cartridge image |
| `zxbox` | ZX Spectrum | A tape or snapshot file |
| `chip8` | CHIP-8 | A CHIP-8 program |
| `frotz` | Interactive fiction | A story file |

The exact files each one looks for, and where, are written by the game library spec of v0.5, not here.

### 7.6 The list file

`firmware/plugins.list`, ours, one plugin per line: its path in the plugins folder of the base build without extension (`games/rockblox`), then, after a space, its TacetOS name if it is renamed. `#` starts a comment. Only plugins in the file reach the zip. A plugin's overlay files (`.ovl`) and data files go with it. `firmware/plugins.sh` applies the list to the zip right after the build makes it (`build.sh` runs it): plugins not in the list leave, renamed plugins take their new name, and the lines of `viewers.config` follow the renames or leave with their plugin. A plugin is a `.rock`, `.ovl` or `.lua` file; its data files are the files with its path and another extension or its path followed by `_`, and the folders named after it, with or without a leading dot (the library folder of `lua`, the puzzles of `picross`). Data files keep their names when the plugin is renamed, since the plugin looks for them by those names. `plugins.sh --check` changes nothing and lists what breaks rule 5, which is how `check-build.sh --plugins` applies it.

## 8. The firmware folder of the iPod Classic

Decision D53: on the Classic the firmware folder is `.tacetos`. The other models keep the base's name while their bootloader looks for it.

### 8.1 What the base allows

- The base's configure script takes the folder name as an option. The firmware built with it reads and writes its settings, themes, plugins and languages in that folder, and its zip has that folder at the top.
- **It is not enough for the Classic**: the bootloader and the firmware's "reload the boot file" feature look for the boot file in a folder that the model's config header fixes to the base's name, whatever the option says. A newer commit of the base changes this for every model at once; TacetOS does not bring it in, since one line for one model is enough.

### 8.2 What TacetOS does

- **Build**: `build.sh` gives the folder option for the normal build and the bootloader build of a model that has `firmware_folder` in `models/models.toml` (read by `firmware/firmware-folder.sh`). Only the Classic has it. Models without it use the default folder of the constants file (the base's name, which only lives there and in `base.lock`).
- **Patch** (`0010`): the configure script also writes the folder of the build under a second name, `FIRMWARE_DIR`, so that our patches never spell the base's macro. On the Classic the boot file folder is the folder of the build. The bootloader looks for the boot file in the new folder first, then in the base's folder, then at the root; the second step makes an update safe in any order (section 8.3). The firmware watches the new folder for a new boot file. On the other models the two folders are the same and the search is the base's. The same patch gives `FIRMWARE_DIR` to the code that wrote the base's folder by hand (the legal texts, the folder skipped by the random folder tool, the data folders of three game engines and their music library), and the message shown when the folder is missing no longer names it.
- **App**: it reads the folder of a model from `models.toml`, with the default of the constants file. Detection looks in every model's own folder first, then in the default one. The install writes the package into the folder of the model, and a package whose zip has another top folder is refused. A backup and `remove-firmware` also count the default folder, which an install made before this version left. The helper never deals with the firmware folder: it writes to the disk below the file system.

### 8.3 Moving an existing folder

On an install or update of the Classic, when the base's folder exists:

1. If the bootloader in the NOR does not know the new folder (the last install record of this iPod is an install that does not name the new folder in `firmwareFolder`, or there is no record), the bootloader is updated first, with the normal DFU flow (`bootloader-classic.md`). It still finds the boot file in the old folder (section 8.2), so the iPod starts at every step. In the wizard this is the input `bootloaderFirst` of the list of steps: "keep my music" then runs `classic-bootloader` before `firmware` (`install-wizard.md` section 2.3). A clean install formats the volume, so it has no old folder to move. When the last record is an uninstall, no bootloader is there to miss the new folder.
2. If the new folder does not exist, the old one is renamed to the new name, in one rename on the volume. If both exist (an earlier move was cut), the old one's files that are missing in the new one are moved in, and the old one is removed once empty. A file present in both keeps the new one's copy.
3. **Paths** are rewritten: in every `.cfg` file of the folder (settings at its root, themes in its `themes` folder) and in every screen file of its `wps` folder (`.wps`, `.sbs`, `.fms`), each path that starts with the old folder starts with the new one. The firmware only accepts a path inside its own folder, so a theme with the old path would not load. Other files are not touched. A file is written only when it changes, in one move.
4. The firmware files are written into the new folder, as in a normal update. The kept files (`install-wizard.md` section 7.3) are read from the moved folder, so they come with their paths already rewritten.

Steps 2 and 3 are the start of the step `firmware`, right after its clean-up of section 7.5 of the wizard spec. Each step can be cut and run again: the app's resume (install wizard spec) starts again from the step that was cut, and each step does nothing when its work is already done. The install record of the Classic keeps `firmwareFolder`, the folder its bootloader looks in first, for step 1 of the next install.

## 9. Who does what

| Task | Does |
|---|---|
| P6.1b | `brand.sh`: language script (4.1), images (4.2), run by `apply.sh` |
| P6.1c | `check-build.sh` and its self-test (6) |
| P6.1d | The patches of 4.3 except the Classic folder, and the build check passes for N and B |
| P6.11a | `plugins.list` and its use (7.6), `--plugins` of the check |
| P6.9a | Classic folder: build option, patch, `models.toml`, constants, check rule 6 (8.1, 8.2) |
| P6.9b | Moving the folder in the app (8.3) |
