# The install screens

The screens of the install wizard: the window that opens from the card of an iPod, the first screen with the two paths, the checks, the backup, the strong confirmation before an erase, the DFU guide of the iPod Classic, the install, the end, the uninstall, and the report. It sits on top of `install-wizard.md` ("wizard" below): the steps, the waits, the codes and the messages come from there, and this spec only says how they look and what each button sends to the runner. The colours, the type and the buttons are those of `home-screen.md` ("home" below), sections 6 to 9.

Style: the same as the home screen, light and sober, nothing taken from Apple. The drawings of the click wheel are our own.

**Order to write the code.** First the window with its list of steps and its empty pages, wired to a runner with steps that do nothing (sections 1, 2, 11, 12). Then one page at a time, each with its tests: the checks (section 3), the backup (section 4), the erase (section 5), the DFU guide (section 6), the install and the end (sections 7 and 8), the uninstall (section 9), the report (section 10).

## 1. The window

```
+--------------------------------------------------------------------+
| [dev build banner, section 11, only on a development build]        |
+--------------------------------------------------------------------+
| (drawing) Install TacetOS                                          |  header, 72 px
|           iPod Classic 120 GB (late 2008)                          |
+------------------+-------------------------------------------------+
|  v Checks        |                                                 |
|  > Backup        |   the page of the step (sections 2 to 10)       |
|    Erase         |                                                 |
|    Firmware      |                                                 |
|    Bootloader    |                                                 |
|    Check         |                                                 |
|    Done          |                                                 |
+------------------+-------------------------------------------------+
| [ Report ]                               [ Cancel ]  [ main ]      |  footer, 64 px
+--------------------------------------------------------------------+
```

- **One window per iPod**, of its own (not a dialog of the main window, not modal). It is opened from the card of the iPod (section 1.1). Opening it again for the same serial brings the open one to the front. The title of the window is "TacetOS · " followed by the name of the iPod.
- **Size**: 880 x 620 px at the first opening, at least 760 x 540 px.
- **Header**: 72 px high, the `window` colour, a 1 px `border` line under it, side margins 24 px. On the left, the drawing of the model (home section 4) 48 px high, 12 px of space, then two lines: the name of the flow in the title style ("Install TacetOS", "Uninstall TacetOS", "Restore a backup"), and the name of the iPod in the secondary style (the same name as on its card).
- **List of steps** (left): 200 px wide, the `window` colour, a 1 px `border` line on its right, 16 px of inner margin, 4 px between two items. It shows the steps of the flow (wizard section 2.3), with the labels of section 13.1: the steps that share a label are one item (the three backups are "Backup"). Before the flow starts, the list shows the steps that the path chosen on the first screen gives, and changes when the user changes the path. Each item is one line of body text, after a sign 16 px wide:

| State of the step | Sign | Text |
|---|---|---|
| Done | A tick, `accent` | `text` |
| Running or waiting | A filled dot, `accent` | `text`, weight 600 |
| Failed | A cross, `danger` | `danger` |
| To come | A hollow dot, `text-secondary` | `text-secondary` |
| Skipped by a resume (done before the cut) | A tick, `accent` | `text` |

- **Page** (right): the `card` colour, inner margin 32 px, a vertical scroll area when the page does not fit. The page of the step that runs or waits; the user cannot open the page of another step by clicking the list (the list only shows where the flow is).
- **Footer**: 64 px high, the `window` colour, a 1 px `border` line above it, side margins 24 px. On the left, the Report button (secondary), shown once the flow has started (section 10). On the right, Cancel (secondary) then the main button of the page, 8 px apart. The text and the role of the main button change with the page (section 13); a page with nothing to confirm has no main button.

### 1.1 Where the window comes from

The card of the home screen gets three buttons (wizard section 2.1). They are added to the table of home section 6, before the main button, in this order:

| Card | Buttons added | Main button |
|---|---|---|
| A progress file exists for the serial | Resume | Resume |
| Firmware line `apple` or `unknown`, no progress file | Install TacetOS; on a Classic also Uninstall (secondary, wizard section 2.1) | Install TacetOS |
| Firmware line `tacetos` or `custom`, no progress file | Uninstall | Uninstall |

- The buttons are shown on `ready`, on `not-mounted` and on `attention` (the checks of section 3 then say what is missing), and are off while the card is `busy`. When they are shown, Show files and Open stay, as secondary buttons.
- While a wizard window is open for the iPod, the card shows `busy` (the iPod is held, wizard section 2.4 rule 8) and a single button, "Show the wizard", which brings the window to the front.
- The accessible name of each button includes the iPod when several are plugged in, as in home section 9.

### 1.2 Navigation

- The flow only goes forward. A step that ends well leads to the next at once, without a button. The user acts only on a wait (the main button of the page answers it) and on a failure (section 1.4).
- There is no Back button. Before the flow starts, the first screen can be changed freely; once it has started, a wrong choice is undone by Cancel, then Start again.
- **Cancel** is on while a step waits, and during a step until its first write (wizard section 2.4 rule 5). It asks first: "Stop here? Nothing has been written to the iPod in this step. You can resume later from the card", with "Stop" and "Go on". Stop sends `cancel` to the runner, then the page says "Stopped. Resume from the card when you want" and the main button becomes Close.
- **The first write.** The runner tells the screen when a step makes its first write (a signal of the runner, sent when a helper `progress` says it is no longer `cancellable`, or when a step of files makes its first change on the volume). From then on, Cancel is off until the step ends, and its tooltip says "TacetOS is writing to the iPod. This step goes to its end".

