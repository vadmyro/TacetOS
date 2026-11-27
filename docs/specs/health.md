# Health of the disk and the battery

How the app tells the user that the disk of an iPod is wearing out or that its battery no longer holds a charge. The firmware writes two small text files on the iPod; the app reads them, keeps a history on the computer, and shows a Health window and one line on the card of the home screen. The app never writes anything on the iPod for this.

Decisions: D36 (a Health page, with data written by our firmware), D53 (the Classic folder), D64 (how the wear of the battery is measured).

## 1. Words

- **Health file**: `health.txt` in the firmware folder of the iPod (section 2.1). The state of the disk and the battery at one moment.
- **Charge file**: `charge.txt` in the firmware folder (section 2.2). The battery level at the moment the charger was last unplugged.
- **SMART block**: the 512 bytes a disk returns to the command that reads its SMART values (feature `0xD0`). Its layout is public (section 5.1).
- **Identify block**: the 512 bytes a disk returns to the IDENTIFY DEVICE command: its model name, serial number and firmware revision among others.
- **Runtime**: the counter the firmware keeps of the seconds it has been switched on since the charger was last unplugged. It does not count while the iPod is off. The firmware also keeps the largest value it ever reached, the **top runtime**. Both are saved with the settings and survive a restart.
- **Estimate**: hours of use one full charge gives, computed by the app from a charge file and a later health file (section 6.2).
- **History**: the readings the app keeps on the computer for one iPod (section 7).

## 2. What the firmware writes

