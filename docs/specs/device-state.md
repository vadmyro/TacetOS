# What the app shows about an iPod

What the app shows for each iPod, on top of the states that detection produces (`detection.md` section 8.3), which operations the app can run on an iPod and how they change what is shown, and what the app writes in its log. This spec is meant to be enough to write the state model, the operation runner and the journal without having seen any screen. It does not say how the screens look: that is the home screen spec.

Detection says what the iPod **is**: its state comes only from what is read on the computer. The app also knows what it **does**: an operation is running, it has just finished, the iPod was released to be unplugged, the iPod is restarting. The screen needs both, with one rule for which wins.

Section 1 lists the displayed states and their transitions, section 2 the operations, section 3 the journal, section 4 the constants, section 5 the tests, and section 6 the points still open.

**What is not here.** The reading of the USB list, the disks and the marker files, and the states of detection, are in `detection.md`. What a write operation does step by step (backup, format, install) is in the specs of the bootloaders, and those operations come in later versions: this spec only gives them a place (section 2.2). The text of the messages of detection stays in `detection.md` section 10.

## 1. Displayed states

### 1.1 The list

One **displayed state** per iPod, found by its port path. The words below are the names in the code and the tests, not the text the user reads (section 1.4).

| Displayed state | Meaning | Comes from |
|---|---|---|
| `unplugged` | No iPod to show. It is the state of the whole screen, not of one iPod: it is shown when the list of displayed iPods is empty | No entry of any kind |
| `reading` | An iPod was just seen and the app is still reading it | Detection `waiting` |
| `ready` | The music volume is mounted and readable | Detection `ready` |
| `not-mounted` | The disk is there, the music volume is not mounted | Detection `no-volume`, when the reason is "not mounted" (not Mac format) |
| `dfu` | The iPod is in DFU mode | Detection `dfu` |
| `rebooting` | The iPod went away on purpose and the app is waiting for it to come back | The iPod is held and its USB device is missing (section 1.2) |
| `busy` | An operation of the app is queued or running on this iPod | An operation (section 2) |
| `done` | The operation just finished well | An operation, for `DONE_SHOWN` |
| `failed` | The operation finished badly | An operation, until the next action |
| `released` | The iPod was ejected: it can be unplugged | An `eject` that finished well, for `RELEASED_KEEP` |
| `attention` | Something the user has to read and maybe act on. Carries a **reason**, which is the detection state | Detection `no-disk`, `mac-format`, `conflict`, `ambiguous`, `permission`, `unsupported-filesystem`, `unsupported-model`, and `no-volume` when the reason is Mac format |
| `disk-mode` | Reserved. The iPod is in Disk Mode, as opposed to the normal mode of the firmware | Not produced yet (section 1.5) |

`attention` is one state with a reason, and not one state per reason, because the screen treats them the same way (a message, maybe one action) and the message comes from `detection.md` section 10 by the reason.

A displayed state is a small record: the state, the reason (only for `attention`), and for `busy`, `done` and `failed` the operation (its kind and, for `failed`, the error). The screen never sees more than this and the detected iPod.

### 1.2 Which one wins

Several of these can be true at once: detection says `ready` while an eject is running, or an iPod that was just ejected still shows a disk. The order is fixed:

1. **Operation.** If an operation record exists for the port (section 2.5), the state is `busy`, `done` or `failed` by its status.
2. **Rebooting.** Else, if the detected iPod is held and its USB device is missing (section 9.4 of `detection.md`), the state is `rebooting`.
3. **Released.** Else, if a released record exists for the port (section 2.7), the state is `released`.
4. **Detection.** Else, the state of detection, through the table of section 1.1.
5. **Nothing.** With no iPod, no operation record and no released record, there is no entry. When no entry exists at all, the screen is `unplugged`.

Two consequences. An operation hides what detection says while it runs (the disk and the volume change under it: `ready` then `no-volume`, and the screen must not flicker). And an iPod that is plugged in but has no entry of its own is not possible: step 4 gives every detected iPod an entry.

When the iPod is not in the list of detection (unplugged, dropped) but an operation record or a released record still exists for its port, the entry stays, built from the record alone, as far as sections 2.5 and 2.7 say.

### 1.3 Transitions

