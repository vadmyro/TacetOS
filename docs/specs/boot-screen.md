# Boot screen

What the iPod shows from power on to the first menu, and the screens around it: the boot logo and how the user replaces it from the app, the quiet bootloader of the iPod Classic, the language screen of the first start, the logo and progress bar that the iPod Classic shows while its bootloader is installed, and the "logo and big bar" page of the app during an install.

Section 2 says what the user sees. Section 3 is the logo file that the app writes on the iPod, section 4 the TacetOS logo built into the firmware. Sections 5 to 7 are the firmware, the bootloader of the Classic and the language screen. Section 8 is the logo dialog of the app, section 9 the screen of the iPod during the DFU install, section 10 the big progress page of the app. Section 11 gives the size limits, section 12 what the tests need and section 13 which task does what.

The behaviour of the base described here comes from a study of its sources at the commit of `firmware/base.lock`, written in our own words. No line of the base is copied here. Decisions: D31, D48, D54, D56, D58.

## 1. Words used here

- **The base**, **firmware folder**, **boot file**: as in `firmware-branding.md` section 1.
- **The logo file**: the file of section 3, in the firmware folder, that says what the iPod shows at boot. Its name is in the constants file of the app (`kBootLogoFile`).
- **The TacetOS logo**: our banner (`docs/assets/logo/`, D24) at the logo size of the screen, built into the firmware, the bootloader and the DFU installer as described in section 4.
- **The choice**: what the logo file asks for: the user's picture, or a black screen. No logo file, or a logo file that cannot be used, means the TacetOS logo.
- **Logo background**: the colour of the screen around the TacetOS logo: the colour of the top left pixel of the banner used for that screen (section 4).
- **Settings file**: the file where the firmware keeps its settings, at the root of the firmware folder (`config.cfg`, first entry of `kKeptFirmwareFiles`).
- **First start**: a start of the firmware when the firmware folder has no settings file.
- **The DFU installer**: the program that runs on the iPod Classic in DFU mode and writes the bootloader to the NOR (`bootloader-classic.md` section 11).

## 2. What the user sees

**iPod Classic, normal start** (times from `bootloader-classic.md` section 13.1):

| Time | Screen |
|---|---|
| 0 to about 0.5 s | Off. The button check of the bootloader |
| About 0.5 s | The TacetOS logo on its background, backlight on. No text |
| About 2.8 s, the disk is ready | The choice replaces the TacetOS logo, if there is one. Otherwise nothing changes |
| About 3 s | The firmware starts. The screen does not change |
| A moment later | The first start: the language screen (section 7). Any other start: the first screen of the firmware (main menu, or what the start setting says) |

**Older iPods** (1G to 5G, Mini, Nano): their bootloader is already quiet (it shows messages only on an error, or when Right is held at power on), and it has no logo. The screen stays dark until the firmware starts, about 1 s after power on. The firmware then shows the TacetOS logo at once, and the choice as soon as its disk is ready.

**What never shows in normal use**: a version number, a line of text, the boot file name, a progress message. Text shows only on an error (section 6.4), in the bootloader's USB mode, and in the low battery wait of the Classic.

**Same picture everywhere.** The TacetOS logo is the same pixels in the bootloader, the firmware and the DFU installer, at the same place (centred on the screen), so a change of program never makes it move or blink. The only change the user can see in a start is the TacetOS logo giving way to their own picture or to black, once.

## 3. The logo file

### 3.1 Where