Both files are plain ASCII text, one `key: value` per line, lines ending in LF, keys in lower case. The first line is `format: 1` and the last line is `end: ok`: a file without that last line was cut while it was written and the app ignores it. A file never goes over 4 KiB. The firmware never reads them back: it only writes, each time the whole file, opened with truncation. The folder is the firmware folder of the build (`.tacetos` on the Classic, the base's name on the other models), so the patch takes it from the same place as the rest of the firmware.

The writing is one new patch, `firmware/patches/0016-health-files.patch`. It is our own code: it calls the functions the base already has (battery level and voltage, the charge state, the runtime counters, the SMART read and the identify block of the ATA driver, the clock) and writes the lines below. It builds on every model of `models.toml`: what a model does not have is written as `unsupported` or left out, as each table says.

### 2.1 The health file

| Key | Value | When it is there |
|---|---|---|
| `format` | `1` | Always, first line |
| `event` | `usb` or `shutdown` (section 2.3) | Always |
| `clock` | `YYYY-MM-DD HH:MM:SS` from the clock of the iPod, or `unset` when the model has no clock or the clock is not valid | Always |
| `battery_level` | Percent, `0` to `100`, as the firmware shows it, or `unknown` | Always |
| `battery_mv` | Battery voltage in millivolts, as the firmware measures it | Always |
| `charging` | `yes` when the charge state is above "discharging", `error` when the firmware stopped the charger after a fault (its error and disabled states), else `no`. On a model where the hardware alone runs the charge (no charge state in the base), `yes` while the charger is in, as the status bar shows it | Always |
| `runtime_s` | The runtime, in seconds, updated just before the line is written | Always |
| `top_runtime_s` | The top runtime, in seconds, updated the same way | Always |
| `disk_kind` | `hdd` or `flash`: `flash` when the ATA driver says the disk is solid state (a flash card in an adapter), on a model the base says has no hard disk (the Nano 1G has ATA storage, but flash), and on a model whose storage is not ATA | Always |
| `identify_data` | The identify block as 1024 lower case hexadecimal digits, the bytes in the order they are in memory | Models with ATA storage |
| `smart` | `ok`, `unsupported` (the model has no SMART read in the base) or `error <code>` (the read returned that code) | Always |
| `smart_data` | The SMART block as 1024 lower case hexadecimal digits, the bytes in memory order | Only with `smart: ok` |
| `end` | `ok` | Always, last line |

The firmware decodes nothing of the two blocks: the app does (section 5). In the base v4.0 only the Classic has the SMART read, so every other model writes `smart: unsupported` until a backport brings it (section 11).

### 2.2 The charge file

| Key | Value |
|---|---|
| `format` | `1` |
| `clock` | As in the health file |
| `battery_level` | As in the health file, read at the moment the charger was unplugged |
| `battery_mv` | As in the health file, read at the same moment |
| `end` | `ok` |

### 2.3 When

1. **Connected to a computer** (`event: usb`): when the firmware receives the USB connection while it runs, after it has saved its settings and before it hands the disk to the computer (the base does both in its handler of the USB connection, in `apps/misc.c`, before it opens the USB screen). The health file is written there, then the disk is handed over as before. A write that fails is skipped: the USB connection is never delayed by more than the write itself and never refused.
2. **Switched off** (`event: shutdown`): during a clean power off or restart, after the settings are saved and before the disk stops. This is the reading the app sees when the iPod was off when it was plugged in: the iPod then starts straight into its USB screen, before it mounts the disk, so nothing can be written at that connection. While the iPod is off the battery and the runtime do not change, so the reading is still right. A power off forced by a flat battery writes nothing on a hard disk: the base then stops every write to the disk, its settings included, and so does the patch. The app keeps the file written before. A crash writes nothing.
3. **Charger unplugged** (the charge file): each time the firmware sees the charger go away (it resets the runtime at that moment, in its event handler and in the loop of the USB screen). The level and the voltage are read at that moment. When the disk is not available at that moment (the USB screen is showing: the computer holds the disk), the values are kept in memory and the file is written right after the USB screen closes and the disk is back.
4. **The start-up case**: when the iPod starts with the cable in, the base shows the USB screen before it loads its settings, and loading the settings then puts back the runtime saved before. An unplug seen in that early USB screen would be lost. The patch remembers it, and once the settings are loaded it resets the runtime again and writes the charge file with the values kept in memory.

### 2.4 What the firmware never does

- It never shows these files, a menu or a text about them on the iPod: the debug menu of the base keeps its own SMART screen, unchanged.
- It never writes them while the computer holds the disk.
- It never sends a SMART command other than the read the base already does (no self test, no change of a setting of the disk).

## 3. Reading on the computer

**When**: each time the card of an iPod reaches `ready` with the firmware line `tacetos` (`detection.md` section 7.6) and its music volume mounted, and again on Refresh. The app reads the two files from the firmware folder of the model (`models.toml`, then the default folder of the constants file, as detection does). Each file is read only when it is at most `kMarkerMaxBytes` (64 KiB); a larger one is ignored. Reading is all the app does on the iPod.

**A file is usable** when its first line is `format: 1`, its last line is `end: ok`, every line has the `key: value` form, and every key the tables of section 2 mark "Always" is present with a value of the right form. Any other file is ignored, with one log line that says why (for example `health.txt: no end line`). A carriage return at the end of a line is dropped before the line is read. An unknown key is ignored, so that a later firmware can add one. `format` with another number makes the file unusable ("health.txt: format 2 not supported").

**How fresh**: the window says where the reading comes from (section 8): with `event: usb` and the iPod running TacetOS now (the running firmware is not Apple's, `detection.md` section 7.6), it is the reading of this connection, dated with the computer's time of the read. With `event: shutdown`, it is the state when the iPod was last switched off, dated with its `clock` when that is set. With `event: usb` while the iPod runs Apple's firmware or Disk Mode, it is the state at the last connection under TacetOS.

## 4. What the app makes of it

The app turns the two files into one **reading**: the event, the clock, the read time, the battery values, the decoded disk (section 5) and, when the pair allows it, an estimate (section 6.2). A reading has a **disk verdict** (section 5.3) and a **battery verdict** (section 6.3). The functions that parse, decode and judge are pure: they take the bytes of the files and the history, and return the reading, with no file system or clock of their own, so that the tests feed them directly.

## 5. The disk

### 5.1 The blocks

Both blocks are 1024 hexadecimal digits; any other length, or a character that is not a hexadecimal digit, makes the block unusable (the rest of the file is still used). The bytes are in memory order of a little endian iPod, which is the standard layout of these blocks: 16 bit words are little endian.

**The identify block**: 256 words. The model name is words 27 to 46 (40 characters), the serial number words 10 to 19 (20 characters), the firmware revision words 23 to 26 (8 characters). In each word the first character is the high byte. Spaces and NUL characters at both ends are removed. When the low byte of word 255 is `0xA5`, the 512 bytes must add up to 0 modulo 256; when they do not, the block is unusable. When it is not `0xA5` there is no checksum to test.

**The SMART block**: bytes 0 and 1 are the revision. From byte 2, 30 entries of 12 bytes: the attribute id (1 byte, `0` for an empty entry), the flags (2 bytes), the normalised current value (1 byte), the worst value (1 byte), the raw value (6 bytes, little endian) and one reserved byte. Byte 511 is a checksum: the 512 bytes add up to 0 modulo 256. A wrong checksum writes one log line and the block is still used, as the usual disk tools do; a block with no attribute at all is unusable. The raw values the app uses are the low 32 bits of the raw field, except the temperature (section 5.2).

### 5.2 What is shown

| Id | Shown as | Value used |
|---|---|---|
| 5 | Reallocated sectors | Raw |
| 197 | Pending sectors | Raw |
| 198 | Uncorrectable sectors | Raw |
| 10 | Spin-up retries | Raw |
| 194 | Temperature | Low byte of the raw field, in °C, shown only from 1 to 99 |
| 9 | Power-on time | Raw, in hours, as the disk counts it |
| 12 | Power cycles | Raw |
| 193 | Head load cycles | Raw |
| 191 | Drop sensor events | Raw |

An attribute the disk does not report is not shown. The other attributes are not shown in v0.3. For ids 5, 197 and 198 the window also shows the change since the previous reading of the history with a different value, when there is one ("+3 since 12 Nov").

### 5.3 The verdict

| Verdict | When |
|---|---|
| `failing` | Reallocated sectors at least `REALLOCATED_FAILING` (100), or pending sectors at least `PENDING_FAILING` (10), or uncorrectable sectors at least `UNCORRECTABLE_FAILING` (10) |
| `watch` | Not `failing`, and any of ids 5, 197, 198 or 10 above 0 |
| `good` | A usable SMART block and none of the above |
| `unknown` | No usable SMART block (`unsupported`, `error`, an unusable block), or `disk_kind: flash` |

On `flash` the attributes are still shown when the block is usable, but no verdict is made: the meaning of the attributes of flash cards differs from one maker to another.

## 6. The battery

### 6.1 What is shown

The level ("74 %") with the voltage ("3.91 V", two digits after the point), the runtime ("On battery since the last charge: 5 h 12 min"), the top runtime ("Longest on one charge: 31 h 40 min"), and the estimate (section 6.2) when the history has one.

### 6.2 The estimate

A charge file and a health file give one **estimate** when all these hold:

1. both files are usable;
2. the charge file level is at least `FULL_LEVEL` (95): the charge started full, and counts as 100 %;
3. the health file level is known, and the drop `100 - level` is at least `MIN_DROP` (30 points);
4. the runtime is at least `MIN_RUNTIME` (1 hour);
5. when both clocks are set, the clock of the health file is later than the clock of the charge file.

The estimate is `runtime_s * 100 / drop / 3600` hours, kept with one digit after the point. It is the time a full charge lasts with the use the iPod had on that charge, menus and playback alike. A partial charge (the charger unplugged before the battery was full) gives no estimate: the app does not guess where it started.

### 6.3 The verdict

The estimates come from the history (section 7), in the order they were read.

| Verdict | When |
|---|---|
| `watch` | The last health file says `charging: error` |
| `worn` | Not `watch`, at least `MIN_ESTIMATES` (6) estimates, and the **current** value is at most `WORN_RATIO` (0.5) times the **reference** |
| `good` | Not `watch`, at least `MIN_ESTIMATES` estimates, not `worn` |
| `unknown` | Fewer than `MIN_ESTIMATES` estimates, and not `watch` |

The **current** value is the median of the last 3 estimates. The **reference** is the largest median of 3 estimates in a row found anywhere in the history. Medians of three keep one long idle charge or one heavy day from deciding alone. The reference is the best this iPod did since the app knows it: a battery that was already worn when the app first saw it is not detected by this rule (section 11).

## 7. The history

One JSON file per iPod in the data folder of the app: `health/<key>.json`. The key is the first 16 hexadecimal digits of the sha256 of `disk:` followed by the serial number of the identify block, or of `usb:` followed by the USB serial when there is no identify block. With neither, there is no history: the window shows the reading alone, and the battery verdict is `unknown`. The code is given the folder, as for the journal (`device-state.md` section 3.4); it never looks for the data folder of the system itself.

```json
{
  "format": 1,
  "readings": [
    {
      "read_at": "2026-11-27T13:03:12Z",
      "event": "usb",
      "clock": "2026-11-27 14:03:00",
      "battery_level": 74,
      "battery_mv": 3912,
      "runtime_s": 18720,
      "top_runtime_s": 114000,
      "charging": "no",
      "smart": { "5": 0, "197": 0, "198": 0, "10": 0 },
      "estimate_h": 20.0,
      "health_sha256": "...",
      "charge_sha256": "..."
    }
  ]
}
```

- A reading is added when the sha256 of the health file differs from the `health_sha256` of the last reading: the same file read at two connections, or at each Refresh, counts once. `smart` holds the ids of section 5.3 that the block has, and is missing without a usable block. `estimate_h` is there only when the pair gave one (section 6.2), and the same charge file never gives two estimates: when `charge_sha256` is the one of an earlier reading with an estimate, the new reading has none.
- At most `HISTORY_MAX` (500) readings: the oldest go first.
- The file is written whole, through a save that replaces it only once complete (`QSaveFile`). A file that cannot be parsed, or has another `format`, is set aside as `<key>.json.bad` (replacing an older `.bad`), with one log line, and a new history starts.
- The history stays on the computer. It is never sent anywhere and never written on the iPod.

## 8. The Health window

Opened by the Health button of the card (section 9). A window of its own, as the update window, with the title "Health" and a Close button. Every text goes through `tr()`.

```
+----------------------------------------------------------+
| Health                                                   |
| iPod Classic 120 GB (late 2008)                          |
| Read when the iPod was plugged in, 27 Nov 2026, 14:03.   |
|                                                          |
| Disk                                                     |
|   TOSHIBA MK1231GAL, hard disk                           |
|   No sign of wear.                                       |
|   Reallocated sectors                 0                  |
|   Pending sectors                     0                  |
|   Temperature                         31 °C              |
|   Power-on time                       4,210 h            |
|                                                          |
| Battery                                                  |
|   Not enough full charges measured yet (2 of 6).         |
|   Level                               74 % (3.91 V)      |
|   On battery since the last charge    5 h 12 min         |
|   Longest on one charge               31 h 40 min        |
|   Per full charge                     about 20.0 h       |
|                                                          |
|                                            [ Close ]     |
+----------------------------------------------------------+
```

- **The name** is the name of the card (`home-screen.md` section 4).
- **The source line** (section 3, "How fresh"): "Read when the iPod was plugged in, <date and time of the read>." / "Read when the iPod was last switched off, <clock>." (without the date when the clock is `unset`: "Read when the iPod was last switched off.") / "From the last time TacetOS ran on this iPod." Dates and numbers with the format of the locale.
- **Disk**: the model name and "hard disk" or "flash storage" from the identify block and `disk_kind` (the model name is left out without an identify block), then the verdict line, then the attributes of section 5.2.
- **Battery**: the verdict line, then the values of section 6.1. "Per full charge" shows the current value of section 6.3 when there are 3 estimates or more ("about 20.0 h, median of the last 3 charges"), the last estimate alone otherwise, and is hidden with none.
- **No usable health file**: the window shows the name, then "TacetOS has not written a health reading on this iPod yet. Unplug it, then plug it in again while it is on." and Close.

**Verdict lines**:

| Verdict | Text | Style |
|---|---|---|
| Disk `good` | "No sign of wear." | Body |
| Disk `watch` | "Some sectors were replaced or could not be read. Keep a copy of your music on the computer." | Body |
| Disk `failing` | "The disk is failing. Copy your music to the computer soon." | Body, `danger` |
| Disk `unknown`, `flash` | "Flash storage gives no wear data." | Secondary |
| Disk `unknown`, other | "This disk gives no health data." | Secondary |
| Battery `good` | "The battery holds its charge." | Body |
| Battery `watch` | "The charger reported an error at the last reading. Unplug the iPod and plug it in again." | Body |
| Battery `worn` | "The battery lasts about half as long as it used to." | Body, `danger` |
| Battery `unknown` | "Not enough full charges measured yet (2 of 6)." | Secondary |

## 9. On the home screen

On `ready`, when the firmware line is `tacetos` and a usable health file was read:

- **The Health button**, secondary, right after Show files. It is never the main button and it does not change which one is (`update-channel.md` section 8, `home-screen.md` section 6). It is disabled while the card is `busy`, as every button.
- **One line**, under the firmware line and the update line, in the secondary style, only for the most serious of: disk `failing` ("Disk failing: copy your music soon.", in `danger`), battery `worn` ("Battery worn.", in `danger`), disk `watch` ("Disk: some sectors replaced."), battery `watch` ("Charger error at the last reading."). Nothing for `good` and `unknown`. The line stays when a Resume is offered.

Without a usable health file there is no button and no line: an iPod that has never run TacetOS long enough gives nothing to show.

## 10. What the tests need

All on files made by the tests or kept in `tests/fixtures/health/`, never on a real iPod; the history in a temporary folder given by the test.

- **Fixtures**: a health file of a Classic with a hard disk and a clean SMART block; the same with reallocated sectors (`watch`) and with pending sectors over the limit (`failing`); one with `smart: unsupported` (an older model); one with `disk_kind: flash`; a charge file at 100 % and one at 80 %. The blocks are built by the test or by a small script from the public layout of section 5.1 (with the right checksums), never copied from a disk tool or from a real iPod of someone else.
- **Parsing** (section 3): each "Always" key missing; no `end` line; `format: 2`; an unknown key ignored; a value of the wrong form; a file over 64 KiB; CRLF lines read as LF lines.
- **Blocks** (section 5.1): wrong length; a character that is not hexadecimal; the identify strings with their padding; the identify checksum right, wrong, and absent; a wrong SMART checksum (block used, one log line); empty entries; a raw value over 32 bits.
- **Verdicts**: each row of sections 5.3 and 6.3, at the limits (99 and 100 reallocated, 9 and 10 pending).
- **Estimate** (section 6.2): each of the five conditions failing alone; the computed value; the same charge file giving no second estimate.
- **History** (section 7): a reading added once for the same file; the 501st reading dropping the first; a bad file set aside; no key without serials; the medians and the reference on a list of estimates given by the test.
- **Screens**: the window with each source line, with no health file, with each verdict; the card with each line and none, the Health button never the main one.
- **Firmware**: `check-build.sh --target ipod6g --types N` builds with the patch. What the files hold is checked on a real Classic in the device test of v0.3, never by the CI.

## 11. Constants and open points

| Name | Value | Where |
|---|---|---|
| Health file | `health.txt`, in the firmware folder | constants file, firmware patch |
| Charge file | `charge.txt`, in the firmware folder | constants file, firmware patch |
| File limit on the iPod | 4 KiB | firmware patch |
| `REALLOCATED_FAILING` | 100 | constants file |
| `PENDING_FAILING` | 10 | constants file |
| `UNCORRECTABLE_FAILING` | 10 | constants file |
| `FULL_LEVEL` | 95 % | constants file |
| `MIN_DROP` | 30 points | constants file |
| `MIN_RUNTIME` | 1 h | constants file |
| `MIN_ESTIMATES` | 6 | constants file |
| `WORN_RATIO` | 0.5 | constants file |
| `HISTORY_MAX` | 500 readings | constants file |
| History folder | `health/`, in the data folder of the app | constants file |

**Open points**:

- **The limits** of sections 5.3 and 6.3 are first values, to check against real disks and batteries during the device test and the beta.
- **A battery worn before the app knew it**: the rule compares the iPod with itself. A typical value per model would catch it, once enough iPods have been measured with TacetOS.
- **SMART on the older disk models**: the base v4.0 reads SMART on the Classic only. Newer commits of the base bring it to the other disk models, and add the read of the disk's own thresholds (feature `0xD1`), which would let the app use the verdict of the disk itself. Each would be a backport of its own (`firmware/backports.md`), with a device test; they depend on another change of the USB storage code, so they do not apply alone on the base.