Each line is one event on one iPod, with the displayed state before and after. "Detection state" means the state of the table of section 1.1 computed from the detected iPod at that moment. A state that is not named in the "Before" column is not changed by the event.

| # | Before | Event | After |
|---|---|---|---|
| 1 | none (`unplugged`) | A USB device of a known id appears | `reading`, or `dfu` when its mode is DFU |
| 2 | `reading` | Detection finds the music volume | `ready` |
| 3 | `reading` | Detection finds a disk and no volume | `not-mounted`, or `attention` with `mac-format` when the disk says so |
| 4 | `reading` | The disk wait is over with no disk (`DISK_WAIT`) | `attention` with `no-disk` |
| 5 | `reading` | Detection ends on `conflict`, `ambiguous`, `permission`, `unsupported-model` or `unsupported-filesystem` | `attention` with that reason |
| 6 | `attention` (`no-disk`), `not-mounted` | A volume appears | `ready` |
| 7 | `attention` (`no-disk`) | A disk appears with no volume | `not-mounted`, or `attention` with `mac-format` |
| 8 | `ready`, `not-mounted`, `attention` | The user starts an operation that is allowed there (section 2.3) | `busy` |
| 9 | `busy` | The operation ends well, kind `mount`, `mount-ro` or `unmount` | `done` |
| 10 | `busy` | The operation ends well, kind `eject` | `released` |
| 11 | `busy` | The operation ends badly | `failed` |
| 12 | `busy` | The iPod leaves, no restart expected (section 2.6) | `busy` until the operation ends, then no entry: the result is not shown, `unplugged` if no other iPod |
| 13 | `done` | `DONE_SHOWN` has passed | The detection state at that moment |
| 14 | `done` | The user starts an operation | `busy` |
| 15 | `failed` | The user starts an operation | `busy` |
| 16 | `failed`, `released` | The user presses Refresh (section 9.5 of `detection.md`) | The detection state |
| 17 | `released` | `RELEASED_KEEP` has passed | The detection state, or no entry if there is no iPod |
| 18 | `released` | A USB device appears on the port | `reading` (or `dfu`): a new plug in |
| 19 | `released` | The iPod leaves | `released` (the record goes on, section 2.7) |
| 20 | `ready`, `not-mounted`, `attention` | The iPod leaves, not held | No entry: `unplugged` if no other iPod |
| 21 | `ready`, `dfu` | The iPod leaves while held, or from `dfu` | `rebooting` |
| 22 | `rebooting` | A USB device appears on the port within the window (section 2.6) | The detection state of what appeared (`dfu`, or `reading` after a restart to the normal mode) |
| 23 | `rebooting` | The window has passed | No entry, or `failed` when the operation expected the iPod to come back (section 2.6) |
| 24 | `dfu` | The USB device changes mode on the same port | The detection state of the new mode |
| 25 | `done`, `failed` | The iPod leaves | No entry (the record is dropped with the iPod, not shown) |

Rows 12 and 25 are the "no error for unplugging" rule of `detection.md` section 9.3.

Several iPods: each has its own entry and its own transitions. The records of one never touch another. Only the empty list is `unplugged`.

### 1.4 Messages

The messages of the states that come from detection are in `detection.md` section 10. The ones below are new. Same rules as there: short, in the plain words of the app, translatable, no file name the user does not know. Messages are the only thing translated in this spec: the journal never is (section 3).

| Situation | Message |
|---|---|
| `unplugged` | Connect your iPod. |
| `rebooting` | The iPod is restarting. Wait... |
| `busy`, `mount` | Opening the iPod... |
| `busy`, `mount-ro` | Opening the iPod, for reading only... |
| `busy`, `unmount` | Closing the iPod... |
| `busy`, `eject` | Getting the iPod ready to unplug... |
| `done`, `mount` and `mount-ro` | The iPod is open. |
| `done`, `unmount` | The iPod is closed. |
| `failed` | The text of the error (below), then what to do |
| `released` | You can unplug the iPod now. |
| `not-mounted`, where the system can mount (Linux, in v0.1) | The iPod storage is there but is not open on this computer. TacetOS can open it. |

On the other systems `not-mounted` keeps the text of `detection.md` section 10, because the app does not mount there yet.

The text of a failed disk operation is chosen by the error of the service, not by its own words, which go to the journal only:

