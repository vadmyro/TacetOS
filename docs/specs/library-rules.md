# Library rules

How TacetOS sorts music into the library and copies it to the device. These are the rules of the reference implementation, a shell script the author used before TacetOS existed. Every rule below was checked by running that script on small generated files, so the examples are real results, not guesses.

The library code of TacetOS must give **the same result** as the reference on the same input, except where section 13 proposes a fix. The reference tests record the reference result, and the library code of TacetOS is compared with them.

## 1. Words used here

- **Library**: the folder on the computer that holds everything.
- **Inbox**: the folder named `To Add` inside the library, where new music is dropped.
- **Genre** and **language**: the two folder levels that come before the artist, for example `Rap/FR`.
- **Artist table**: a text file mapping artists to a genre and a language.
- **Device**: the iPod.

## 2. Folders

The library holds these entries, all created when missing:

| Entry | Role |
|---|---|
| `To Add/` | The inbox |
| `Music/` | Sorted music, `Music/<Genre>/<Language>/<Artist>/<Album>/` |
| `Podcasts/` | Copied to the device as it is, never filled by sorting |
| `Audiobooks/` | Copied to the device as it is, never filled by sorting |
| `Playlists/` | Copied to the device as it is, never filled by sorting |
| `Artists.tsv` | The artist table, created empty when missing |

**Genres** (10, in this order): `Rap`, `R&B`, `Afro`, `Latin`, `Pop`, `Electro`, `Rock`, `Reggae`, `OST`, `Other`.

**Languages** (7, in this order): `FR`, `US`, `UK`, `DE`, `ES`, `IT`, `Other`.

In the reference both lists are fixed. In TacetOS they are settings, with these values as the defaults.

**Folder skeleton.** At the start of every run, and again at the end, the reference creates `To Add/<Genre>/<Language>/` for all 70 pairs and `Music/<Genre>/` for the 10 genres. Language folders inside `Music/` are created only when a file needs them. The cleanup in section 10 removes empty inbox folders, which is why the skeleton is created a second time.

## 3. Overview

Every run does two steps:

1. **Sort**: move audio files from the inbox into `Music/`, then clean the inbox.
2. **Sync**: copy the library to the device, unless the run is sort-only.

Sorting never touches `Music/`, `Podcasts/`, `Audiobooks/` or `Playlists/` except to add files to `Music/`.

## 4. Which files are sorted

Every regular file under the inbox, at any depth, whose extension is one of the following, in any letter case: `mp3`, `flac`, `m4a`, `aac`, `ogg`, `oga`, `opus`, `wav`, `wma`, `ape`, `wv`, `mpc`, `aif`, `aiff`.

- Other files are left alone: `m4b`, `mp4`, `mka`, `txt`, `jpg`, `m3u8`, `cue`, files with no extension, and names such as `x.mp3.part`.
- A folder named like an audio file (`dir.mp3`) is not a file. The audio files inside it are sorted normally.
- Hidden files count. A file named exactly `.mp3` is sorted.
- The reference handles files in directory order, which is not defined. This only matters for duplicates (section 8).

## 5. Tags

For each file the reference reads six tags: album artist, artist, album, title, track and disc. Only the first line is used when a tag value spans several lines (`First Line` and `Second Line` give `First Line`). Tag names are matched without regard to case.

The **artist** used for sorting is the album artist when it is not empty, otherwise the artist.

**Track and disc numbers**: take the text before the first `/`, then remove every character that is not an ASCII digit. If nothing is left, the number is absent.

| Tag | Number | Tag | Number |
|---|---|---|---|
| `5` | 5 | `A1` | 1 |
| `3/12` | 3 | `abc` | absent |
| `07` | 7 | `1-2` | 12 |
| `0` | 0 | full-width `２` | absent |

Tags were checked on `mp3`, `flac` and `m4a` files and are read correctly. **Tags in `ogg`, `oga` and `opus` files are never read** by the reference (see Q1): such a file behaves as if it had no tags at all.

## 6. Genre and language

There are two ways a file gets its genre and language, tried in this order.

### 6.1 Explicit folders

The path of the file is taken relative to the inbox. If its first folder is a genre **and** its second folder is a language, both exactly as written in the lists (letter case matters), the file goes there and the artist table is not used. The artist may even be missing.

| Path in the inbox | Result |
|---|---|
| `Rap/FR/song.mp3` | `Rap/FR` |
| `Rap/FR/Some Album/song.mp3` | `Rap/FR` |
| `Rap/song.mp3` (no language folder) | looked up in the artist table, the `Rap` folder is ignored |
| `rap/fr/song.mp3` (wrong case) | looked up in the artist table |
| `Pop/XX/song.mp3` (unknown language) | looked up in the artist table |
| `song.mp3` (directly in the inbox) | looked up in the artist table |