### 1.3 What is blocked during a write

While a step of the flow is past its first write:

- **Closing the window** is refused: a message says "TacetOS is writing to the iPod. Wait for the end of this step, then close", and the window stays.
- **Quitting the app** (closing the main window, or the quit shortcut) is refused the same way, with the name of the iPod.
- The card of the iPod is `busy`, with every button off but "Show the wizard".

At any other time, closing the window while a flow runs is a Cancel (with its question); closing it when nothing runs closes it at once. Quitting the app while a flow waits asks once for every open window: "Stop the work on <iPod>? You can resume later".

### 1.4 A failure

A step that fails (the runner's `stepFailed`) shows its page in the failed state: a banner at the top of the page (as home section 4.1: 3 px `danger` edge, the message of the code in `danger`), then what the step had shown when it failed (the progress bar stays where it stopped), then the way out of wizard section 13 as body text. The main button is the offer of the code when there is one ("Try again", "Resume"; section 13.2), else Close. The Report button is shown on every failure.

An offer that the step asks while it waits (`WaitKind::Offer`: put back the settings, write the backup back, try once more, remove anyway) is a page of its own, with the question in the large style, one paragraph that says what each answer does, and two buttons: the yes as the main button, the no as a secondary one ("Not now", "No"). Nothing is written before the yes.

## 2. The first screen

The first page of every window. It is not a step of the runner: the runner starts when the user presses its main button.

### 2.1 Install

```
  Choose how to install

  (o) Clean install                                    recommended
      Everything on the iPod is copied to this computer and checked,
      then the iPod is erased and gets TacetOS only.
      Needs 72.0 GB on this computer, 410.3 GB free.

  ( ) Keep my music
      Only your settings, themes and playlists are copied. TacetOS is
      added next to your music.

  Firmware package    tacetos-0.2.0-classic-6g  0.2.0    [ Choose... ]
  Backup folder       ~/Documents/TacetOS Backups        [ Change... ]

  What will happen: checks, backup, erase, firmware, bootloader,
  check. About 1 h 10 min. Keep the iPod plugged in.
                                                           [ Start ]
```

- **The two paths** (D38, wizard section 2.2) are two radio buttons, each with its title in body weight 600 and two lines of secondary text under it. Clean install is chosen by default and carries the mark "recommended" (secondary style, right aligned). The space line of the clean install gives the space needed (wizard section 5.2) and the free space of the backup folder; the space line of Keep my music gives its own need.
- **A path that cannot be taken** is greyed, with its reason under it in `danger` instead of the space line: the messages of wizard section 2.2 rules 1 and 2. When the clean install is greyed for the space, Keep my music is chosen. When both are greyed, Start is off and the reason of the clean install is shown.
- **Firmware package** (D39): the Choose button opens a file dialog that shows only the manifests of packages (`*.json`). Once one is chosen, the line shows its name, its version and the model it is for, read from the manifest. A package that fails one of the checks of wizard section 7.1 shows the message of the check under the line in `danger` ("Choose another package"), and Start stays off. The last folder used is kept in the settings of the app.
- **Backup folder**: the folder of wizard section 5.1, written with `~` for the home folder. Change opens a folder dialog; the choice is kept for every iPod. The space lines of the paths are computed again at once.
- **What will happen**: the steps of the path in words, separated by commas (section 13.1), the time it should take, and "Keep the iPod plugged in". The time comes from the size of the work at `COPY_SPEED_GUESS`, 30 MB/s (about an hour for a full 120 GB iPod, wizard section 5.5), until the real install measures it (wizard section 16).
- **Start** (main button) is on when a path can be taken and the package passes. It is the confirmation that `bootloader-legacy.md` section 22 asks for an install: the screen names the iPod, the operation and the state it ends in. It starts the runner with the serial, the family, the flow, the path and the package.

### 2.2 Resume

When the window opens with Resume (a progress file exists, wizard section 3.3):

```
  An install was stopped at "Firmware"
  Started on 18 November 2026 at 20:14, stopped at 21:02.
  Your backup is safe in ~/Documents/TacetOS Backups/...

                                [ Start again ]  [ Resume ]
```

- The large style for the first line, which names the label of the step at `current` (section 13.1). The dates in the format of the locale.
- **Resume** (main) calls the resume of the runner. The list of steps shows the steps before `current` as done.
- **Start again** (secondary) asks "Start again from the beginning? The saved progress is deleted. Your backups are kept", then deletes the progress file and shows the first screen.
- A progress file that the runner refuses (`progress-refused`, `progress-mismatch`) shows the message of wizard section 13 on this page, and the first screen under it.

### 2.3 Restore a backup

A link under the first screen, "Restore from a backup", opens the restore page (section 4.3) in the same window. It is also offered at the end of a clean install (section 8).

## 3. The checks

The page of the step `diagnose` (wizard section 4).

```
  Checking this computer and the iPod

  THIS COMPUTER
  v  System          Linux
  v  Helper          Ready
  !  Development build: every step stops before the first write
  x  Disk service    udisks2 is not running
                     [ sudo systemctl enable --now udisks2 ] [ Copy ]

  THIS IPOD
  -  Checked once the computer is ready
  ?  Is the iPod charged?                         [ Yes, it is ]

                                                 [ Check again ]
```

- **Two groups**, "This computer" then "This iPod" (wizard section 4.1), each with its title in the secondary style in capitals, 24 px between the groups.
- **One line per check**, in the order of the wizard: a sign 16 px wide, the name of the check (section 13.3) in body weight 600 in a column of 160 px, then the message in body text, wrapped, never cut. A check that passes shows a short word ("Ready", "Linux", "FAT32", "2.0.1") or nothing.

| Result | Sign | Message colour |
|---|---|---|
| `pass` | A tick, `accent` | `text-secondary` |
| `warn` | An exclamation mark, `text-secondary` | `text` |
| `fail` | A cross, `danger` | `danger` |
| `ask` | A question mark, `text` | `text` |
| `skipped` | A dash, `text-secondary` | `text-secondary` |

- **A command to type** (wizard section 4.4, D34) is shown under its line, in a monospace field that can be selected but not changed, with a Copy button (secondary) on its right that puts the command in the clipboard and says "Copied" for 2 s. The app never runs it.
- **The action of a check** is a button on its line: "Install the rule" for `udev-rule` (`install-udev-rule`, `helper.md` section 6.11). While it runs, the button is off and says "Waiting for your permission"; then the checks run again by themselves. The two commands of `helper.md` section 11.5 are shown under it, as commands to type, for those who prefer.
- **The battery** (`WaitKind::Battery`): its line asks "Is the iPod charged?" and holds the button "Yes, it is" (main style). The answer goes to the runner, which runs the checks again. "No" is not a button: the message under the line says "Charge the iPod for an hour first" and the user comes back later.
- **The list comes from the runner**: the step sends its checks to the screen each time it runs them (a signal of the runner carrying the list of `Check`, added for this page), and the page shows the last list.
- **Check again** (secondary, under the list, right aligned) runs the step again, as a resume of a failed `diagnose` (wizard section 3.3). It is on when the step failed with `check-blocking` or waits for the battery.
- When every blocking check passes, the flow goes on by itself: the page stays 1 s with "Everything is ready" so that the user sees it, then the next step starts.

## 4. The backup and the restore

### 4.1 The backup

The page of `backup-full`, `backup-keep` and `backup-settings` (wizard section 5).

```
  Backing up your iPod
  Copying the music. Each file is read back and checked.

  [##################..............]   58 %
  41.2 GB of 71.0 GB · about 22 min left
  Music/Artist/Album/03 Song.mp3

  Saved to ~/Documents/TacetOS Backups/ABC123/2026-11-18_201430
                                                     [ Open folder ]
```

- Title in the large style, then one line in the secondary style that says what is copied (section 13.2: the music, the settings, the firmware partition).
- **Progress bar**: 8 px high, the width of the page, radius 4 px, track `border`, done part `accent`. Under it, in body text: the bytes done and total (decimal GB with one digit, as home section 4), and the time left of wizard section 5.5, written "about 22 min left", "about 1 h 05 min left", "less than a minute left". During the first 10 s, "Computing the time left".
- **The file being copied**: one line in the secondary style, its path on the volume, cut in the middle with "..." when it is too long. It is never written in the journal (wizard section 5.3 rule 3).
- **The stage** "Copying the firmware partition" replaces the file line during `backup-firmware`.
- **The folder** of the backup, under it, with "Open folder" (secondary), which opens it in the file manager of the system.
- **Cancel** is on during the whole backup (it writes nothing on the iPod). After it, the page says "Stopped. The files already checked are kept: the backup goes on from there at the next try".
- **At the end**, the page shows for 1 s "Backup checked: 1 234 files, 71.0 GB", then the flow goes on. The same line is kept on the end page (section 8).

### 4.2 Not enough space, files that cannot be read

The failures of the backup (`backup-full-disk`, `backup-read-failed`, `backup-name-refused`, `source-changed`, `backup-verify-failed`) use the failed state of section 1.4. For `backup-read-failed` and `backup-name-refused`, the page lists the names of the files concerned (at most 20, then "and 37 more"), in a list that can be selected and copied: the names are shown, never logged. "Nothing was written to the iPod" is always the last line.

### 4.3 The restore

Opened from the first screen ("Restore from a backup") or from the end of a clean install ("Put my music back"). It is not a flow of the runner: it is the restore of wizard section 5.7, with its own progress.

```
  Backups of this iPod

  ( ) 18 Nov 2026, 20:14   Full       1 234 files, 71.0 GB
  ( ) 18 Nov 2026, 20:14   Settings   38 files, 2.1 MB
  ( ) 12 Nov 2026, 09:02   Incomplete
      [ ] Also write back the firmware partition (older iPods)

                                                   [ Restore ]
```

- **The list**: the backups of the serial in the backup folder, the newest first, one radio button each: the date, the kind (section 13.2: Full, Keep my music, Settings), and the count and size of the files. An incomplete backup is greyed with "Incomplete" and cannot be chosen. None is chosen at the opening; Restore is off until one is.
- **The firmware partition** (older iPods, a backup that holds it): a checkbox, off by default. When it is on, Restore asks the confirmation of an install first (`bootloader-legacy.md` section 22), naming the iPod and "The firmware partition of the iPod is written back from this backup".
- **Restore** (main) for a `full` backup asks first: "Copy 1 234 files back to the iPod (71.0 GB)? Files already there are skipped". Then the page shows the progress of section 4.1, with "Putting your music back" as title. Cancel is on between two files.
- **At the end**: "Done: 1 234 files put back" (with how many were already there), and the main button becomes Close. A failure (`backup-damaged`, `restore-write-failed`) says how many files were put back (wizard section 13).
- An iPod of another serial, or not mounted, shows "Plug in the iPod of this backup" and Restore stays off.

## 5. The erase

### 5.1 The strong confirmation

The page of the wait `WaitKind::TypedName` of `format` and `convert` (wizard section 6.2, `bootloader-legacy.md` section 22).

```
  All music on this iPod will be erased

  1 234 files, 71.0 GB. They are in the backup checked a moment ago:
  ~/Documents/TacetOS Backups/ABC123/2026-11-18_201430

  To confirm, type the name of the iPod: JOHN'S IPOD
  [                                    ]

                                       [ Cancel ]  [ Erase the iPod ]
```

- Title in the large style, in `danger`.
- One paragraph: the number of files and the space used, given by the runner (`files`, `bytes`), then where the backup is. For `convert`, a second paragraph: "This iPod is formatted for Mac. It becomes a Windows iPod, which TacetOS needs".
- **The name to type** (`ipodName`) is written in body weight 600, and can be selected. The field under it is empty at the opening and has the focus.
- **Erase the iPod** (main button, `danger` style of section 5.3) stays off until the typed text matches the name with the rule of `helper.md` section 6.7 (spaces around the text do not count, the case does, an empty name never matches). The page uses the same function as the step of the iPod Classic, never its own comparison. Enter in the field presses the button when it is on.
- Cancel is on (nothing is written yet). The text typed is sent to the runner as the answer (`text`).

### 5.2 The progress

The page of the step after the confirmation:

- Title "Erasing the iPod" (or "Converting the iPod"), then "Do not unplug the iPod" in body weight 600.
- The progress bar of section 4.1, with the stage of the helper in words under it (section 13.2: unmount, open, check, backup, write, verify). The stage `open` says "Waiting for your permission" (`helper.md` section 10.1), with no time limit.
- On a Classic (wizard section 6.4), the stages are "Erasing the files" then "Checking", and the count of entries deleted.
- Then "Waiting for the iPod to come back" while the volume returns (wizard section 6.3 step 3), with the seconds left of `VOLUME_RETURN`.
- Nothing can be clicked once the first write is made (section 1.2).

### 5.3 The danger button

A main button with `danger` as its fill and `button-text` as its text. Under the mouse, a 1 px `text` edge is added; the fill does not change. Off, it is as any off button (home section 7). The contrast of `button-text` on `danger` is checked with the others (section 14.1). It is used only for "Erase the iPod".

## 6. The DFU guide

The page of the wait `WaitKind::ReadyForDfu` of `classic-bootloader` and `classic-remove`, and of the stages of `classic-dfu` that follow it (wizard section 9.2, `bootloader-classic.md` sections 2, 12 and 16). Its goal: the user never has to read a manual.

### 6.1 The drawing

An iPod Classic seen from the front, drawn by us in SVG (`app/resources/dfu/wheel.svg`): the case, the screen, the click wheel with its four labels (MENU, the two arrows, the play and pause sign) and the center button. No image of Apple, no photo. It is drawn 240 px high, sharp on high density screens.

The screen shows it in four ways, by changing the colours of named parts of the same SVG (`menu`, `center`, `play`, `screen`), never with a second file:

| Way | MENU | Center | Play | Screen of the drawing |
|---|---|---|---|---|
| Rest | `border` | `border` | `border` | `window` |
| Hold | `accent`, and a pulse | `accent`, and a pulse | `border` | black |
| Release | `border` | `border` | `border` | black |
| Wrong buttons | `border` | `danger` | `danger` | `window` |

The pulse is a change of opacity between 100 % and 60 %, once a second. It is not shown when the system asks for less motion (section 12).

### 6.2 Before Start

```
  Put the iPod in DFU mode
                                       (drawing, rest)
  1. Keep the iPod plugged into this computer.
  2. Press Start. TacetOS ejects the iPod.
  3. Then hold MENU and the center button together,
     and keep holding until TacetOS says to let go
     (about 15 seconds). The screen goes black: that is normal.

  Not the center button and Play: that is Disk Mode.
  Nothing is sent to the iPod before you let go.
                                                         [ Start ]
```

- The drawing on the right, the steps on the left, numbered, in body text. The line about Disk Mode in `danger`, with a small drawing of the "Wrong buttons" way next to it (64 px).
- **Start** (main) answers the wait. Nothing is ejected before it.
- On an uninstall, the title is "Put the iPod in DFU mode to remove TacetOS" and the rest is the same.

### 6.3 Hold, then let go

After Start, the page follows the stages of `classic-dfu` by itself, without any click:

| Stage | Title | Drawing | Under it |
|---|---|---|---|
| `check`, `eject`, `spin-down`, `pause` | "Getting the iPod ready" | Rest | A thin indeterminate bar |
| `wait-device` | "Hold MENU and the center button now" | Hold | The countdown, below |
| `quiet` | "Let go of the buttons now" | Release | "Sending in 10 s", counting down (`bootloader-classic.md` section 16.1) |
| `send` | "Sending TacetOS to the iPod" | Release | The progress bar, in bytes. "Do not unplug the iPod" |
| `resume` | "The iPod restarts" | Release | Section 7.3 |

- **The countdown** of `wait-device` starts at 15 s, from the first `wait-device` event, and counts each second in the large style ("15", "14", ...). Under it, "Keep holding". At 0 it does not go below: the text becomes "Keep holding. It can take a few more seconds", until the helper sees the DFU device or gives up (60 s, wizard section 9.4).
- When the stage `quiet` comes, the change is immediate and the title is announced to screen readers (section 12).
- **The iPod went back to normal mode** (`disk-mode-instead`, wizard section 9.4): the page shows the "Wrong buttons" way, the title "The iPod started in Disk Mode", and "You held the center button and Play. Hold MENU and the center button instead", then Start again (the step waits again).
- **No DFU mode in time** (`dfu-failed`): "The iPod did not appear in DFU mode", then the steps of section 6.2 again, with Start.
- **The iPod keeps leaving DFU mode** (three resets, `bootloader-classic.md` section 16.1): its message, then Start.

## 7. The install

### 7.1 Firmware

The page of `firmware` (wizard section 7):

- Title "Putting TacetOS on the iPod", then the version of the package.
- The progress bar of section 4.1, in bytes, with the stage in words (section 13.2: unpack, settings, swap, check). "Do not unplug the iPod".
- The offer "Put back your settings and themes from <date>" (`WaitKind::Offer` with `offer` `settings`) is a page of section 1.4: "Put them back" (main), "Start fresh" (secondary).

### 7.2 Bootloader of an older iPod

- **Disk Mode** (`WaitKind::DiskMode`, wizard section 8.1): title "Put the iPod in Disk Mode", the steps for its model (section 13.4), a drawing of the model (home section 4) at 160 px, and the main button "The iPod is in Disk Mode". After it, "Waiting for the iPod" with the seconds left of `RESTART_WINDOW` plus `VOLUME_RETURN`.
- **The run**: title "Installing the bootloader" (or "Updating the bootloader", from the state read), the progress bar with the stages of the helper (section 13.2), "Do not unplug the iPod".
- **A failure after the first write** follows `bootloader-legacy.md` section 19 with the offers of the helper (`next`): "Try again" (main) for `offer-retry`; "Write the backup back" (main) for `offer-restore`, with the confirmation of an install first; for `restored`, the message that the backup was written back and the iPod is as before, and Close; for `restore-failed`, the plain words of section 19 (the iPod may not start, Disk Mode is the way back, keep the backup file, ask on the Discord), the folder of the backup with Open folder, and the Report button highlighted as the main one.
- **Resume after a cut** with the state `inconsistent` (wizard section 3.3): the offer page "The firmware partition was left half written. Write the backup back?", "Write the backup back" (main), "Not now".

### 7.3 Bootloader of the iPod Classic, and the restart

After the DFU guide (section 6), the stage `resume` and the wait for the return (wizard section 9.2 step 4):

```
  The iPod restarts

  Listen: a low beep then a high one means it worked.
  Waiting for the iPod: 48 s

  What the beeps mean
    one short high beep     the program runs
    low, then high          done, the iPod restarts
    one long low beep       nothing changed, the iPod is as before
    two long low beeps      not enough room, nothing was written
    three long low beeps    the iPod may not start (see below)
```

- Title, then the line about the beeps, then the seconds left of `RESTART_WINDOW` in body text.
- **The table of tones** of `bootloader-classic.md` section 12, in the secondary style, always shown: the user may hear them before the iPod comes back.
- **The iPod is back with TacetOS**: the step ends, the flow goes on to `verify`.
- **The other cases** of wizard section 9.4 are failure pages (section 1.4) with their messages. `bootloader-not-taken` offers "Try once more" the first time, and the common causes of `bootloader-classic.md` section 16.3 the second time. `ipod-not-back` and a three-tone error never offer to install again: they show the steps to reset the iPod, then DFU mode and the restore by Apple's software (`bootloader-classic.md` sections 12 and 16.3).
- **A development build** stops with `dev-build` before the eject: the page says "This development build does not send anything to the iPod. Everything up to this point was checked", in body text, not as a failure in `danger`.

## 8. The end

The page of `verify` then `finish` (wizard section 11).

- **`verify`**: "Checking the result" with a thin indeterminate bar. A difference is a failure page (`verify-failed`) with what differs.
- **`finish`, install**:

```
  TacetOS is installed                                    (drawing)
  TacetOS 0.2.0 on iPod Classic 120 GB.

  Starting each firmware
  · TacetOS: it starts by itself.
  · Apple's firmware: turn the iPod off, then turn it on while
    holding MENU for a second.
  · Back to TacetOS from Apple's firmware: hold MENU and the center
    button until the screen goes black, then let go at once.

  Backup checked: 1 234 files, 71.0 GB      [ Open folder ]
  [ Put my music back ]  (clean install only)

  Eject the iPod before you unplug it.          [ Eject ]  [ Close ]
```

- Title in the large style, with the drawing of the model at 112 px on the right.
- **Starting each firmware**: for the Classic, the three lines of `bootloader-classic.md` section 13.3, in our words (section 13.5). For an older iPod, the text of its model when the model file has one; else the section is not shown (wizard section 16).
- **The backup**: its summary line and Open folder. After a clean install, "Put my music back" (secondary) opens the restore page (section 4.3) with this backup chosen.
- **Eject** (main) runs the eject of the card (home section 6), then becomes off with "You can unplug the iPod" next to it. Close (secondary) closes the window. The progress file is deleted by the runner (wizard section 11.2).

## 9. The uninstall

The same window, with "Uninstall TacetOS" as its name.

- **The first screen**: title "Back to Apple's software only", then one paragraph: "TacetOS and its bootloader are removed. Your settings and themes are saved on this computer first, and a later install offers to put them back. Your music stays on the iPod". The backup folder line of section 2.1. Then the main button "Uninstall". It is the confirmation of `bootloader-legacy.md` section 22 for an uninstall.
- **Nothing to do** (wizard section 10): on an older iPod with nothing installed, the first screen shows "This iPod has no TacetOS. There is nothing to remove" and no main button but Close. On a Classic with no firmware folder and no install record, it shows "TacetOS does not seem to be on this iPod. Remove it anyway?" with the main button "Remove anyway".
- **The steps** use the pages of the install: the backup (section 4.1, "Saving your settings"), Disk Mode (section 7.2), the run ("Removing the bootloader"), the DFU guide for a Classic (section 6), the restart (section 7.3, with "the iPod comes back with Apple's firmware" as the success), then "Removing the TacetOS files".
- **`still-tacetos`**: a failure page with "Try again" (main) once.
- **The end**: "Apple software only", the model, "Your settings were saved in" and the folder with Open folder, then Eject and Close as in section 8.

## 10. The report

### 10.1 The Report button

The Report button of the footer (section 1) opens the report page over the page of the step, with a Back button (secondary) on the left of the footer to go back to it. It is shown once the flow has started, and on every failure page it is next to the main button too, in the page, as "Copy report for the Discord".

### 10.2 The report page

```
  Report

  Paste this on the TacetOS Discord to get help. It holds no serial
  number, no name of a file of yours and no name of your computer.
  +------------------------------------------------------------+
  | TacetOS 0.2.0 · Linux (Fedora 44) · udisks2 2.11.0          |
  | iPod Classic (dfu) · firmware: apple                        |
  | install · clean · step firmware · firmware-write-failed     |
  | ```                                                         |
  | 21:02:13 error op firmware ...                              |
  +------------------------------------------------------------+
  1 840 of 2 000 characters

  [ Show the journal ]       [ Save full report... ]  [ Copy report ]
```

- One paragraph that says what the report holds and what it does not (wizard section 12.2), then the summary of wizard section 12.3 in a monospace field that can be selected but not changed, 240 px high with its own scroll. Under it, the count of its characters and the limit.
- **Copy report** (main) puts the summary in the clipboard, then says "Copied. Paste it in the help channel of the Discord" for 4 s.
- **Save full report...** (secondary) opens a save dialog in the documents folder, with the name `tacetos-report-2026-11-18_210213.txt` (the local time, no `:`). It writes the full report (D40).
- **Show the journal** (secondary) shows, in place of the summary, the entries of the journal of the flow, one per line (time, level, step, message), with the same masking as the report. It becomes "Show the report" to go back.
- A link "Open the TacetOS Discord" under the paragraph opens `TACETOS_DISCORD_INVITE` in the browser.
- The app never sends the report anywhere.

## 11. The banner of development builds

When the hello of the helper says `realWrites` false (`helper.md` section 9), or when the helper is not found yet, a strip 36 px high runs across the top of the window, above the header: the `window` colour, a 3 px `text-secondary` edge on its left, and in body text "This development build does not write to iPods. Every step runs up to the first write, then stops". It cannot be closed. The same text is the message of the check `writes` (section 3).

The home screen shows nothing of it: a development build only matters once the user starts a flow.

## 12. Keyboard, screen readers and names for the tests

- **Tab** goes through: the controls of the page, from top to bottom, then the footer from left to right. The list of steps is not in the order of Tab (it cannot be clicked), but it has an accessible name ("Steps") and each item says its label and its state ("Backup, done").
- **The focus** at the opening of a page: the main button, except the name field of section 5.1 and the first path of section 2.1. Enter presses the main button when it is on. Escape is Cancel, with its question.
- **Announcements**: each new title of a page, the change to "Let go of the buttons now", each failure, and "Copied" are announced to screen readers (an accessible event on the title, `QAccessible::updateAccessibility` with an alert).
- **Less motion**: when the system asks for it, the pulse of section 6.1 and the 1 s pauses of sections 3 and 4.1 are not used.
- **Colours** never carry a meaning alone: every sign of sections 1 and 3 has its own shape, and every state has its text.
- **Object names** (`QObject::objectName`), fixed, used by the tests and never shown:

| Object | Name |
|---|---|
| The window | `wizardWindow` |
| List of steps | `wizardSteps` |
| The page area | `wizardPages` |
| Main button, Cancel, Report, Back | `wizardMain`, `wizardCancel`, `wizardReport`, `wizardBack` |
| Dev banner | `devBuildBanner` |
| The two paths | `pathClean`, `pathKeep` |
| Package line and its button | `packageLabel`, `packageChoose` |
| Backup folder line and its button | `backupFolderLabel`, `backupFolderChange` |
| What will happen | `planSummary` |
| Resume, Start again | `resumeFlow`, `startAgain` |
| A line of the checks | `check-<id>` (`check-udisks`), its command `command-<id>`, its Copy `copy-<id>`, its action `action-<id>` |
| Check again, battery yes | `checkAgain`, `batteryYes` |
| Progress bar, its text, the file line | `stepProgress`, `stepProgressText`, `stepCurrentFile` |
| Open folder | `openBackupFolder` |
| Name to type, its field, the button | `typedNameExpected`, `typedNameField`, `typedNameConfirm` |
| The DFU drawing, the countdown | `dfuDrawing`, `dfuCountdown` |
| Disk Mode button | `diskModeDone` |
| Offer, yes and no | `offerYes`, `offerNo` |
| Backups of the restore | `backupList`, `restoreFirmwarePartition` |
| Report field, Copy, Save, journal | `reportText`, `copyReport`, `saveReport`, `showJournal` |
| End page Eject, music back | `finishEject`, `putMusicBack` |

## 13. Texts

Every text goes through the translation of the app, in the context `wizard`. The messages of the codes and of the checks come from `install-wizard.md` sections 4 and 13, `bootloader-legacy.md` and `bootloader-classic.md`, and are not repeated here.

### 13.1 The labels of the steps

| Steps | Label |
|---|---|
| `diagnose` | Checks |
| `backup-full`, `backup-keep`, `backup-settings` | Backup |
| `disk-mode` | Disk Mode |
| `format` | Erase |
| `convert` | Convert |
| `firmware` | Firmware |
| `bootloader`, `classic-bootloader` | Bootloader |
| `remove-bootloader`, `classic-remove` | Remove bootloader |
| `remove-firmware` | Remove files |
| `verify` | Check |
| `finish` | Done |

### 13.2 Stages and kinds

| Where | Name | Text |
|---|---|---|
| Helper stage | `unmount` | Closing the iPod's volume |
| | `open` | Waiting for your permission |
| | `check` | Checking the iPod |
| | `backup` | Saving the firmware partition |
| | `write` | Writing |
| | `verify` | Reading back and checking |
| | `record` | Keeping a record |
| Firmware stage | unpack | Unpacking the firmware |
| | settings | Keeping your settings |
| | swap | Switching to the new version |
| | check | Checking the files |
| Backup what | full | Copying the music. Each file is read back and checked |
| | keep | Copying your settings, themes and playlists |
| | settings | Saving your settings and themes |
| Kind of backup | `full`, `keep`, `settings` | Full, Keep my music, Settings |

The names of the stages of the steps of files are those that the steps already give to `progress`; a stage the table does not know shows no text, never its code.

### 13.3 The names of the checks

| Id | Name |
|---|---|
| `system` | System |
| `helper` | Helper |
| `writes` | Writes |
| `udisks` | Disk service |
| `classic-build` | Classic support |
| `udev-rule` | USB permission |
| `backup-folder` | Backup folder |
| `backup-space` | Space for the backup |
| `model` | Model |
| `serial` | Serial number |
| `single-ipod` | One iPod |
| `volume` | Music volume |
| `format` | Format |
| `apple-firmware` | Apple's firmware |
| `bootloader-state` | Firmware partition |
| `package` | Package |
| `firmware-room` | Space on the iPod |
| `battery` | Battery |

### 13.4 Disk Mode, by model

Facts (D22), to be checked against two sources (Apple's support page on Disk Mode, and the manual of each model) by the task that writes the page:

| Models | Steps |
|---|---|
| `ipod-1g2g`, `ipod-3g` | 1. Hold MENU and PLAY until the Apple logo shows (about 6 s). 2. At once, hold PREVIOUS and NEXT until "Do not disconnect" shows |
| Every other older model (click wheel) | 1. Hold MENU and the center button until the Apple logo shows (about 6 s). 2. At once, hold the center button and PLAY until "Do not disconnect" shows |

### 13.5 New texts of this spec

| Where | Text |
|---|---|
| Window titles | Install TacetOS, Uninstall TacetOS, Restore a backup |
| Card buttons | Install TacetOS, Uninstall, Resume, Show the wizard |
| Main buttons | Start, Resume, Start again, Check again, Yes, it is, Erase the iPod, The iPod is in Disk Mode, Try again, Try once more, Write the backup back, Put them back, Uninstall, Remove anyway, Eject, Close, Restore |
| Secondary buttons | Cancel, Report, Back, Choose..., Change..., Copy, Install the rule, Open folder, Put my music back, Start fresh, Not now, Show the journal, Show the report, Save full report..., Copy report |
| Cancel question | Stop here? Nothing has been written to the iPod in this step. You can resume later from the card. (Stop, Go on) |
| Writing | TacetOS is writing to the iPod. This step goes to its end |
| Close refused | TacetOS is writing to the iPod. Wait for the end of this step, then close |
| First screen | Choose how to install, Clean install, Keep my music, recommended, Needs %1 GB on this computer, %2 GB free, Firmware package, Backup folder, What will happen, About %1, Keep the iPod plugged in, Restore from a backup |
| Path texts | Everything on the iPod is copied to this computer and checked, then the iPod is erased and gets TacetOS only. / Only your settings, themes and playlists are copied. TacetOS is added next to your music. |
| Resume | An install was stopped at "%1", Started on %1, stopped at %2, Your backup is safe in %1, Start again from the beginning? The saved progress is deleted. Your backups are kept |
| Checks | Checking this computer and the iPod, This computer, This iPod, Is the iPod charged?, Copied, Everything is ready |
| Backup | Backing up your iPod, %1 GB of %2 GB, about %1 left, less than a minute left, Computing the time left, Saved to %1, Backup checked: %1 files, %2 GB, Nothing was written to the iPod, and %1 more |
| Restore | Backups of this iPod, Incomplete, Also write back the firmware partition, Copy %1 files back to the iPod (%2 GB)? Files already there are skipped, Putting your music back, Done: %1 files put back, Plug in the iPod of this backup |
| Erase | All music on this iPod will be erased, %1 files, %2 GB. They are in the backup checked a moment ago:, To confirm, type the name of the iPod:, Erasing the iPod, Converting the iPod, Do not unplug the iPod, Waiting for the iPod to come back |
| DFU guide | Put the iPod in DFU mode, the steps of section 6.2, Not the center button and Play: that is Disk Mode., Getting the iPod ready, Hold MENU and the center button now, Keep holding, Keep holding. It can take a few more seconds, Let go of the buttons now, Sending in %1 s, Sending TacetOS to the iPod, The iPod started in Disk Mode, You held the center button and Play. Hold MENU and the center button instead |
| Restart | The iPod restarts, Listen: a low beep then a high one means it worked., Waiting for the iPod: %1 s, What the beeps mean |
| End | Checking the result, TacetOS is installed, Starting each firmware, You can unplug the iPod, Eject the iPod before you unplug it., Apple software only, Your settings were saved in |
| Start each firmware (Classic) | TacetOS: it starts by itself. / Apple's firmware: turn the iPod off, then turn it on while holding MENU for a second. / Back to TacetOS from Apple's firmware: hold MENU and the center button until the screen goes black, then let go at once. |
| Uninstall | Back to Apple's software only, the paragraph of section 9, Saving your settings, Removing the bootloader, Removing the TacetOS files |
| Report | Report, the paragraph of section 10.2, %1 of %2 characters, Copied. Paste it in the help channel of the Discord, Open the TacetOS Discord, Copy report for the Discord |
| Dev build | This development build does not write to iPods. Every step runs up to the first write, then stops. / This development build does not send anything to the iPod. Everything up to this point was checked |

## 14. What the tests need

All with the offscreen platform, a runner with steps made by the test (no helper, no disk, no iPod), a fake clock, and the object names of section 12. Nothing touches the user's folders: the backup folder, the data folder and the documents folder are temporary folders.

### 14.1 Window and style
- The window opens from each card button of section 1.1, one per serial: a second opening brings the same window.
- The list of steps for each list of wizard section 2.3, with the labels of section 13.1, and its signs for done, running, waiting, failed and to come.
- Cancel: on while a step waits and before the first write; off after the signal of the first write; the question, then `cancel` sent to the runner.
- Closing the window and quitting the app during a write are refused; during a wait they ask; with nothing running they close.
- The contrast of `button-text` on `danger` reaches 4.5:1 in both palettes, computed from the palette.
- The dev banner with `realWrites` false, none with true.

### 14.2 Pages
- First screen: the clean install chosen by default; greyed with its reason for the space (Keep my music then chosen), Keep my music greyed for a Mac iPod, both greyed for a Mac Classic; a package that fails each check of wizard section 7.1 keeps Start off; a good package shows its version and model; the list of steps changes with the path.
- Resume: the step and dates shown; Start again deletes the progress file after the question and keeps the backups; a refused file shows its message.
- Checks: one case per result and sign; a command and its Copy (the clipboard holds the command); the action of `udev-rule`; the battery button sends `yes`; Check again runs the step again.
- Backup: the bytes, the time left after 10 s of the fake clock, the file line, the end line; the names of `backup-read-failed` listed and not in the journal.
- Restore: the list from manifests made by the test, an incomplete one greyed, the question for a full backup, the count at the end.
- Erase: the button off for an empty text, a wrong case, another name; on for the name with spaces around it; the text sent to the runner is the typed text.
- DFU guide: each stage of section 6.3 gives its title and its way of the drawing (the colours of the named parts read back from the rendered SVG); the countdown from 15 to 0 with the fake clock, then "Keep holding"; `disk-mode-instead` shows the wrong buttons and waits again.
- Install and end: the offers of the helper (`offer-retry`, `offer-restore`, `restored`, `restore-failed`) give their buttons; the end page of each family; Eject runs the eject.
- Uninstall: each family, nothing to do, remove anyway.
- Report: the summary of a journal made by the test, its count, the clipboard after Copy, the file written by Save (a temporary folder given to the dialog by the test), no serial in either.

### 14.3 Accessibility
- Every button has an accessible name; the list of steps says each label and state; the title of a new page is announced.

## 15. Later

- The library sync, which brings the music back without the backup (P7).
- Downloading the package from the releases instead of choosing a file (P6.4).
- Keeping the computer awake during a long backup.
- The pages of Windows and macOS when their ports come (`helper.md` section 12).
- A guide drawn for each older model in Disk Mode, like the one of the Classic.