| Error | Message |
|---|---|
| Not authorized | The system did not allow it. Try again and accept the permission request. |
| Busy | Another program is using the iPod. Close it, then try again. |
| No volume | The iPod has no volume that can be mounted. |
| Service missing | The disk service is not running. Install it, then try again. |
| Not an iPod disk | This disk is not an iPod that the system lets us handle. |
| Anything else | The system could not do it. |

### 1.5 Disk Mode

A `disk-mode` state is reserved. The firmware's normal USB mode and Apple's Disk Mode show the same USB id, so detection reports `normal` for both (`detection.md` section 2). Until a real iPod has shown how to tell them apart from information readable without rights, the app never produces `disk-mode`, and treats the two as the same state. The state exists in the list so that the screens and the tests can already plan a place for it, and no other state changes the day it is turned on. What it will offer (probably the same as `ready` or `not-mounted`, with a line about Disk Mode) is decided then.

## 2. Operations

An operation is something the app does to one iPod, on the request of the user, that takes time or can fail. The runner keeps one record per operation and tells the state model when it changes.

### 2.1 Kinds

| Kind | What it does | In |
|---|---|---|
| `mount` | Asks the disk service of the system to mount the music volume, with read and write | v0.1, Linux |
| `mount-ro` | The same, read only | v0.1, Linux |
| `unmount` | Unmounts every mounted volume of the iPod disk. Succeeds when nothing was mounted | v0.1, Linux |
| `eject` | Unmounts, then asks the service to power the drive off or eject it. The volumes are never left half done: when one cannot be unmounted, the drive is not touched | v0.1, Linux |
| backup, format, install and the others | Write operations, with their own flows | later, see section 2.2 |

The mount is done by the disk service of the system, as the user would from the file manager. TacetOS does not write to the iPod itself in these four kinds, so none needs the helper. `detection.md` section 13 asked whether the app should mount: the answer here is that it does, only when the user asks, and never on its own.

### 2.2 Kinds that come later

The write operations (backup, format, installation, update, uninstall) come in later versions, from the specs of the bootloaders. They fit the same record, with the same statuses, the same hold and the same displayed states. What they add:

- They go through the helper (the app never writes to a disk itself).
- They have their own failure table (`bootloader-legacy.md` section 19, `bootloader-classic.md` section 16), and their `failed` message comes from there, not from section 1.4.
- Some end in a restart of the iPod. They declare it (section 2.6).
- An unplug during a write is **not** "no error": what the app says and does is in the spec of that operation. The rule of section 2.5 about dropping the record on an unplug applies to the four kinds of section 2.1 only.

### 2.3 Where each kind is offered

| Displayed state | Offered |
|---|---|
| `ready` | `unmount`, `eject` |
| `not-mounted` | `mount`, `mount-ro`, `eject` |
| `attention` with `mac-format` | `mount-ro`, `eject` |
| `attention` with another reason, `reading`, `dfu`, `rebooting`, `busy`, `released`, `done` | none (after `done`, the state has gone back to detection within `DONE_SHOWN`) |
| `failed` | what the detection state offers, as if there were no failure |

The runner does not check this table: the screen does. The runner only checks section 2.4, so that a screen that is wrong cannot start two operations.

### 2.4 One at a time

At most one operation per iPod, at any moment, from the acceptance to the end of its record's active part (section 2.5). A second request for the same port is **refused at once**, not queued, and the refusal is returned to the caller and written in the journal. Requests for different iPods do not block each other, but the work is done one after another by one worker (the disk service answers without any time limit, a password window included, so the work must not run on the thread of the screen). Two operations on two iPods can therefore be `queued` and `running` at the same time.

### 2.5 The record

| Field | Meaning |
|---|---|
| `id` | A counter, unique in the session |
| `kind` | Section 2.1 |
| `port` | The port path of the iPod |
| `status` | `queued`, `running`, `done` or `failed` |
| `result` | The error of the service (or none) and its text. The text is for the journal, not for the user |
| `start` | When the request was accepted, in ms of the clock of the app |
| `end` | When it ended, in ms. None until then |

Statuses:

- `queued`: accepted, not started. The worker is busy with another operation.
- `running`: the worker has started it.
- `done`: it ended well. The record is **shown** as `done` for `DONE_SHOWN` from its `end`, then dropped. The state model drops it, not the runner.
- `failed`: it ended badly. It is shown as `failed` **until the next action** on that iPod: the user starts another operation (row 15), presses Refresh (row 16), or the iPod leaves (row 25). There is no timer, because an error that disappears by itself cannot be read.

