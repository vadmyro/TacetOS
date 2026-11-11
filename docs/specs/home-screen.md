# The home screen

The first screen of the app: the iPods that are plugged in, what state each one is in, and the few things the user can do with them in v0.1 (open, eject, refresh). It sits on top of `device-state.md`: the states, the messages and the operations come from there. This spec only says how they look.

Style: light and sober, in the spirit of a music player of the 2000s, nothing taken from Apple. A light and a dark look, which follow the system.

## 1. The window

```
+--------------------------------------------------------------+
| (mark) TacetOS                                    [ Refresh ] |  header, 56 px
+--------------------------------------------------------------+
|                                                              |
|   +------------------------------------------------------+   |
|   | card of the first iPod                               |   |
|   +------------------------------------------------------+   |
|   +------------------------------------------------------+   |
|   | card of the second iPod                              |   |
|   +------------------------------------------------------+   |
|                                                              |
+--------------------------------------------------------------+
```

- **Header**: 56 px high, the `window` colour, a 1 px `border` line under it. On the left, the mark of the logo (`docs/assets/logo/mark.svg` in light, `mark-white.svg` in dark) 24 px high, 10 px of space, then "TacetOS" in the title style. On the right, the Refresh button (secondary style). Side margins 24 px.
- **Body**: a vertical scroll area, no frame, the `window` colour. The cards are in one column, 12 px apart, with 24 px of margin around them. The column is at most 720 px wide and centred in the window, so the cards do not stretch on a wide screen. The scroll bar shows only when the cards do not fit.
- The window keeps its minimum size of 800 x 560 px.

## 2. How many iPods

| iPods | What the body shows |
|---|---|
| 0 | The empty state (section 3) |
| 1 | One card, at the top of the column (not centred vertically: the card must not jump when it grows or shrinks) |
| 2 or more | One card per iPod, in the order they were plugged in (the order of the state model), a new one at the bottom |

A card goes away when its entry leaves the state model (the iPod was unplugged and nothing keeps it, `device-state.md` section 1). A `released` iPod keeps its card until the model drops it. There is no animation in v0.1: cards appear and go at once.

## 3. The empty state

```
                         (mark, 64 px)

                      Connect your iPod.
     TacetOS finds it by itself. Nothing on it changes until you ask.
```

Centred in the body, horizontally and vertically: the mark of the logo 64 px high, 20 px of space, the message of `unplugged` (`device-state.md` section 1.4) in the large style, 8 px, then the second line in the secondary style.

On a system where the app does not detect iPods yet (everything but Linux in v0.1), the second line is replaced by "Detection on this system is coming." The Refresh button stays, and does nothing visible. No error is shown.

## 4. The card of an iPod

```
+----------------------------------------------------------+
| [banner: done or failed, only then]                      |
|                                                          |
|  +--------+   iPod Classic 120 GB (late 2008)            |
|  |        |   o Ready.                                   |
|  | drawing|   TacetOS 0.1                                |
|  |        |   [##########..........] 84.2 GB used of     |
|  +--------+   119.2 GB, 35.0 GB free                     |
|                                                          |
|                             [ Eject ]  [ Show files ]    |
+----------------------------------------------------------+
```

- **Box**: the `card` colour, 1 px `border`, corner radius 12 px, inner margin 20 px. The height follows the content.
- **Drawing** (left): `:/models/<id>.svg` from the model of the iPod, or `generic.svg` when the model is not known for sure (no model, or several candidates). Drawn 112 px high, the width from its own proportions, sharp on high density screens (rendered at the device pixel ratio). 20 px between the drawing and the text. The drawings keep their own colours in both looks: if one does not stand out on the dark card, the fix goes in the drawing.
- **Text** (right, top aligned with the drawing), one line each, in this order:
  1. **Name**: the name of detection (`ipodName`, `detection.md` section 8.4: the variant with its release period, "iPod Classic 120 GB (late 2008)", the same name as the `added` line of `tacet-watch`), or the name of the model when there is no variant, or "iPod" when the model is not known. Title style.
  2. **State**: a dot of 8 px, 8 px of space, then the message of the state. Body style. The colour of the dot is in section 5. A long message wraps on several lines; it is never cut. For `ready` the message is only "Ready.": the name is already above, and the format of the disk tells the user nothing. The same line is kept under the banner of `done` and `failed`.
  3. **Firmware**: the firmware line (`detection.md` section 10, `firmwareMessage`). Secondary style. Hidden when there is no line (`unknown`, `detection.md` section 7.6), and hidden while the state is `released`.
  4. **Space**: only when the music volume is mounted and its size is known. A bar 6 px high, as wide as the text column, radius 3 px, the track in `border` and the used part in `accent`; under it, 6 px lower, "84.2 GB used of 119.2 GB, 35.0 GB free" in the secondary style. Sizes in decimal gigabytes (1 GB = 1 000 000 000 bytes, as on the box of the iPod), one digit after the point, written with the separators of the locale. Hidden while the state is `released`.