### 6.2 The artist table

The table is `Artists.tsv` in the library, a UTF-8 text file with one artist per line: the artist name, a TAB, then the place as `Genre/Language`.

```
# comment lines start with a hash
Nova	Pop/US
1PLIKÉ140	Rap/FR
```

- A line starting with `#` is ignored.
- A line with fewer than two TAB-separated fields is ignored. A line that uses spaces instead of a TAB is ignored.
- Fields after the second are ignored.
- Nothing is trimmed: spaces before a name, or a Windows line ending (`\r`) after a place, make the line useless (see Q14).

### 6.3 Matching an artist

Two names are compared after **normalisation**: letters are converted to lower case, and accents are removed (`é` becomes `e`). Characters that have no plain ASCII form become `?` (see Q2 and Q3).

The reference computes two names from the artist tag:

- the **full name**: the tag as it is;
- the **first name**: the main artist of the tag (section 7), then cut at the first occurrence of one or more spaces, then `&`, `x` or `with` (lower case only), then a space, up to the end.

Then it searches the table in this order:

1. A line whose name equals the **full name**. If there are several, the **first** line wins.
2. Otherwise, a line whose name equals the **first name**. If there are several, the **last** line wins.

The place of the winning line must be `Genre/Language` with both parts in the lists. If there is no line, the place is invalid, or the artist is empty, the file is **unsorted**: it stays in the inbox, one line is reported, and it is counted. The message always says the artist is missing from the table, even when the line exists with an invalid place (Q15).

Examples, with this table: `Duo A & Duo B` is `Pop/FR`, `Duo A` is `Rap/US`, `Dup` is `Rock/UK` then `Dup` is `Pop/US`, `Nova` is `Pop/US`, `1PLIKÉ140` is `Rap/FR`.

| Artist tag | Result | Why |
|---|---|---|
| `NOVA` | `Pop/US` | letter case ignored |
| `1plike140` | `Rap/FR` | accents ignored |
| `Duo A & Duo B` | `Pop/FR` | the full name wins |
| `Duo A feat. Zed` | `Rap/US` | first name is `Duo A` |
| `Duo A x Duo C` | `Rap/US` | first name is `Duo A` |
| `Duo A with Duo C` | `Rap/US` | first name is `Duo A` |
| `Duo A X Duo C` | unsorted | upper-case `X` is not a separator |
| `Dup` | `Rock/UK` | exact name, first line wins |
| `Dup feat. Q` | `Pop/US` | first-name match, last line wins |
| `Nobody Known` | unsorted | not in the table |
| (no artist tag) | unsorted | nothing to look up |

When a file has both an album artist and an artist, only the album artist is looked up.

## 7. Main artist and artist folder

The folder name uses the **main artist**, so a guest never gets a folder. It is computed from the artist tag (section 5) with two cuts, in this order:

1. Cut from the first match of: one or more spaces, an optional `(` or `[`, then `feat`, `ft` or `featuring` in any letter case, an optional `.`, and a space. Everything from the spaces to the end is removed.
2. Cut from the first `,` or `;`, with the spaces before it, to the end.

Nothing else is cut: `&`, `x` and `with` stay in the folder name (they only matter for the table lookup in section 6.3).

| Artist tag | Folder |
|---|---|
| `Artist feat. X` | `Artist` |
| `Artist (ft. X)` | `Artist` |
| `Artist [feat X]` | `Artist` |
| `Artist Featuring X` | `Artist` |
| `Artist FT. X` | `Artist` |
| `Artist (feat. B) & C` | `Artist` |
| `Artist  feat. Spaces` (two spaces) | `Artist` |
| `Tyler, The Creator` | `Tyler` |
| `Earth, Wind & Fire` | `Earth` |
| `A; B` | `A` |
| `Big Feat Band` | `Big` |
| `Daft Punk` | `Daft Punk` |
| `Artist ft` | `Artist ft` |
| `Artist & Bob` | `Artist & Bob` |
| `Artist x Bob` | `Artist x Bob` |
| `feat. Leading` | `feat. Leading` |
| `Solo (Live)` | `Solo (Live)` |

The result goes through the cleaning of section 9. An empty artist tag gives `Unknown Artist`, and an empty album tag gives `Unknown Album`. The two default names are applied before the cleaning (Q10).

The folder names are used exactly as written. `NOVA` and `Nova` give two different folders (Q8).

## 8. Where the file ends up

```
Music/<Genre>/<Language>/<Artist folder>/<Album folder>/<File name>
```

The album folder is the album tag, cleaned.