At the root of the firmware folder of the iPod (the model's own folder when `models.toml` gives one, else the default one), under the name `kBootLogoFile`, which is `boot-logo.bin`. It is added to `kKeptFirmwareFiles`, so an install that keeps the settings, an update and the backup of an uninstall keep it too (`install-wizard.md` section 7.3).

### 3.2 Format

Little-endian. A header of 16 bytes, then the pixels.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | The bytes `TLGO` (ASCII) |
| 4 | 1 | Version: 1 |
| 5 | 1 | Kind: 0 black screen, 1 colour picture, 2 grey picture |
| 6 | 2 | Width in pixels |
| 8 | 2 | Height in pixels |
| 10 | 2 | Reserved, 0 |
| 12 | 4 | CRC-32 of the pixel bytes: the one of zip (polynomial `0xEDB88320`, start `0xFFFFFFFF`, result inverted). 0 for kind 0 |
| 16 | | The pixels, rows from top to bottom, each row from left to right |

- **Kind 0, black screen**: width 0, height 0, no pixels. The file is 16 bytes long.
- **Kind 1, colour picture**: one pixel is 2 bytes, RGB565 (5 bits red at the top, 6 green, 5 blue), little-endian. The file is `16 + width x height x 2` bytes long.
- **Kind 2, grey picture**: one pixel is 1 byte, 0 black, 1 dark grey, 2 light grey, 3 white. The file is `16 + width x height` bytes long.
- **Width and height are the size of the screen** of the model (`[model.screen]` of `models.toml`). The picture always fills the whole screen: there is no position and no scaling on the iPod.

The largest file is 153 616 bytes (320 x 240 in colour).

### 3.3 When a logo file cannot be used

The firmware and the bootloader read the file the same way. It is **not used** when any of these is true, and the TacetOS logo stays:

1. It cannot be opened or read to the end.
2. The first 4 bytes are not `TLGO`, or the version is not 1, or the reserved bytes are not 0.
3. The kind is not 0, 1 or 2, or it does not match the screen (1 on a grey screen, 2 on a colour screen).
4. For kinds 1 and 2: the width and height are not those of the screen.
5. The length of the file is not the one of section 3.2 for its kind.
6. The CRC-32 of the pixels is not the one of the header.

Nothing is shown about it on the iPod: no message, no delay. A newer version of the file (version 2 and up) is not used by a firmware that only knows version 1.

### 3.4 Reading it

The picture is read and drawn a row at a time, into the drawing buffer of the screen, while the CRC-32 is computed. The screen is updated only after the whole file was checked: if a check of section 3.3 fails on the way, the TacetOS logo is drawn again into the buffer before the update, so a bad file never reaches the screen. Neither program keeps a buffer as large as the picture: the size limit of the bootloader (section 11) does not allow it.

## 4. The TacetOS logo, built in

### 4.1 The picture

The TacetOS logo is the banner of `docs/assets/logo/`, made by `brand.sh` at the logo size of each screen kind of `firmware-branding.md` section 3.4, the same way as the logo images of that section 4.2 (dark banner on colour screens, light banner reduced to 4 greys on grey screens). Here it is kept in a small compressed form, because the bootloader of the Classic and the DFU installer cannot hold the 62 720 bytes of the 320 x 98 picture (section 11).

| Screen | Logo | Kept as |
|---|---|---|
| 320 x 240, colour | 320 x 98 | Up to 16 colours |
| 220 x 176, colour | 220 x 68 | Up to 16 colours |
| 176 x 132, colour | 176 x 54 | Up to 16 colours |
| 160 x 128, grey | 160 x 53 | 4 greys |
| 138 x 110, grey | 138 x 46 | 4 greys |

It is drawn **centred on the screen**, both ways (on 320 x 240: at x 0, y 71), and the rest of the screen is filled with the logo background. Nothing else is drawn: no version, no text.

### 4.2 The compressed form

- **Palette**: up to 16 entries. On colour screens, RGB565 values: the banner at its size is reduced to 16 colours, and entry 0 is the logo background. On grey screens, the 4 grey levels of section 3.2 (0 black to 3 white), entry 0 the logo background.
- **Data**: bytes. The high 4 bits of a byte are a palette entry, the low 4 bits the length of a run minus 1 (runs of 1 to 16 pixels of that entry). Runs follow the pixels row after row and may go on from one row to the next.
- A reader checks that the data gives exactly width x height pixels; a logo built in that does not (a build error) is never drawn.

The 320 x 98 logo takes about 5 KB in this form.

### 4.3 Where it comes from

`brand.sh` writes one C header into the base sources, `firmware/export/tacetos_boot_logo.h`, after the images of `firmware-branding.md` section 4.2: the width, height, palette and data of the logo of each screen kind, chosen by the screen size of the target at compile time. The firmware, the bootloader and the DFU installer each include it from one C file only. `brand.sh --boot-logo SRC` writes only this header into the sources at SRC (the DFU parts of section 9 use it). Running it twice gives the same file. No picture of the base is used for it.

The base's own logo images (branded by `firmware-branding.md` section 4.2) stay as they are: they are no longer drawn at boot, but the build check still makes sure they are ours.

## 5. The firmware

Replaces patch `0001-no-boot-logo.patch`, which kept the screen of the Classic black at boot.

1. **Before its disk is ready** (where the base draws its logo and version): on the iPod Classic, the firmware does not touch the screen, so the picture left by the bootloader stays. On the other models it draws the TacetOS logo on its background.
2. **As soon as the music volume is mounted**, before the settings are read: it reads the logo file (section 3) and draws the choice: the picture, or a black screen. When there is no logo file or it cannot be used, it draws the TacetOS logo. On the Classic, this gives the same pixels as the bootloader drew, so nothing changes on the screen.
3. The picture stays until the first screen of the firmware is drawn (the language screen of section 7, or the first screen of the user). **No wait is added**: the boot takes the same time as without the logo.
4. The backlight follows the base: nothing changes there.
5. **Simulator**: the same. The base waits one second there so that the logo can be seen; that wait stays.

On the Classic, the screen of the iPod must not go dark between the bootloader and step 2. If the start of the screen driver of the firmware clears the panel, the first real test (P6.6a) notes it as a bug to fix in this patch.

## 6. The bootloader of the iPod Classic

### 6.1 Quiet

The bootloader shows no message in normal use: the title, the version, the battery level, the loading lines and the "starting Apple's firmware" line are not drawn, not even into the drawing buffer. Messages show only in the cases of section 6.4. There is no button that turns messages on: Select and Right keep starting the bootloader's USB mode (`bootloader-classic.md` section 13.2), which shows its own text as today.

### 6.2 The logo

1. When the bootloader starts the screen (`bootloader-classic.md` section 13.1 step 4), it draws the TacetOS logo on its background, updates the screen, **then** turns the backlight on, so the user never sees a lit empty screen.
2. The disk spins up. The hold switch can start Apple's firmware (`bootloader-classic.md` section 13.1 step 5): the TacetOS logo stays until Apple's firmware draws its own screen.
3. **Once the music volume is mounted**, before the boot file is loaded, the bootloader reads the logo file (section 3) and draws the choice. When there is no logo file or it cannot be used, it draws nothing: the TacetOS logo is already there.
4. It loads and starts the firmware without touching the screen again.

A start into Apple's firmware with Menu, into diagnostics or into Apple's Disk Mode happens before the screen starts (`bootloader-classic.md` section 13.2): it shows no TacetOS logo, as today.

### 6.3 Low battery

When the battery is too low to spin the disk, the bootloader shows its low battery lines as today (they tell the user to plug a charger and count down to the power off). When the battery is good enough again, the screen goes back to the TacetOS logo before the disk spins up.

### 6.4 Errors

On every error that stops the start (no partition, disk error, boot file missing or unreadable, Apple's firmware cannot be loaded, `bootloader-classic.md` section 13.4), the bootloader clears the screen and shows, from the top: the title line (`TacetOS boot loader`), the version line, then the lines of that error as they are today with the texts of patch `0004-bootloader-messages.patch` (what went wrong, then the way out). The colours stay those of the base. Messages written while the bootloader was quiet are not shown, but the line that names the error is.

### 6.5 Size

The bootloader file must stay under the limit of section 11. `check-build.sh --types B` fails, with both sizes in its message, when it does not.

### 6.6 Older iPods

Their bootloader is already quiet by default and draws no logo; nothing changes in it for this spec. The firmware shows the logo (section 5).

## 7. The language screen

### 7.1 When

At the **first start** (section 1), after the boot logo, before the first screen of the firmware. A start with a settings file never shows it: an update, or an install that put the settings back, goes straight to the user's first screen. A clean install, or "Start fresh", shows it once.

### 7.2 The list

The languages offered come from **our list file**, `firmware/languages.list` in the repository: UTF-8, one language per line, `#` for comments, two fields separated by a tab:

1. the name of the language file of the base, without its extension (`english`);
2. the name of the language **written in that language** (`English`, `Français`, `Deutsch`...).

`build.sh` copies it into the firmware zip as `langs/languages.txt` inside the firmware folder, at the step where the plugin list is applied. **Until v0.7 it holds one line, English (D58)**: the screen, the list and the setting are made and tested now; v0.7 adds the other languages and the fonts they need (section 14).

On the iPod, a line is offered only when its language file is in the firmware folder (English, built into the firmware, always is). Lines that cannot be read are skipped. When the list file is missing or no line is left, there is no screen and the firmware starts in English.

### 7.3 The screen

```
+----------------------------+
| (status bar of the theme)  |
| > English                  |
|   Français                 |
|   Deutsch                  |
|   ...                      |
+----------------------------+
```

- A list of the base, filling the screen, one line per language, in the order of the list file. No title: the screen must be understood in any language. The first line is selected when it opens.
- The look follows the theme in use (at a first start, the default one).
- **Wheel**: moves the selection, and the list scrolls when it is longer than the screen. **Select**: picks the selected language. **Menu** and the other buttons do nothing: there is no way to skip it.
- **USB**, **power off** and the **hold switch** work as on any other screen of the firmware: plugging a cable shows the USB screen, and after it the language screen again.

### 7.4 After the choice

The language is set as Settings > General > Language would set it (its language file is loaded, the built-in English needs none), and the settings are saved at once. The settings file now exists, so the screen never comes back on its own. Then the firmware goes on to its first screen. A power cut before the choice simply shows the screen again at the next start.

The language is changed later in Settings, as in the base. That menu is not changed in v0.3.

## 8. The logo in the app

### 8.1 Where

A secondary button **Boot logo** on the card of an iPod (`home-screen.md` section 6), before Show files, when all of these are true: the state is `ready`, its firmware state is `tacetos` or `present` (`detection.md` section 7), and its model is known (one model, so one screen). A firmware without the patch of section 5 ignores the logo file, which does no harm. The button opens the dialog of section 8.2, modal to the main window.

### 8.2 The dialog

```
+-------------------------------------------------------------+
| Boot logo                                                   |
|                                                             |
|  (o) TacetOS logo           +--------------------------+    |
|  ( ) My picture  [Choose]   |                          |    |
|        (o) Fit  ( ) Fill    |   preview, real pixels   |    |
|  ( ) Black screen           |                          |    |
|                             +--------------------------+    |
|  The new logo shows the next time the iPod starts.          |
|                                     [ Cancel ] [ Put on iPod ] |
+-------------------------------------------------------------+
```

- Title "Boot logo". Three choices, radio buttons: **TacetOS logo**, **My picture**, **Black screen**.
- **When it opens**, the dialog reads the logo file of the iPod: none selects TacetOS logo; kind 0 selects Black screen; a picture that can be used (section 3.3) selects My picture and shows it as "Current picture"; a file that cannot be used selects TacetOS logo and says "The picture on the iPod cannot be read. The iPod shows the TacetOS logo".
- **Choose** opens the file dialog of the system, filtered on the pictures Qt can read. The chosen picture is converted at once (section 8.3) and shown in the preview. **Fit** (the default) and **Fill** change the preview at once.
- **Preview**: the pixels that will go to the iPod, read back from the converted data (not the original picture), at their real size, or twice their size for screens under 200 px wide, in a frame with a 1 px `border` line and 8 px corners. For TacetOS logo, the banner of the app's resources drawn as in section 4.1; for Black screen, black.
- **Put on iPod** (main) writes (section 8.4). Its buttons are disabled while it writes. On success the dialog says "Done. The new logo shows the next time the iPod starts." and its main button becomes Close. On failure it says "The logo could not be written to the iPod: " followed by the reason, and nothing else changed on the iPod.

### 8.3 Converting a picture

1. **Read** with Qt's image reader, rotation of the photo applied, first frame only. Refused with "This file is not a picture the app can read" when it cannot be read, and with "This picture is too large" over 64 MiB or over 16 384 px on a side.
2. **Size**: W x H is the screen of the model (`[model.screen]`). **Fit**: scaled to fit inside W x H keeping its proportions, centred. **Fill**: scaled to cover W x H keeping its proportions, centred, the overflow cut. Smooth scaling. The picture is laid on black, so the bars of Fit and the transparent pixels are black.
3. **Colour screens** (kind 1): each pixel to RGB565 by rounding, `r5 = (r x 31 + 127) / 255`, `g6 = (g x 63 + 127) / 255`, `b5 = (b x 31 + 127) / 255`, in whole numbers. No dithering.
4. **Grey screens** (kind 2): the grey of each pixel, `(299 R + 587 G + 114 B) / 1000`, is reduced to the 4 levels 0, 85, 170, 255 (values 0 to 3) with Floyd-Steinberg error diffusion, every row from left to right.
5. The header of section 3.2 is added, CRC-32 included.

The conversion is a function of the app's library with no window, so that it is tested without a screen.

### 8.4 Writing

Through the file system of the mounted volume, as the firmware step of the wizard does (`install-wizard.md` section 7), in a worker thread:

- **My picture** and **Black screen**: the file is written under a temporary name in the firmware folder (`kBootLogoFile` followed by `.new`), flushed, renamed over the logo file, then read back and compared byte for byte.
- **TacetOS logo**: the logo file is deleted, and a `.new` left by a cut too.
- A volume that goes away during the write fails it with the reason. A `.new` left by a failed write is deleted at the next write.

## 9. The screen of the iPod Classic during the DFU install

D48: the iPod shows the TacetOS logo and a progress bar while the DFU installer writes the bootloader, instead of a black screen. Only the installer: the uninstaller keeps a black screen (it gives the iPod back to Apple's firmware).

### 9.1 What it shows

```
+--------------------------------+
|                                |
|         ( TacetOS logo )       |   y 71, as at boot
|                                |
|     [=========.............]   |   y 193
|                                |
+--------------------------------+
```

- The TacetOS logo exactly as the bootloader draws it (section 4.1), so the restart into the new bootloader changes nothing but the bar, which goes away.
- **The bar**: 192 x 6 px at x 64, y 193. Track `#3A3D44`, done part `#FFFFFF`, both drawn on the logo background.
- **Progress**, from 0 to the full width, in this order (`bootloader-classic.md` section 11): reading the boot slot and finding Apple's image, 0 to 10 %; writing Apple's copy, 10 to 60 %, sector by sector; writing the bootloader, 60 to 90 %, sector by sector; the final comparison, 90 to 100 %. A sector left alone because it already holds the right bytes counts as written.
- **On success**: the full bar until the reset.
- **On any error** (the one, two and three tone ends of `bootloader-classic.md` section 12): the done part turns `#E5484D` and stays until the iPod is reset. The tones still say which error it is; the app's page of `install-screens.md` section 7.3 explains them.

### 9.2 Rules

- **The screen never stops an install.** The screen and backlight are started after the first tone and before anything is read from the NOR. Every wait for the screen hardware has a time limit; if the screen does not answer, the installer goes on with no screen, exactly as today.
- Nothing in the order of `bootloader-classic.md` section 11, the tones or the 2 second end changes. Drawing the bar between two sectors must not make the whole install longer by more than 10 %.
- The installer draws with the base's own driver of the Classic's screen, for every screen revision that driver knows. The patch adds the needed files of the base to the build of the installer (their sources, from the pinned tree), with small stand-ins of our own for the kernel calls those files make; the drawing and the progress are our code.

### 9.3 Where the patch lives

The DFU parts are built from a part of the base sources only (`tests/reference/build-dfu-parts.sh`, D31), so their patches are kept apart: `firmware/patches/dfu/NNNN-<subject>.patch`, not applied by `apply.sh`. `build-dfu-parts.sh` applies them, and runs `brand.sh --boot-logo` (section 4.3), on the sources it builds from: on its own checkout in CI, on a copy with `--sources` (the reference sources are never changed in place). The cache key of the DFU parts in CI adds the files of `firmware/patches/dfu/` and `brand.sh`. Same rules as `firmware/patches/`: our lines only, nothing that names the base.

## 10. The big progress page of the app

### 10.1 When

In the wizard window (`install-screens.md`), the steps that write to the iPod show this page instead of their own progress: the steps labelled Erase, Convert, Firmware, Bootloader, Remove bootloader, Remove files and Check (`install-screens.md` section 13.1). The pages where the user must choose, act or wait for something (the confirmation of an erase, the offer to put the settings back, Disk Mode, the DFU guide, a failure, the end) stay as they are and come back over it when the step needs them. The backup, with its cancel button and time left, keeps its own page.

### 10.2 Layout

```
+---------------------------------------------------+
|                                                   |
|              ( TacetOS banner )                   |
|                                                   |
|   [==================..........................]  |
|   Putting TacetOS on the iPod · 42 %              |
|   Do not unplug the iPod                          |
|                                                   |
+---------------------------------------------------+
```

- In the page area of the window (the list of steps stays on the left).
- **Banner**: the TacetOS banner of the app's resources (the light one in the light look, the dark one in the dark look), 64 px high, centred, at a third of the page height.
- **Bar**: 32 px under the banner, 70 % of the page width, 12 px high, 6 px corners, track `border`, done part `accent`.
- **Under the bar**, 12 px lower: the stage of the step in words (`install-screens.md` section 13.2) and the whole percentage, in the secondary style; under it "Do not unplug the iPod", in body text.
- On the Classic's bootloader step, the table of tones of `install-screens.md` section 7.3 is shown under these lines, as there.

### 10.3 The whole percentage

The bar shows the whole of the writing steps of the flow, not one step. With N such steps in the flow (counted from its list, `install-wizard.md` section 2.3), each one is an equal share: the bar is at `(steps done + part of the current step) / N`.

- **Part of a step**: its `done / total` from its progress (bytes, or seconds for the waits). When its `total` is 0, the part stays where it was.
- **The wait for the Classic to restart** (`install-wizard.md` section 9.2 step 4) has no bytes: its part is the time waited over `RESTART_WINDOW`, at most 0.9, and 1 when the iPod is back.
- The bar never goes back, except at the start of a new flow.

### 10.4 Names for the tests

`bigProgressPage`, `bigProgressBanner`, `bigProgressBar`, `bigProgressStage`. The bar's accessible name is "Install progress", its value the whole percentage.

## 11. Size limits

The DFU install image of the Classic holds the installer and the bootloader together and may not exceed `0x20000` bytes (`bootloader-classic.md` section 8.3): `0x30C` + the installer + `0x7B0` + the bootloader body, padded to 16 bytes. Both grow with this spec, so each gets its own limit:

| Part | Limit | Today | Checked by |
|---|---|---|---|
| The installer payload (`installer-payload.bin`) | 24 576 bytes | about 5 400 | `build-dfu-parts.sh`, which fails over it |
| The bootloader file of the Classic (with its 8 byte prefix) | 103 752 bytes (a body of 103 744 once padded) | 95 208 | `check-build.sh --types B` |

`0x30C` + 24 576 + `0x7B0` + 103 744 = 131 068, under 131 072. The app's image builder still refuses any image over `0x20000`. The rule of `bootloader-classic.md` section 11 (the bootloader of a release never smaller than the one before) still holds.

## 12. What the tests need

- **Logo file** (app library): a file written then read back for each kind and three screens (320 x 240 colour, 176 x 132 colour, 138 x 110 grey); each case of section 3.3 refused; the CRC-32 of a known picture.
- **Conversion**: for the same three screens, a picture already at the screen size gives known pixels (colour rounding, the Floyd-Steinberg levels on a fixed grey ramp); a wide and a tall picture give the right black bars with Fit, and no bars with Fill. Pixels after a scaling are not compared one by one, since the smooth scaling can differ a little between systems: only the size, the bars and the kind.
- **Dialog**: on a `tacet_fakepod` image, each of the three choices written, read back and shown again when the dialog opens; the cut write (`.new` left) cleaned at the next write; the button hidden when the model is not known.
- **Big page**: the percentage from recorded progress of a Classic install (equal shares, the restart wait at most 0.9, never going back).
- **Firmware and bootloader**: `check-build.sh` with the types N and B; the iPod itself in P6.6a (TacetOS logo, own picture, black screen, the language screen at a first start, no text at boot).
- **DFU installer**: `tst_dfu_reference` still passes with the parts rebuilt; the screen is seen on the iPod in P6.6a.

## 13. Who does what

| Task | Does |
|---|---|
| P6.2b | `brand.sh` writes the logo header (4.3); firmware patch (5) in place of `0001-no-boot-logo.patch` |
| P6.2c | Bootloader patch of the Classic (6), size rule of `check-build.sh` (11) |
| P6.13a | Language screen (7): patch, `firmware/languages.list`, copied by `build.sh` |
| P6.2d | Logo file and conversion in the app's library (3, 8.3, 8.4), `kBootLogoFile` in the constants and in `kKeptFirmwareFiles` |
| P6.2e | Boot logo button and dialog (8.1, 8.2) |
| P6.12a1 | DFU installer: screen, backlight and TacetOS logo (9.1 without the bar, 9.2, 9.3), size limit of `build-dfu-parts.sh` (11) |
| P6.12a2 | DFU installer: the bar, its progress and its error colour (9.1) |
| P6.12b | Big progress page of the app (10) |

## 14. Later

- **v0.7, the languages** (D58): the other languages in `firmware/languages.list`, each written in its own words, and a third field for the font a language needs when the default font has no letters for it (Chinese, Japanese, Korean, Hindi, Thai); the language screen then switches the font with the language. The language chosen in the app (D44) may also be written to the iPod then.
- Settings > Language of the firmware showing the same names as the language screen.
- A logo and a bar for the uninstaller of the Classic.
- A boot logo in the bootloaders of the older iPods.