- **Released**: after an Eject the volume and the disk are gone, so what detection still reads is poorer than before (the exact variant and the firmware line come from files of the volume). The card keeps the name it showed before the eject, and shows no firmware line and no space. The state model keeps for that the last complete (`ready`) iPod of the port while `released` lasts (`device-state.md` section 2.7).
- **Busy**: while the state is `busy`, the space bar is replaced by a thin indeterminate progress bar (same size, `accent`), and every button of the card is disabled.
- **Buttons**: at the bottom right, 8 px apart, 16 px under the text. Which buttons, in section 6.

### 4.1 The banner

A strip at the very top of the card, inside its box, for `done` and `failed` only: 3 px of colour on the left edge, then the message, on the `window` colour, inner margin 10 px 20 px.

| State | Left edge | Text |
|---|---|---|
| `done` | `accent` | The `done` message of `device-state.md` section 1.4, body style |
| `failed` | `danger` | The message of the error (`device-state.md` section 1.4), body style in `danger` |

`done` goes away on its own (the state model removes it after its delay). `failed` stays until the next action on that iPod (a button of the card, or Refresh). There is no close button in v0.1.

## 5. The dot of the state

| Displayed state | Dot |
|---|---|
| `ready`, `done` | `accent` |
| `reading`, `not-mounted`, `dfu`, `rebooting`, `busy`, `released` | `text-secondary` |
| `attention`, `failed` | `danger` |

## 6. Buttons

What a state offers comes from `device-state.md` section 2.3. On the card, it becomes:

| Displayed state | Buttons, from left to right (the last one is the main one) |
|---|---|
| `ready` | Eject (secondary), Show files (main) |
| `not-mounted`, on Linux | Eject (secondary), Open (main) |
| `attention` with `mac-format` | Eject (secondary), Open read only (main) |
| `failed` | The buttons of the state underneath, as if there were no failure |
| any other | none |

- **Show files** on `ready` opens the mount point in the file manager of the system. It is not an operation: nothing changes on the card.
- **Open** on `not-mounted` starts the `mount` operation. When it ends in `done`, the app opens the mount point, as if the user had pressed Show files on `ready`. When it fails, nothing is opened.
- **Open read only** starts `mount-ro`, then opens the mount point the same way.
- **Eject** starts the `eject` operation.
- **Refresh** (header) asks the state model to read everything again (`detection.md` section 9.5). It is always enabled.
- `unmount` has no button in v0.1: Eject covers what a user needs. It stays in the library for later screens.

A button that starts an operation passes through the state model, which refuses a second operation on the same iPod (`device-state.md` section 2.4); the screen does not need to check it again, but disables the buttons as soon as the card is `busy`.

## 7. Colours

Two palettes, light and dark, built from the colours of the logo. They are the only colours of the app: no colour is written anywhere else in the code.

| Role | Light | Dark | Used for |
|---|---|---|---|
| `window` | `#F6F5F2` | `#1C1F24` | Background of the window, the header, the banner |
| `card` | `#FFFFFF` | `#262A31` | Background of a card |
| `text` | `#1C1F24` | `#F6F5F2` | Main text |
| `text-secondary` | `#5C6068` | `#A9ADB4` | Second lines, firmware, space label, grey dot |
| `border` | `#E3E1DC` | `#353A42` | Lines, card edges, track of the space bar |
| `accent` | `#FF5A4E` | `#FF5A4E` | The dot of `ready` and `done`, the used space, the progress bar, the focus ring. **Never for text** (3.1:1 on white) |
| `danger` | `#C2362B` | `#FF7A70` | Text and dot of a failure or an attention state |
| `button` | `#1C1F24` | `#F6F5F2` | Background of the main button |
| `button-text` | `#F6F5F2` | `#1C1F24` | Text of the main button |
| `button-hover` | `#33373E` | `#E3E1DC` | Background of the main button under the mouse |