**Duplicates.** If a file with the destination name already exists, the new file is not moved. It stays in the inbox, the run reports `already present`, and the duplicate counter goes up. Contents are never compared. The check is exact, so `Song.mp3` and `song.mp3` are two different files on the computer (Q9). When two inbox files map to the same destination in one run, the one met first wins. The same duplicate is reported again at every run, as it stays in the inbox.

## 9. File names

**Cleaning** is applied to the artist folder, the album folder and the title, one at a time, in this order:

1. Each of these characters is replaced by `_`: `/` `\` `:` `*` `?` `"` `<` `>` and the vertical bar.
2. Control characters (code 0 to 31) are removed, not replaced. The delete character (code 127) is kept.
3. Spaces and dots are removed at both ends.
4. The result is cut to 100 characters.

The cut happens last, so a name may end with a space or a dot after it (Q11). The reference counts characters in a UTF-8 locale, and bytes in the `C` locale, where it can cut a character in half (Q3).

| Input | Result |
|---|---|
| `Alb: The Sequel?` | `Alb_ The Sequel_` |
| `Colon:Artist` | `Colon_Artist` |
| `  ..Lead and trail..  ` | `Lead and trail` |
| `tab`, a TAB, `here`, code 1, `ctl` | `tabherectl` |
| `Éléphant Ünïcode ♥ 日本語` | unchanged |
| 120 times `x` | 100 times `x` |
| `...` | empty (Q10) |

**File name**: `[disc-][NN - ]Title.ext`.

- **Title**: the title tag, cleaned. If the tag is empty, the file name without its extension, cleaned.
- **Track**: when a track number is present, the name starts with `NN - `, where `NN` is the number with at least two digits (`5` gives `05`, `100` stays `100`).
- **Disc**: when the disc number is greater than 1 **and** a track number is present, `D-` is added in front of everything, for example `2-03 - Title.mp3`. A disc number is ignored when there is no track number.
- **Extension**: lower case, never cleaned.

| Tags | File name |
|---|---|
| title `One`, track `1` | `01 - One.mp3` |
| title `Two`, track `3/12` | `03 - Two.mp3` |
| title `Zero`, track `0` | `00 - Zero.mp3` |
| title `T`, track `3`, disc `2/2` | `2-03 - T.mp3` |
| title `T`, track `3`, disc `02` | `2-03 - T.mp3` |
| title `T`, track `3`, disc `10/12` | `10-03 - T.mp3` |
| title `T`, track `3`, disc `1/2` or `0` | `03 - T.mp3` |
| title `T`, no track, disc `2` | `T.mp3` |
| no title, track `4`, file `My Song.mp3` | `04 - My Song.mp3` |
| no tags, file `Plain Name.MP3` | `Plain Name.mp3` |

When cleaning leaves nothing, the result is still used as it is: a title `...` with track 8 gives `08 - .mp3`, and without a track gives the hidden file `.mp3`. An album that cleans to nothing puts the file directly in the artist folder. An artist that cleans to nothing puts the album folder directly in the language folder (Q10).

## 10. Cover art and inbox cleanup

**Cover art.** After a file is moved, if its album folder has no `cover.jpg`, the reference looks in the folder the file came from for these names, in this order, and copies the first one it finds as `cover.jpg`: `cover.jpg`, `folder.jpg`, `Cover.jpg`, `Folder.jpg`, `front.jpg`, `Front.jpg`, `AlbumArt.jpg`.

- The names are case-sensitive and only these seven count. `COVER.JPG` and every `png` are not copied.
- An existing `cover.jpg` is never replaced, even by a newer one.
- Art embedded in the tags is not extracted.

**Cleanup.** After sorting, for every folder under the inbox (not the inbox itself):

1. If the folder has no audio file directly inside (audio as defined in section 4), its `jpg`, `jpeg` and `png` files are deleted, whatever their letter case.
2. Then every empty folder is deleted, parents included once their content is gone.
3. Then the folder skeleton of section 2 is created again.

Other files (`txt`, `cue`, `m3u8`...) are never deleted, and a folder that still holds one stays. Audio files that stay in the inbox (unsorted or duplicates) keep their folder and the pictures next to them, with one exception (Q18).

## 11. Report

The reference prints one line for each unsorted file and one for each duplicate, then a summary with three counters: **sorted**, **unsorted** and **duplicates**. TacetOS shows the same information in the app.

## 12. Sync

The reference finds the device by the volume label `Ipod` and mounts it when needed. TacetOS uses its own device detection instead. If no device is found, the run says so and ends normally: the music is already sorted, and nothing is lost.

**What is copied.** Four library folders, in this order: `Music`, `Podcasts`, `Audiobooks`, `Playlists`. Each goes to the folder of the same name on the device, which is created when missing. **Nothing else on the device is read, written or deleted**: the firmware folder, other folders and files at the root are never touched. `To Add`, `Artists.tsv` and any other library entry are never copied.