One record per port at most. A new operation replaces the record of the last one, which is only possible when it is `done` or `failed`.

The clock is the injectable one of the live part, in ms from its own scale, so that the tests run without waiting.

### 2.6 Holding the iPod

An operation holds its iPod for as long as it is active (`detection.md` section 9.4 rule 1):

1. The runner holds the iPod when the request is **accepted**, before it is queued, and releases it when the operation **ends**, in every case: done, failed, an exception, a refusal by the worker. A hold that is never released would keep an iPod on the screen forever. It is a test (section 5.2). The one change is a kind that expects a restart (rule 3): its hold goes on after the end, for the return window, and is released when the iPod is back or the window has passed.
2. An unplug while the operation is active does not show an iPod-gone screen: the entry stays (as `busy`) until the operation ends. When it ends, with no restart expected (the four kinds of section 2.1), the hold is released, the iPod is dropped by the tracker, the record is dropped with it and nothing is shown (row 12). The log gets a warning (section 3.1), not the screen. The one exception is a good `eject`, which leaves a `released` record (section 2.7), since the disappearance of the drive is the aim.
3. The **return window** is how long the app waits for an iPod to come back after a clean restart, per kind. The three seconds of `SAME_PORT_WINDOW` in `detection.md` section 9.4 fit a USB reset, and are **too short for a restart**: the iPod has to shut down, start the new firmware and enumerate again. Each kind that expects a restart declares its window (`RESTART_WINDOW` is the starting value). The four kinds of section 2.1 declare none and use `SAME_PORT_WINDOW`. A flow that asks the user to hold buttons (`bootloader-classic.md` section 16.1) holds the same way, with the window of its own step. While the window runs and the iPod is missing, the state is `rebooting`.
4. When the window passes with no device back, the tracker drops the iPod. For a kind that expected a restart, the operation ends as `failed` with the reason "did not return" and the entry stays as `failed` (row 23), because the spec of that flow has a message and a way out for it. For any other case there is no entry.

The tracker takes the window as a parameter of the hold (without it, `SAME_PORT_WINDOW`). The hold is what makes `rebooting` possible: the state model asks the tracker whether the iPod is missing, and shows `rebooting` when it is (section 1.2 rule 2).

### 2.7 Released

A good `eject` leaves a **released record** for the port. The record outlives the iPod that is probably going away: powering a USB drive off often makes the device disappear by itself, and the user then unplugs the cable.

- It is shown as `released` (the priority of section 1.2: operation, then rebooting, then released, then detection), even when the iPod is no longer in the list of detection.
- It ends after `RELEASED_KEEP` since the eject ended (row 17), when a USB device **appears** on that port (row 18: a new plug in starts from the beginning), when the user presses Refresh (row 16), or when the user starts an operation.
- The disappearance of the iPod does **not** end it (row 19). It is the aim of the eject, and the message "You can unplug the iPod now" stays true while the cable is still in.
- After the eject, the detection state of a drive that is still on the port will usually be `no-volume`, then `no-disk`. The record hides it while it lasts. When it ends and the iPod is still there, that state shows: the screen goes from "unplug" to a message about missing storage. This is accepted: after `RELEASED_KEEP` the user has had time to act, and the message is true.

A failed eject leaves no released record. It is a `failed` operation like the others.

## 3. The journal

The journal is the list of what the app saw and did, kept so that a problem can be explained afterwards, on the Discord, without the user having to remember. It is for people who read logs, not for the screen. Everything that detection decides, with its reasons (`detection.md` section 2, "the logs show why a decision was taken"), and everything an operation does, goes there.

### 3.1 One entry

| Field | Content |
|---|---|
| `time` | UTC, ISO 8601, to the millisecond, with a `Z`: `2026-11-10T14:03:22.481Z` |
| `level` | `info`, `warning` or `error` |
| `domain` | Where it comes from: `usb` (the USB list), `disk` (disks, volumes, marker files), `merge` (the decision of section 8 of `detection.md`), `op` (operations), `app` (everything else: start, stop, the journal itself) |
| `port` | The port path of the iPod it is about, or none |
| `text` | One line of English |