The secondary button has no fill: a 1 px `border` edge, `text` for its text, and the `window` colour as fill under the mouse.

Every pair of text and background below must have a contrast ratio of at least 4.5:1 (WCAG 2.1 formula). The values above give:

| Text | Background | Light | Dark |
|---|---|---|---|
| `text` | `window` | 15.2 | 15.2 |
| `text` | `card` | 16.5 | 13.2 |
| `text-secondary` | `window` | 5.8 | 7.3 |
| `text-secondary` | `card` | 6.3 | 6.4 |
| `danger` | `window` | 5.0 | 6.5 |
| `danger` | `card` | 5.5 | 5.7 |
| `button-text` | `button` | 15.2 | 15.2 |
| `button-text` | `button-hover` | 11.0 | 12.6 |

A disabled button keeps its shape, with its text in `text-secondary` and no fill (WCAG does not ask a contrast for disabled controls).

### 7.1 Following the system

The app reads the colour scheme of the system (`QStyleHints::colorScheme`) at start and follows its changes while it runs (`colorSchemeChanged`), without a restart. When the system says nothing (unknown), the light palette is used. There is no setting in the app in v0.1.

## 8. Type

- **Font**: Adwaita Sans, the font of the logo (SIL Open Font License 1.1). It is not stored in the repository: the build takes the file from the system when it is installed (`AdwaitaSans-Regular.ttf`), or downloads a fixed version with a checked hash when asked to (the CI does). When the file is not there, the app uses the font of the system, with no error and no warning to the user.
- **Sizes** (pixels, so that they do not depend on the point size of the system):

| Style | Size | Weight | Colour |
|---|---|---|---|
| Large (empty state) | 22 px | 600 | `text` |
| Title (name of the iPod, "TacetOS" in the header) | 17 px | 600 | `text` |
| Body (state, banner, buttons) | 14 px | 400 (buttons 500) | `text` |
| Secondary | 13 px | 400 | `text-secondary` |

## 9. Keyboard and screen readers

- Tab goes through the buttons: Refresh, then each card from top to bottom, and in a card from left to right. The focus is shown by a 2 px `accent` ring around the button.
- Each card has an accessible name: the name of the iPod, then the message of the state. Each button has an accessible name that includes the iPod when several are plugged in ("Eject iPod Classic 120 GB").

## 10. Texts

Every text of this screen goes through the translation of the app, in the context `home`. New texts of this spec (the others come from `detection.md` section 10 and `device-state.md` section 1.4):

| Where | Text |
|---|---|
| Empty state, second line | TacetOS finds it by itself. Nothing on it changes until you ask. |
| Empty state, other systems | Detection on this system is coming. |
| Buttons | Refresh, Show files, Open, Open read only, Eject |
| Space label | %1 GB used of %2 GB, %3 GB free |
| Unknown model | iPod |
| State line of `ready` | Ready. |

## 11. What the tests need

All without a real iPod, with the offscreen platform, and with states written by hand.

### 11.1 Style
- Both palettes apply, and switching the scheme while the app runs changes the colours of the window.
- Every pair of section 7 reaches 4.5:1, computed in the test from the values of the palette, not copied from this spec.
- Without the font file, the app starts and uses another font.

### 11.2 Card and screen
- One case per displayed state: the message, the dot colour, which buttons are visible and enabled (section 6), the banner for `done` and `failed` only.
- `busy`: every button disabled, the progress bar shown, the space bar hidden.
- Space: shown only with a mounted volume of known size; the label for a known size (for example 84 200 000 000 of 119 200 000 000 bytes).
- The name is the full name of the variant with its release period; `ready` says "Ready." (also under the banner of `done`); `released` keeps the name from before the eject and shows no firmware line and no space.
- Another model gives another drawing; an unknown or ambiguous model gives `generic.svg`.
- 0, 1 and 3 iPods: the empty state, one card, three cards in the order of the model.

### 11.3 Wiring
- With a fake source and a fake clock, an iPod that appears is on the screen within the same simulated second, and its card goes away when it is unplugged.
- Eject on a fake iPod: `busy`, then `released`, then the card goes away.

## 12. Later

- Choosing the model when several share a connection (`ambiguous`): v0.1 shows the message and the generic drawing, no choice.
- Progress of the write operations (backup, install), with their own steps.
- A setting to force light or dark.
- Screenshots of both looks for the README.