**When a file is copied.** The copy is recursive and keeps modification times, but not permissions, owner or group. A file is copied when it is missing on the device, when its size differs, or when its modification time differs by **more than 2 seconds** (the 2 seconds are the time resolution of the device file system). The contents are never compared.

| Situation | Copied |
|---|---|
| New file | yes |
| Same size, time differs by 1 or 2 seconds | no |
| Same size, time differs by 3 seconds | yes |
| Same size and same time, other content | no |
| Different size | yes |
| File changed on the device, with another time | yes, the computer version replaces it |

Empty folders are copied too, so the 10 genre folders appear on the device even when empty.

**Mirror mode.** Without it, nothing is ever deleted on the device: a file removed from the library stays there. With it, files and folders are also deleted from `Music`, `Podcasts` and `Audiobooks` on the device when they are not in the library. **`Playlists` is never cleaned**: playlists made on the device are kept, and a playlist deleted on the computer stays on the device.

**Report.** One line per operation, `send` or `del.`, followed by the path relative to the folder, folders included. Then the space used, the total and the free space of the device.

**Eject.** On request, after the copy: unmount the device, then power off its disk. The message "you can unplug" is printed whether or not that worked (Q22). Eject does nothing when the device was not found or when the run is sort-only.

**Run options.** Sort only (stop after step 1), mirror, eject. Any other option is an error and ends with status 1.

## 13. Known quirks and what to do in TacetOS

The reference has the oddities below. Each was seen on a real run. **Keep** means TacetOS reproduces it, **Fix** means TacetOS does better and the matching test records the fixed result, **Decide** means the author chooses before the library code is written.

| # | Behaviour | Proposed |
|---|---|---|
| Q1 | Tags in `ogg`, `oga` and `opus` files are never read. They are stored per stream and the reference only looks at the file level. Such a file gets `Unknown Artist/Unknown Album` and its file name as the title, or stays unsorted | Fix: read stream tags |
| Q2 | Normalisation turns every character with no ASCII form into `?`, so two different names in a non-Latin script of the same length are equal. A Japanese artist not in the table was filed under another one | Fix: remove accents only, compare all other characters as they are |
| Q3 | The result depends on the locale. In the `C` locale every accented letter becomes `?` too (so `1plike140` no longer matches `1PLIKÉ140`), and the 100-character cut counts bytes and can leave an invalid character | Fix: never depend on the locale |
| Q4 | The inbox folder for `R&B` is created as `RB`: the shell treats `&` as a placeholder in the text it builds (seen with bash 5.3) | Fix: create `R&B` |
| Q5 | Files are handled in directory order, so the file kept among duplicates is not predictable | Fix: sort by full path |
| Q6 | An artist name with a comma is cut there: `Tyler, The Creator` goes to `Tyler`, `Earth, Wind & Fire` to `Earth` | Decide |
| Q7 | `feat` is found inside a name: `Big Feat Band` goes to `Big` | Decide: require a dot or a bracket |
| Q8 | `NOVA` and `Nova` make two folders, which collide on a case-insensitive file system such as FAT32 | Fix: reuse an existing folder whatever its letter case |
| Q9 | `Song.mp3` and `song.mp3` are different on the computer and the same on the device | Fix: compare names without regard to case |
| Q10 | A title, album or artist that cleans to nothing is not replaced by its default. Files named `.mp3`, or placed one folder too high, appear | Fix: apply the defaults after cleaning |
| Q11 | The cut to 100 characters can leave a trailing space or dot | Fix: remove them after the cut |
| Q12 | The delete character (code 127) is kept in names | Fix: remove it |
| Q13 | Track `1-2` becomes `12` (all digits are joined) | Fix: use the first run of digits |
| Q14 | A Windows line ending or spaces before a name in the table break that line | Fix: trim each field and accept both line endings |
| Q15 | A line with an invalid place (`Jazz/FR`, `Rap`, `Rock/UK/more`) is reported as "artist missing from the table" | Fix: give a distinct message |
| Q16 | A disc number is ignored when there is no track number | Keep |
| Q17 | A duplicate stays in the inbox and is reported at every run | Keep |
| Q18 | Pictures in a folder are deleted when the folder has no audio of its own, even if a subfolder still holds unsorted audio | Fix: only delete when nothing below holds audio |
| Q19 | `m4b` (audiobooks) is not in the extension list | Decide |
| Q20 | Files are compared by size and time only. A newer file edited on the device is replaced by an older computer file if the times differ | Keep |
| Q21 | Empty genre folders are copied to the device | Decide |
| Q22 | The eject message is printed even when unmounting failed | Fix: report the error |