The text is **always English and never translated**, whatever the language of the app: the people who read logs must find the same words. It is written for a reader who knows the specs. It holds numbers and names the way they are (ids, states, counts, errors of the service) and no sentence for the user.

Levels:

- `info`: the normal flow. An iPod added or removed, a state change (`waiting > ready`), the evidence of a decision, an operation that starts and ends well.
- `warning`: unexpected but handled. An unplug during an operation (section 2.6), a read refused, a disagreement that is shown to the user (`conflict`, `other-model`), a second operation refused, a file of the journal that could not be written.
- `error`: something failed that the user will see. A failed operation, a reader that gave up.

### 3.2 What never goes in

- **No file name of the music volume.** Not a track, an album, an artist, a playlist. The journal may hold: the names that TacetOS defines itself (the folders and files of detection and of the install, written by their role and not by their real name, since the real names live in the one constants file), counts, sizes, and the states and errors of the system. When a failure is about a file of the music, the entry says which **operation** failed and with which error, never which file.
- **The home folder is written as `~`.** Any path that starts with the home folder of the account is shortened (`/home/anna/x` becomes `~/x`, `C:\Users\anna\x` becomes `~\x`, `/Users/anna/x` becomes `~/x`). The account name, when it is a whole path component elsewhere (the mount point `/run/media/anna/IPOD`), is replaced by `<user>`.
- **Control characters.** A line feed, a carriage return or another control character in a text is replaced by a space, so that an entry is always one line. A text longer than `JOURNAL_LINE_MAX` is cut and ends with `...`.

This is done by one pure function (`cleanText`) that every entry goes through, not by every caller.

**The serial number.** The USB serial string of the iPod stays in the local journal as it is, because the logs of a given iPod are only useful if they can be told apart from another iPod's. It is masked **only** in the report the user chooses to send, by the code that builds that report (a later step of the roadmap), never in the file on the user's computer.

### 3.3 The buffer

The app keeps the **last `JOURNAL_MAX_ENTRIES` entries** in memory, a ring: when it is full, the oldest is dropped. The screens read from it (a "show the log" view). A new entry is announced to the listeners, with the entry, once. The count of entries dropped is not kept.

### 3.4 The file

Every entry is also written to a file, one line per entry, in the folder of the data of the app, under `logs/` (`JOURNAL_FILE`). The code that writes the journal is given the folder: it never looks for the data folder of the system itself, so that the tests can use a temporary one.

The line is `time level domain port text`, separated by one space, in UTF-8 with `\n` on every system (Windows too). `port` is a `-` when there is none, and a port path has no space, so the line can be cut on the first four spaces. Example:

```
2026-11-10T14:03:22.481Z info usb 1-2 added 05ac:1261 normal
2026-11-10T14:03:24.002Z info disk 1-2 volume mounted, filesystem fat32
2026-11-10T14:03:24.010Z info merge 1-2 waiting > ready
2026-11-10T14:05:01.377Z warning op 1-2 unplugged during eject
```

Rules:

1. Every entry is written and flushed at once, so that a crash leaves the log up to the crash.
2. **Rotation at `JOURNAL_FILE_MAX_BYTES`.** Before an entry is written, if it would take the file over the size, the files are rotated: `tacetos.log` becomes `tacetos.1.log`, that one becomes `tacetos.2.log`, and the old `tacetos.2.log` is deleted. `JOURNAL_FILES` files in all, the current one and two old ones. An entry is never split between two files. A file that is already too big when the app starts is rotated at the first write.
3. **The journal never stops the app.** If the folder cannot be created, the file cannot be opened, or a write fails, the file part is switched off for the rest of the session, one `warning` entry is added to the buffer (not to the file, which is not there), and everything goes on in memory.
4. The journal opens the file in append mode, so several starts of the app go in the same file. The first entry of a start is an `app` `info` one, with the version and the system.

## 4. Constants at a glance

Kept in the one file of constants of the device module (the ones of the journal in its own), with the same names. The times and sizes are starting values.

| Name | Value | Section |
|---|---|---|
| `DONE_SHOWN` | 5 s | 1.1, 2.5 |
| `RELEASED_KEEP` | 30 s | 2.7 |
| `RESTART_WINDOW` | 60 s, to be measured on a real iPod, per kind | 2.6 |
| Window of the kinds of section 2.1 | `SAME_PORT_WINDOW` (3 s, `detection.md` section 11) | 2.6 |
| `JOURNAL_MAX_ENTRIES` | 1000 | 3.3 |
| `JOURNAL_LINE_MAX` | 1000 characters | 3.2 |
| `JOURNAL_FILE` | `logs/tacetos.log`, in the data folder of the app | 3.4 |
| `JOURNAL_FILE_MAX_BYTES` | 1 MiB | 3.4 |
| `JOURNAL_FILES` | 3 (the current one and two old ones) | 3.4 |
| Time format of an entry | `yyyy-MM-ddTHH:mm:ss.zzzZ`, UTC | 3.1 |
| Levels | `info`, `warning`, `error` | 3.1 |
| Domains | `usb`, `disk`, `merge`, `op`, `app` | 3.1 |

## 5. What the tests need

The pure functions are tested with tables, the runner and the model with a fake clock and fake actions, with no sleeping, no D-Bus and no iPod.

### 5.1 Displayed state (section 1)

- A table with one case per row of section 1.3: the detected iPod, the operation record, the released record and the time in, the displayed state out.
- The order of section 1.2: an operation over every detection state; `rebooting` over `released` and detection; `released` over detection; `done` and `failed` over `rebooting`.
- Every state of section 8.3 of `detection.md` reaches the right displayed state, and `no-volume` splits on its reason.
- Scenarios, with the fake clock and a scripted detection: plug in, `reading`, `ready`, `busy` (eject), `released`, unplugged, then `unplugged`; DFU on the same port, then `rebooting`, then `dfu`; `done` gone at exactly `DONE_SHOWN`; `released` gone at exactly `RELEASED_KEEP`; `released` kept when the iPod leaves and ended when a device appears on the port.
- Two iPods, one with an operation, the other not: the transitions of one do not move the other.
- The model sends nothing when the displayed state did not change.

### 5.2 Operations (section 2)

- Success of each kind, with a fake service: the statuses go `queued`, `running`, `done`, the times are the ones of the fake clock, the result is kept.
- An error of each kind of section 1.4 gives `failed` with that error.
- A second request for the same port is refused, and does not touch the first. A request for another port is accepted.
- The hold: taken before the work starts, released after `done`, after `failed`, and after a work that throws, **in every case**.
- An unplug during the work: no error shown, a warning in the journal, the record dropped. A good eject during which the device leaves leaves a released record.
- A kind that expects a restart, with its own window: the iPod is `rebooting` for the window, and the end is `failed` with "did not return".

### 5.3 Journal (section 3)

- The format of a line, with and without a port, and the cut of a line back into its five fields.
- A text with a line feed, with a carriage return, with a control character, and with a text over `JOURNAL_LINE_MAX`.
- The home folder becomes `~`, in each of the three system forms, and a text that holds a path of another account does not change. The account name as a path component becomes `<user>`.
- A text with a music file name: the check is on the callers (no entry of the tests holds one), since the function cannot tell a file name from a word.
- A full buffer: the 1001st entry drops the first, the order is kept, the new-entry notice is sent once per entry.
- The rotation: a file that fills up turns into `.1`, the next into `.2`, the oldest goes, three files at most, no entry split, a start with a file already too big.
- A folder that cannot be created (a file with its name), a file that cannot be written: no crash, one warning in the buffer, the buffer goes on.
- All in a temporary folder given by the test, never the data folder of the system.

## 6. Not decided yet

- How to tell Disk Mode from the normal mode of the firmware (section 1.5). To be tried on a real iPod: the USB text strings and the vendor and model of the disk are readable without rights, and may differ.
- How long a clean restart really takes, per model, from the end of the write to the new enumeration. `RESTART_WINDOW` is a guess until then.
- Whether an iPod whose drive was powered off comes back by itself, and after how long, on each system (section 2.7). If it does, the new device on the port ends `released` early, and a longer rule may be needed.
- Whether the USB serial changes with the mode (`detection.md` section 13). The model uses the port path only until it is known.
- The label of the volume (a name the user chose) goes in the journal as it is. If that turns out to hold names of people, it may need the same care as the file names.
- A "copy the log" or "save the log" button, and what the report holds, belong to the support step of the roadmap.
