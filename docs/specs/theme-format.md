# Theme format

How a TacetOS theme is written, and how a reader must understand it. A theme screen is a small text document. It says where the text and the pictures go on the screen, which information of the player goes where, and what changes when the player changes (a song starts, the battery charges, a button is pressed). This spec is meant to be enough to write the theme reader and the preview of the theme editor without having seen any other implementation. It is about reading and meaning only. Where the files of a theme live, how fonts and pictures are found, the settings file of a theme and the screen sizes of each model are the second half of this spec (sections 10 to 14).

The same syntax is used by the firmware, which has to keep reading the themes that people already made. The syntax is therefore fixed. This spec describes it, it does not invent it. When the firmware and this spec disagree, the firmware is right and this spec has a bug.

**Order to write the code.** First the scanner and the parser (sections 2 to 4), with the fixtures of section 9 and no screen at all. Then the viewport and line layout (sections 5 and 6). Then the evaluation of conditions (section 7) with a fake player state. Then the tag table (section 8), family by family. Then the files of a theme (sections 10 to 13), with the fixtures of section 14. Drawing comes last.

## 1. Words used here

- **Theme screen**: one text document, for one screen of the player (playback, status bar, file lists...). A theme is made of several of them, and of the files they point to.
- **Text**: characters of the document that are shown as they are.
- **Tag**: a `%` followed by a short name, and optionally an argument list in parentheses. A tag stands for a value of the player (`%it` is the title of the current song) or for an instruction (`%ac` means "center what follows").
- **Argument**: one value in the parentheses of a tag, separated from the next by a comma.
- **Condition**: a tag that chooses between several pieces of the document, written `%?tag<first|second|...>`.
- **Branch**: one piece between the `<`, the `|` and the `>` of a condition.
- **Line**: what the document writes between two line breaks. One line is one row of text on the screen.
- **Subline**: one of the parts of a line that contains `;`. Only one subline is shown at a time.
- **Viewport**: a rectangle of the screen with its own font and colors. A theme screen is a list of viewports.
- **Value of a tag**: what the tag gives when the player is asked: a piece of text, which may be empty, and for some tags also a number (section 7).
- **Reader**: the code of TacetOS that reads a theme screen (the preview). The **firmware** is the program on the iPod that reads the same documents.

## 2. The document

A theme screen is UTF-8 text. A reader accepts CR LF as a line break as well as LF, and ignores one byte order mark at the very start. The last line does not need a line break at its end.

A document is a list of **viewports**, one after the other. A viewport starts with a viewport tag (section 5) and goes up to the next viewport tag, or to the end of the document. Everything between is the content of the viewport: a list of lines.

If the document does not start with a viewport tag, the first viewport is **implicit**: it covers the whole screen, uses the interface font and the colors of the player. An implicit viewport has the default label (section 5.3).

Each line of a viewport is either a plain line or a line with sublines (section 6.3). Lines are shown in order from the top of the viewport.

## 3. Characters with a meaning

### 3.1 The special characters

| Character | Where it means something | Meaning |
|---|---|---|
| `%` | everywhere | Starts a tag, a condition (`%?`) or an escape (section 3.2) |
| `#` | everywhere | Starts a comment (section 3.3) |
| `;` | at the level of a line | Separates the sublines of a line |
| `(` `)` | after a tag name | Open and close the argument list |
| `,` | in an argument list | Separates two arguments |
| `<` `>` | after a condition tag | Open and close the branches |
| `\|` | in the branches | Separates two branches |
| `-` | as a whole argument | The "default" value (section 4.3) |
| `?` | right after `%` | Makes the tag a condition |

Outside of an argument list and of the branches of a condition, only `%`, `#` and `;` are special in text. A `,`, a `)`, a `|` or a `>` is plain text there. Inside a branch or an argument list they end the piece of text. To be safe, a writer escapes all of them wherever they are meant as text.

### 3.2 Escapes

A `%` followed by one of the nine characters `% ( , ) ; # < | >` stands for that character as plain text. So `%%` is a percent sign, `%;` a semicolon, `%#` a hash sign. This also works inside a string argument.

A `%` followed by anything else is the start of a tag. If no tag has that name, the document is wrong (section 4.6).

### 3.3 Comments

A `#` starts a comment that runs up to the end of the line. The comment, **with its line break**, is dropped. Comments are allowed everywhere a character can be: between lines, at the end of a line, inside an argument list, inside a condition.

Because the line break goes with the comment, a comment at the end of a line that also has other content joins the next line to the current one. Comments at the start of a viewport are skipped before the first line is read. A theme writer therefore puts a comment on a line of its own, and the editor never writes one at the end of a line. See section 15 for the check to do with the real firmware.

### 3.4 Spaces and line breaks

- Tab characters at the very start of a line are skipped (to allow indentation). Spaces are never skipped there: a space at the start of a line is text.
- Inside an argument list, spaces and tabs before an argument are skipped. The spaces at the end of a string argument are part of the string.
- Right after the `<` of a condition and right after a `|` between branches, one line break is skipped. This lets a long condition be written on several lines. A branch itself cannot contain a line break.
- An empty line is a line without content. It is still a row: it makes a blank row on the screen (section 6.1).

## 4. Tags

### 4.1 Finding the tag

After the `%` the reader looks for the longest tag name that matches, trying three characters first, then two, then one. Names are case sensitive (`%al` and `%aL` are different tags). A name is never longer than three characters. Tag names are listed in section 8.

### 4.2 The argument list

Every tag has a **signature** that says which arguments it takes. The signature is a string of letters, one per argument:

| Letter | The argument is | Written as |
|---|---|---|
| `I` | an integer | `12`, `-4` |
| `P` | a percentage | `50%`, `12.5%` (one decimal at most) |
| `D` | a decimal number | `5`, `0.5` (one decimal at most) |
| `S` | a string | any text up to the next `,` or `)` |
| `F` | a file name, written like a string | `cover.bmp` |
| `C` | code: text and tags, like a branch | `%it - %ia` |
| `T` | exactly one tag, written with its `%` | `%mp` |

A lowercase letter means that the argument may also be written `-` (the default value). An uppercase one means it may not. Other signs in a signature:

- `[ ]` around several letters: the argument may be any of those kinds, and the reader picks one from what is written (section 4.4).
- `|`: everything after this sign is optional.
- `*` after an argument or a bracketed group: it can be repeated as many times as the writer wants.
- `?` at the very start: the tag may be written with no parentheses at all. If it has parentheses, all the required arguments must be there.

Percentages are stored in tenths of a percent (`12.5%` is 125). Decimal numbers are stored in tenths too (`0.5` is 5, `5` is 50). A percentage is a fraction of the size the tag works in: of the width of the viewport for a horizontal value, of its height for a vertical one, of the whole screen for the position of a viewport.

A tag with no arguments in its signature is written without parentheses. If the signature starts with an optional argument (`|D`), the parentheses may be left out too.

### 4.3 The default value

An argument written `-` (alone, not followed by a digit) means "use the usual value". It is allowed only where the signature gives a lowercase letter. A `-` followed by a digit is a negative number.

### 4.4 Choosing the kind of a bracketed argument

When an argument may be of several kinds, the reader tries these rules in order and takes the first that fits:

1. The text is `-` and some kind of the group is lowercase: the default value.
2. The text has only digits, `.` and `-`, contains a `.`, and `D` or `d` is allowed: a decimal.
3. The text has only digits, `.`, `-` and `%`, ends with `%`, and `P` or `p` is allowed: a percentage.
4. The text has only digits, `.`, `-` and `%`, and `I`, `i`, `D` or `d` is allowed: an integer (or a decimal if only that is allowed).
5. The text contains a `%` and `T` or `C` is allowed: a tag, or code.
6. `S` or `s` is allowed: a string.
7. Otherwise: the argument is wrong.

### 4.5 Strings, numbers and tags as arguments

- A string argument ends at the next `,` or `)`. A `%` inside it escapes the next character. A line break inside it is an error.
- An integer argument must start with a digit or `-`.
- A tag argument (`T`) is a tag written in full with its own `%`, and with its own arguments if it takes any: `%if(%pc,>,30)`.
- A code argument (`C`) can hold text, tags and conditions.

### 4.6 What is wrong

The reader refuses the whole theme screen, and says the line, when it meets one of these:

- a tag name that does not exist;
- a tag that needs arguments but has no parentheses;
- more arguments than the signature allows, or fewer than it needs;
- `-` where the default value is not allowed;
- an integer or a decimal that is expected and not there;
- a missing `,` between arguments, or a missing `)` or `>` at the end;
- a line break inside a string argument;
- a condition without `<`.

On top of this, later checks can refuse a tag whose arguments are readable but wrong, for example a viewport that does not fit in the screen (section 5.2), an image number used twice, a font number outside 2 to 13 or a font file that does not end in `.fnt` (sections 8.7 and 10.3), a color that is not six hexadecimal digits, or a setting name that does not exist. The firmware then drops the theme screen and uses the built-in one. The preview shows the error and the line instead.

## 5. Viewports

### 5.1 The viewport tags

A viewport tag starts with `%V` followed by `(`, `l` or `i`. It ends the previous viewport wherever it is written. It is good style to write it at the start of a line, and a line break right after it is skipped.

| Tag | Signature | Meaning |
|---|---|---|
| `%V(x,y,w,h,font)` | `[IP][IP][ip][ip]i` | A viewport that is always shown |
| `%Vl(label,x,y,w,h,font)` | `S` then the same | A viewport with a label, **hidden** until a `%Vd` of its label is read (section 5.4) |
| `%Vi(label,x,y,w,h,font)` | `s` then the same | The rectangle where the player draws its own list (file browser, menus) on screens that have one. The label may be `-` |

### 5.2 Position, size and font

- `x` and `y` are the top left corner. A negative value counts from the right edge (for `x`) or from the bottom edge (for `y`). So `-10` is ten pixels before the edge.
- `w` and `h` are the width and the height. A negative value means "up to that far from the edge": `-10` stops ten pixels before the edge. `-` as a width means "up to the right edge", as a height "up to the bottom edge".
- A percentage is a fraction of the width of the screen for `x` and `w`, and of its height for `y` and `h`.
- The font is a number: `1` is the interface font of the player (and the default), `2` to `13` are fonts loaded by `%Fl` (sections 8.7 and 10.3). `0` is a fixed font built into the firmware that themes should not use (section 15).
- After all this, the rectangle must be inside the screen. If not, the theme screen is wrong.

### 5.3 The default viewport

A document that starts with no viewport tag has an implicit first viewport (section 2). Its label is the default label, which is the single character `|` (written `%|` in a document, because a bare `|` would end an argument). It is always shown.

### 5.4 Showing a hidden viewport

A viewport made with `%Vl` is not drawn. It is drawn while a `%Vd(label)` with its label is read in a branch that is currently chosen. When the branch is no longer chosen, the viewport is hidden again and its area is cleared. The usual way to write it:

```
%?mp<%Vd(stopped)|%Vd(playing)|%Vd(paused)>
```

`%VI(label)` is the same for the viewports made with `%Vi`: it selects which list rectangle the player uses.

### 5.5 Colors and text style of a viewport

These tags change the viewport that contains them. The change holds for the lines after it.

| Tag | Meaning |
|---|---|
| `%Vf(color)`, `%Vb(color)` | Foreground and background color. `-` goes back to the colors of the player |
| `%Vs(mode[,arg])` | Text style: `invert`, `clear`, `color,color` (text in a color), `gradient[,n]` (a color gradient across `n` lines) |
| `%Vg(start,end[,text])` | The two colors of a gradient, and optionally the color of the text on it |
| `%VB` | Draw the content of this viewport on the background picture instead of on the screen |

A **color** is written as six hexadecimal digits, `RRGGBB`, without any sign before them (a `#` would be a comment).

## 6. Lines

### 6.1 One line, one row

Each line of the document is one row of text in the viewport, counted from the top, each as high as the font. A line is built from its text, the values of its tags and the branches chosen by its conditions, put end to end. A line without any content is a blank row.

Some tags do not take a row of their own: when a line holds one of them, the line does not count as a row, so that the next line is drawn at the same height. This holds even if the line also has text: that text is drawn at the row, and the next line starts on the same row. These are `%xl`, `%x`, `%X`, `%Vp`, `%Vf`, `%Vb`, `%Vg`, `%VB`, `%we`, `%wd`, `%wi`, `%Fl`, `%Cl`, and `%T`. The usual case is a line that only loads a picture or sets a color.

### 6.2 Alignment

`%al`, `%ac` and `%ar` (and the mirrored `%aL`, `%aR` and `%ax`, section 8.1) cut a line into a left part, a center part and a right part. The text after an alignment tag goes to that part until the next alignment tag. A line starts left aligned. The parts are placed at the left edge, in the middle and at the right edge of the viewport. When two parts would touch or overlap, they are merged into one text with a space between them, drawn where the part it was joined to would be: the center part joins the left one if it would touch it, or else the right one if it would touch that. A center part that does not fit on its own moves to the left. When the left and right parts overlap, they are merged in the same way.

### 6.3 Sublines

A line that contains a `;` (not escaped, not inside a tag's parentheses) is a **line with sublines**: `first;second;third`. Only one subline is shown at a time, the others take its place one after the other, in a loop. Each subline is shown for two seconds by default.

- `%t(seconds)` at the start of a subline sets how long it stays. `seconds` is a decimal: `%t(5)`, `%t(0.5)`. `%t(show,hide)` shows it for `show` seconds then hides it for `hide` seconds before it can be shown again.
- A subline with no content is skipped. A subline whose time is zero (set through a condition, for example) is skipped too.
- The sublines of a line take one row in all.

Sublines may also be used inside a branch of a condition.

### 6.4 Scrolling

`%s` anywhere in a line makes the whole line scroll sideways when it is wider than the viewport. Without `%s` a long line is cut at the edge.

## 7. Conditions

### 7.1 The shape

A condition is written `%?tag<branch1|branch2|...>`. The tag can have arguments, which go between the name and the `<`: `%?St(shuffle)<on|off>`. A condition is part of a line: its chosen branch is put into the line in place of the whole condition. Conditions can be nested. A branch can hold text, tags, conditions, and sublines. After the closing `>` the line goes on.

A condition with only one branch is allowed (`%?bc<charging>`). A condition with no branch at all is not.

### 7.2 Which branch is chosen

The reader asks the tag for its **value** and the number `n` of branches. The tag gives a text (possibly empty) and, for some tags, a number `k`. Then:

1. If the tag gave a number `k`: branch `k` is chosen, counting from 1. If `k` is below 1 or above `n`, the **last** branch is chosen.
2. If the tag gave no number (a presence tag): if the text is not empty, the **first** branch is chosen. If the text is empty, the **last** branch is chosen, **except** when there is only one branch, in which case nothing is shown.

So `%?it<%it|unknown>` shows the title if there is one, and `unknown` if not. With three branches `<a|b|c>`, a presence tag chooses `a` or `c`, never `b`.

Section 8 tells for each tag whether it is a presence tag (**P**), a number counted from 1 (**E**, a choice among options), a quantity spread over the branches (**Q**), or a mix. Quantity tags spread their range over the `n` branches, from the lowest value in the first branch to the highest in the last. The exact spread of each Q tag (volume, battery, progress, signal) is not written yet (section 15): it will come with fixtures made from the firmware simulator.

### 7.3 Compile time conditions

A few tags ask about what the iPod can do rather than what it is doing now. They are marked **F** in section 8 (`%?cc`: has a clock, `%?tp`: has a radio, `%?Tp`: has a touch screen, `%?Rp`: can record). They are decided once, when the theme screen is read. The first branch is kept if the feature exists and the second one (if any) is dropped, and the other way round if not. A condition with one branch on a missing feature shows nothing.

A reader needs to know which of these four features the chosen model has. They are not in `models.toml` yet (section 15). The preview works for one model at a time.

### 7.4 The `%if` tag

`%if` turns a value into a yes or no, and so can be used as the tag of a condition: `%?if(%pc,>,30)<late|early>`. Its signature is `TS[ITS]|D`:

```
%if(tag, operator, operand [, options])
```

- `tag` is a tag, written with its `%`.
- `operator` is one of `=`, `==`, `!=`, `>`, `>=`, `<`, `<=` (a string argument).
- `operand` is what the tag is compared with: an integer, a string, or another tag.
- `options` (optional) is a number of branches that the value of the tag is spread on, as if it was the tag of a condition with that many branches. It is parsed as a decimal, and so is stored as ten times the number written. See section 15.

How the left side is taken:

- If the tag gives a number, that number is used. Quantities (a time in seconds, a battery level in percent) give their real value when no `options` is given, and a choice (play state, repeat mode) gives its position counted from 1.
- If the tag gives no number, it counts as 1 if its text is not empty and as 0 if it is.

How the right side is taken:

- A number: the number as written.
- A string: the text of the tag is compared with it as text (equal or not equal only make sense).
- A tag: the two texts are compared as text for `=` and `!=`. For the other operators the right side counts as 1 if it is not empty, 0 if it is.
- `-`: stands for -1.

The result of `%if` is a presence value: true when the comparison holds, so that `<yes|no>` works as expected.

### 7.5 Combining: `%and` and `%or`

`%and(tag,tag,...)` is true if the text of every tag is not empty. `%or(tag,...)` is true if the text of at least one is. They are used as the tag of a condition: `%?and(%bc,%bu)<charging over USB|other>`. They take one or more tags.

### 7.6 Cutting a text: `%ss`

`%ss(start,length,tag[,number])` gives a piece of the text of a tag, counted in characters (not bytes). `start` below zero counts from the end. A `length` of `-` or too long gives everything up to the end. If the last argument is the word `number` and the piece is made of digits, the piece also gives its value plus one as a number, so that it can drive a condition: `%?ss(0,1,%cH)<...>` chooses on the first hour digit.

## 8. The tags

This section lists every tag the reader must know. The signature is shown when there is one. In the last column of the condition tags: **P** presence, **E** a choice counted from 1, **Q** a quantity, **F** compile time (section 7.3), **-** not meant for conditions (the text is always there, so it picks the first branch).

Some tags of 8.2 have a twin for the **next** track instead of the current one: `%Ia` to `%IC` (twins of `%ia` to `%iC`), `%Fb`, `%Fc`, `%Ff`, `%Fk`, `%Fm`, `%Fn`, `%Fp`, `%Fs`, `%Fv` (twins of the `%f` ones), `%D(n)` (twin of `%d(n)`) and `%Pt` (twin of `%pt`). They are not repeated in the tables. Do not mix up `%Fl`, which loads a font (section 8.7).

### 8.1 Layout

| Tag | Meaning |
|---|---|
| `%al` `%ac` `%ar` | Left, center, right aligned text (section 6.2) |
| `%aL` `%aR` | Left and right, but swapped in languages written from right to left |
| `%ax` | The next positioned thing (a bar, a viewport) is mirrored in right to left languages |
| `%s` | The line scrolls (section 6.4) |
| `%t(seconds[,hide])` | How long a subline stays (section 6.3) |
| `%cs` | Number of the screen being shown (section 8.9) |

### 8.2 The current track

| Tag | Meaning | Cond |
|---|---|---|
| `%ia` `%ic` `%id` `%iA` | Artist, composer, album, album artist | P |
| `%iG` `%ig` `%ik` `%in` | Grouping, genre, disc number, track number | P |
| `%it` `%iv` `%iy` `%iC` | Title, tag version, year, comment | P |
| `%fb` `%fc` `%ff` `%fk` `%fv` `%fs` | Bit rate, codec, frequency in Hz, frequency in kHz, variable rate mark, size | P (`%fc`: E) |
| `%fm` `%fn` `%fp` | File name with its extension, without it, full path | P |
| `%d(n)` | The name of the folder `n` levels up from the file (1 is the folder holding it) | P |
| `%ft(file[,line\|text])` | A piece of a text file: the given line, or the line that starts with the given text | P |
| `%pc` `%pr` `%pt` | Time played, time left, length of the track. Shown as `m:ss` | Q |
| `%px` | Percentage played | Q |
| `%pS[(n)]` `%pE[(n)]` | True during the first, or the last, `n` seconds of the track (10 if left out) | P |
| `%rp` `%rr` `%ra` | Play count, rating, automatic score. As a number: the value plus one | E |
| `%rg` | Replay gain mode in use | P |
| `%C` | True if there is cover art for the track | P |

### 8.3 Playlist, state and sound

| Tag | Meaning | Cond |
|---|---|---|
| `%mp` | Play state: 1 stopped, 2 playing, 3 paused, 4 fast forward, 5 rewind, 6 recording, 7 recording paused, 8 radio, 9 radio paused | E |
| `%mm` | Repeat mode: 1 off, 2 all, 3 one, 4 shuffle, 5 A to B | E |
| `%ps` | True if shuffle is on | P |
| `%pp` `%pe` `%pn` | Position in the playlist, number of entries, name of the playlist | P |
| `%pv[(bar)]` | The volume, in dB as text, or as a bar if the bar arguments are given | Q |
| `%xf` | Crossfade mode | E |
| `%Sp` `%Ss` | Pitch and speed, in percent | Q |
| `%mh` `%mr` | True if the hold switch of the player, of the remote, is on | P |
| `%mv[(n)]` | True during `n` seconds (10 if left out) after the last volume button | P |
| `%lh` | True while the disk is busy | P |

### 8.4 Battery and power

| Tag | Meaning | Cond |
|---|---|---|
| `%bl[(bar)]` | Battery level in percent, as text or as a bar | Q |
| `%bv` `%bt` `%bs` | Voltage, time left, time before the sleep timer stops the player | P |
| `%bc` | True while charging | P |
| `%bp` | True while a charger is plugged in | P |
| `%bu` | True while a USB cable is plugged in | P |

### 8.5 Clock

Clock tags give the same letters as the usual date formats of the C library. They give `--` (`----` for a four digit year, `---` for names, `-` for a weekday number) when the player has no clock.

| Tag | Meaning |
|---|---|
| `%cc` | True if the player has a clock (**F**, section 7.3) |
| `%cf` | The 12 or 24 hour setting, as a choice counted from 1 (**E**) |
| `%cd` `%ce` | Day of the month, with a zero or with a blank before one digit |
| `%cH` `%ck` | Hour 0 to 23, with a zero or with a blank before one digit |
| `%cI` `%cl` | Hour 1 to 12, with a zero or with a blank before one digit |
| `%cm` `%cM` `%cS` | Month, minute, second |
| `%cy` `%cY` | Year on two digits, on four digits |
| `%cP` `%cp` | AM or PM in capitals, am or pm in small letters |
| `%ca` `%cb` | Name of the weekday, name of the month (in the language of the player) |
| `%cu` `%cw` | Weekday as a number, counted from Monday and from Sunday |

The number tags of this table give their number as the value for conditions (the month gives 1 to 12). `%cw` and `%cf` give their number plus one, so that they count from 1.

### 8.6 Radio and recording

These only matter on players that have them (section 7.3). On the iPods of this project the recording tags are always false and the radio tags depend on the model.

| Tag | Meaning |
|---|---|
| `%tp` | True if there is a radio (**F**) |
| `%tt` `%tm` `%ts` | True if tuned, in scan mode, in stereo |
| `%ta` `%tb` `%tf` | Lowest, highest and current frequency |
| `%tr[(bar)]` `%tl` `%th` | Signal strength (as a bar or a quantity), and its lowest and highest values |
| `%Ti` `%Tn` `%Tf` `%Tc` | Preset: number, name, frequency; and the number of presets |
| `%tx` `%ty` `%tz` | True if the station sends RDS text; its name; its text |
| `%Rp` | True if the player can record (**F**) |
| `%Rr` `%Rf` `%Re` `%Rb` `%Rm` | Recording: in progress, frequency, encoder, bit rate, mono |
| `%Rs` `%Rn` `%Rh` | Recording time: seconds, minutes, hours |

### 8.7 Pictures, fonts and drawing

| Tag | Signature | Meaning |
|---|---|---|
| `%xl(id,file[,x,y[,n]])` | `SF\|[IP][IP]I` | Loads a picture without showing it. `id` is a name of one or two letters. `n` is the number of images stacked in the file (section 8.7.1) |
| `%x(id,file,x,y[,n])` | `SF\|II` | Loads a picture and shows it where it is |
| `%xd(id[,sub[,offset]])` | `S\|[IT]I` | Shows a loaded picture. `sub` is the number of the image in a stack, counted from 1, or a tag whose value gives it. `offset` is added to it |
| `%x9(id)` | `S` | Shows a picture as a frame stretched to the whole viewport |
| `%X(file)` | `f` | The background picture of the screen. `-` for none |
| `%dr(x,y,w,h[,color[,color2]])` | `[IP][IP][ip][ip]\|ss` | A filled rectangle. Two colors make a gradient from the first to the second |
| `%Fl(id,file[,glyphs])` | `IF\|I` | Loads a font as number `id` (2 or more). The file name must end in `.fnt` |
| `%Cl(x,y,w,h[,align...])` | `[iP][iP][iP][iP]\|ss` | The area for the cover art of the playing track |
| `%Cd` | none | Draws the cover art loaded by `%Cl` |

#### 8.7.1 Stacks of images

A picture file may hold several images stacked one above the other, all of the same size. This is how a theme gets an icon with several states or a bar with several fills: the `n` of the load tag says how many there are, and the height of each is the height of the file divided by `n`. `%xd` picks one. If the file name is the special name `__list_icons__`, the picture is the set of list icons of the player instead of a file.

#### 8.7.2 Bars

`%pb`, `%pv`, `%bl`, `%pL`, `%pR`, `%tr`, `%LB` and the setting form of `%St` are drawn as **bars**. They share the signature `?[iP][iP][iP][iP]|s*`: with no arguments, a bar in the usual place at the usual size. With arguments, `x`, `y`, `width` and `height` (each `-` for usual) and then any number of words:

| Word | Meaning |
|---|---|
| `invert` | Fills from the other end |
| `nofill`, `noborder`, `nobar` | Do not draw the fill, the border, the whole bar |
| `vertical`, `horizontal` | Direction. By default horizontal if wider than high |
| `image,file` | Draw the bar from a picture file |
| `slider,file` | Draw a slider picture on top of the bar |
| `backdrop,file` | Draw a picture under the bar |
| `notouch` | The bar does not react to touch |
| `setting,name` `soffset,n` | For bars that show a player setting |

The bar must fit inside the viewport: `x` and `y` inside it, `width` above zero and `x + width` not past the right edge, `height` above zero and not past the bottom edge, otherwise the theme screen is wrong.

### 8.8 Player settings, text of the player and variables

| Tag | Signature | Meaning | Cond |
|---|---|---|---|
| `%St(name)` | `[Sip]\|[ip][ip][ip]s*` | The value of a player setting by its name. With more than four arguments, a bar for it | by setting |
| `%Sx(text)` | `S` | A text of the player, in the language of the player. The text is the English one | - |
| `%Sr` | none | True if the language is written from right to left | P |
| `%vs(name,op,value[,max])` | `SSi\|I` | Changes a variable: `op` is `set`, `inc`, `dec` or `touch` | - |
| `%vg(name)` | `S` | The value of a variable | E |
| `%vl(name[,seconds])` | `S\|D` | True during `seconds` (10 if left out) after the variable last changed | P |

A setting in a condition: a yes/no setting chooses branch 1 for yes and 2 for no. A setting with choices chooses branch `k` for choice `k`. A text setting chooses 1 if it is not empty and 2 if it is. A sound setting without decimals counts from its lowest value. Settings with decimals cannot be used.

### 8.9 Screens, status bar, lists and touch

| Tag | Meaning |
|---|---|
| `%cs` | The current screen as a number counted from 0: unknown, main menu, playback, recording, radio, playlist viewer, settings, file browser, database browser, plugin browser, quick screen, pitch screen, option select, playlist browser, plugin, context menu, system screen, time screen, bookmark list, shortcut menu, track info, USB screen |
| `%we` `%wd` | Turn on, turn off the built-in status bar for this screen |
| `%wi` | Draw the built-in status bar in this viewport |
| `%Lt` `%Li` | Title of the list being drawn, and its icon |
| `%LT[(col,text)]` `%LI[(col,icon)]` | Text and icon of the list row being drawn |
| `%LR` `%LC` `%LN` `%Lc` | Row, column, number of the list item being drawn, and true if it is the selected one |
| `%Lb(viewport,w,h[,...])` | Sets the size of one list item |
| `%LB[(bar)]` | A scroll bar, true when the list needs one |
| `%Vp(offset,code)` | One entry of the playlist, `offset` entries from the current track, drawn with `code` as its text |
| `%QT` `%Qt` `%QR` `%Qr` `%QB` `%Qb` `%QL` `%Ql` | Name and value of the quick screen setting at the top, right, bottom, left |
| `%T(...)` `%Tl` `%Tp` | Touch screen regions and the last touch; true if the player has a touch screen (**F**) |

The target iPods have no touch screen. A reader still has to accept the touch tags and ignore them, so that themes made for other players load.

## 9. What the tests need

Pure functions first, with no screen:

- **Scanner and parser**: from a string to a tree of lines, tags, texts and conditions, and from a bad string to an error with a line number. At least one fixture for each rule of sections 3 and 4: every escape, a comment between lines, a comment at the end of a line, each kind of argument, each way to be wrong.
- **Branch choice**: from a tag value, a number (or none) and a number of branches to a branch (section 7.2). A table of cases: presence with 1, 2 and 3 branches, empty and not empty; a number below 1, inside, and above `n`.
- **`%if`**: each operator against an integer, a string, and another tag; with and without the `options` argument.
- **Layout**: the viewports of section 5.2 on a screen of a given size: negative values, percentages, defaults, and one that goes off the screen.
- **Rows**: which lines take a row (section 6.1), and the merge of the three alignment parts (section 6.2).
- **Sublines**: which subline is shown at a given time with a fake clock (section 6.3).

A few small documents are written by hand and checked into the repository with the expected tree. Documents from real themes are not checked in. They are kept outside the repository and the tests read them from a folder that the developer points to, when it is there.

Examples that the fixtures must contain:

```
%V(0,0,-,-,1)
%ac%it
%ar%?bc<charging|%bl>
```

A small playback screen: the title centered on the first row, and on the second row the battery level, or the word `charging`, at the right.

```
%Vl(menu,10%,10%,80%,80%,-)
%?bu<%Vd(menu)>
```

A hidden viewport that is shown while a USB cable is plugged in.

```
%ac%it %zz
```

A wrong document: `zz` is not a tag, so the reader reports line 1.

```
%t(3)%ia;%t(1)%id
```

A line with two sublines: the artist for three seconds, then the album for one second, in a loop.

## 10. Where the files of a theme live

### 10.1 The data folder

The player keeps everything a theme needs in one **data folder** at the root of its music disk, with one sub-folder per kind of file. The name of the data folder is a technical name that the firmware looks for. It is the firmware folder of `detection.md`. Its name is written once, in the one constants file of the app (as in `bootloader-classic.md` section 9), and this spec calls it `<data>`.

| Kind | Folder | Extension | Chosen by |
|---|---|---|---|
| Playback screen | `<data>/wps` | `.wps` | the key `wps` |
| Status bar | `<data>/wps` | `.sbs` | the key `sbs` |
| Radio screen | `<data>/wps` | `.fms` | the key `fms` |
| Pictures of a screen | `<data>/wps/<name>/` | `.bmp` | the tags of the screen |
| Settings of a theme | `<data>/themes` | `.cfg` | the user |
| Fonts | `<data>/fonts` | `.fnt` | the key `font` and the tag `%Fl` |
| Icon sets | `<data>/icons` | `.bmp` | the keys `iconset` and `viewers iconset` |
| Backdrops | `<data>/backdrops` | `.bmp` | the key `backdrop` (section 10.4) |
| File type colors | `<data>/themes` | `.colours` | the key `filetype colours` |

The three kinds of screen share one folder. The remote variants (`.rwps`, `.rsbs`, `.rfms`) exist for players with a second screen on a wired remote. No target model of TacetOS has one, so the editor ignores them and the reader accepts the files without drawing them.

### 10.2 The pictures of a screen

A picture named in a tag (`%x`, `%xl`) is looked for in a folder next to the screen file, **with the same name as the file and no extension**. The screen `<data>/wps/night.wps` finds its pictures in `<data>/wps/night/`. The name in the tag is added to that folder as it is, with its `.bmp`. The editor writes plain names with no folder in them. What the firmware does with a name that has a slash was not tried.

A picture is a BMP file. The firmware asks the loader for any format, with transparency, so the depth of the file does not matter and the loader converts it to the screen. The color **magenta** (red 255, green 0, blue 255) is transparent: the pixel lets what is under it show. A second color, **cyan** (0, 255, 255), is defined as "replace by the foreground color of the viewport". Whether it works for every picture was not tried (section 15).

If several tags name the same file, it is read once and they share it. A file with a stack of images (section 8.7.1) is cut by the number `n` of **each** tag that names it, so two tags may cut one file in two different ways.

The special name `__list_icons__` does not point to a file. It means the icon set of the player (section 10.5), and it counts as one stack with one image for each icon that a theme can change.

### 10.3 Fonts

`%Fl(id,file[,glyphs])` gives a font the number `id`. The file name must end in `.fnt` and is looked for **only in the fonts folder**, never in the folder of the screen as pictures are.

- The number `1` is the font of the player's interface, `0` is a fixed font built into the firmware that themes should not use (sections 5.2 and 15), and the fonts of a theme are numbered from `2`.
- The firmware has room for 12 fonts, so the numbers are 2 to 13. Beyond that the tag is wrong.
- `glyphs` is how many characters the firmware makes room for in memory. Without it, the value of a setting of the player is used.
- The table of fonts looks shared by every theme screen, so two screens of one theme must not give the same number to two different fonts. This was read, not tried (section 15).
- The key `font` of the settings file (section 11) names the font of the interface without the folder and without the `.fnt`. It is loaded before the screens.

### 10.4 Backdrops

A backdrop is a picture as big as the screen, drawn under everything. It is used on every model of TacetOS, since all of them have more than one bit of depth. In a screen, `%X(file)` names it, looked for like a picture (section 10.2). A screen that did not come from a file (the one built into the firmware) looks for it in the backdrops folder instead.

`%X(-)` means that the screen has no backdrop of its own. In that case, if the settings of the player name one, that one is shown. The same file used by several screens is kept once in memory. The firmware keeps room for exactly one screen of pixels per backdrop, so a backdrop has the size of the screen of the model. A different size was not tried.

### 10.5 Icons and file type colors

An **icon set** is a BMP file in the icons folder, with the icons stacked in it (section 8.7.1). There are two keys: `iconset` for the lists and the menus, `viewers iconset` for the icons of the files shown in the browser. The value `-` means the icons built into the firmware. The editor does not draw its own icons, it only copies the files.

The file of **file type colors** is a text file in the themes folder with the extension `.colours`. Its format was not read in this step, and the key is left out of what the editor writes (section 15).

### 10.6 When a file is missing

If one picture of a screen cannot be read, or one font cannot be loaded, the **whole screen is dropped**: the firmware reports the error (for a font, with the name of the font) and does not use the screen. It does not draw the rest of it without the picture. The reader does the same: a missing file is an error of the theme, with the name of the file, and the preview shows the error instead of a half drawn screen.

What the firmware shows in place of a dropped screen was not read.

## 11. The settings file of a theme

### 11.1 The shape of the file

A settings file is a text file with one setting per line, written `name: value`.

- Spaces before the name are skipped. A line whose first character, once those spaces are skipped, is `#` is a comment.
- The name is everything before the **first** colon, spaces included: `wps : night` has the name `wps `, which the player does not know. A line with no colon is ignored.
- Spaces after the colon are skipped. Spaces at the end of the value are removed. The value is the rest of the line, so it may contain colons.
- A name that the player does not know, and the order of the lines, do not matter for the lines that are known. What the player does with an unknown name was not tried.
- The firmware reads a line in a buffer of 128 bytes. The editor keeps its lines under 120 bytes. What happens with a longer line was not tried.

The player writes its own files with the name `theme` and a number, in the themes folder, when the user saves the theme.

When a file changes any setting that belongs to the theme (the files of the screens, the font, the backdrop, the icons, the colors), the player reloads all its theme screens after reading it.

### 11.2 The keys

These keys were read in the firmware. Every key is optional.

| Key | Value |
|---|---|
| `wps`, `sbs`, `fms` | The playback screen, the status bar and the radio screen. A name, or `-` for the built-in one |
| `font` | The font of the interface, a name without the `.fnt` (section 10.3). `-` for the built-in one |
| `backdrop` | A backdrop, with its folder and its `.bmp` (section 10.4). `-` for none |
| `iconset`, `viewers iconset` | An icon set, a name, or `-` (section 10.5) |
| `filetype colours` | A name of a file in the themes folder, or `-` |
| `statusbar` | `off`, `top` or `bottom` |
| `selector type` | `pointer` or `bar (inverse)`; with a color screen also `bar (color)` and `bar (gradient)` |

The theme builder of the firmware also writes these keys, for color screens: `foreground color`, `background color`, `line selector start color`, `line selector end color`, `line selector text color`, `list separator color` and `list separator height`. A color is six hexadecimal digits, red then green then blue (`B6C6E5`). It also writes `ui viewport` (the part of the screen where the lists are drawn, `-` for the whole screen) and `show icons`. The syntax of the values of `ui viewport` was not read.

The files of the screens appear in two ways in the settings files seen so far: a bare name (`wps: night`) as the player stores it, and a whole path with the extension (`wps: <data>/wps/night.wps`) as the theme builder writes it. The editor writes the whole path. How the firmware shortens a path to a name was not read, so the reader accepts both.

## 12. Screen sizes

### 12.1 The sizes of the models

A theme screen is drawn for one exact screen: width, height and depth in bits. Depth 2 means four shades of grey. Depth 16 means color. These sizes come from `models/models.toml`, which stays the one place where they are written.

| Size | Models |
|---|---|
| 138 x 110 x 2 | `mini-1g`, `mini-2g` |
| 160 x 128 x 2 | `ipod-1g2g`, `ipod-3g`, `ipod-4g` |
| 176 x 132 x 16 | `nano-1g`, `nano-2g` |
| 220 x 176 x 16 | `ipod-color` |
| 320 x 240 x 16 | `ipod-video`, `classic-6g` |

That is five sizes for ten models. The default theme of the firmware has a playback screen for each of these five sizes, which gives five real documents to test the reader with (section 14).

### 12.2 One file per theme, not per size

On the player there is **one file per theme screen**: `night.wps`, not `night.160x128x2.wps`. The size is not part of the name, and the firmware does not choose a file by size when it loads. The size is chosen earlier, when a theme is packed for a model. Whether the firmware checks that the screen fits the display when it loads was not read. The reader checks it (section 5.2) and warns.

So a theme is made **for a model, or for a size**. The editor keeps the size with the theme, and one theme made for two sizes is two packed themes.

### 12.3 Themes from the outside

The source files of the themes of the firmware carry the size in their name, `<theme>.<width>x<height>x<depth>.wps`, or the name of a target instead of the size, `<theme>.<target>.wps`. A list file says which one goes with which screen, with a line `key.pattern: value`. The pattern is a regular expression that has to match the **end** of the size written as `160x128x2`, and it may add `&touchscreen` to require a feature. The builder copies the chosen file under the plain name `<theme>.wps`.

The reader of TacetOS does not read this list. It is useful to know it when someone brings a theme made for the firmware: the files in the package already have their plain names, and the size has to be asked of the person or guessed from the pictures.

## 13. Packing a theme for a model

A packed theme for a model is these files, all relative to `<data>`:

1. `themes/<name>.cfg`, the settings file of section 11.
2. `wps/<name>.wps`, and when there is one `wps/<name>.sbs` and `wps/<name>.fms`.
3. `wps/<name>/` with every picture that the screens load.
4. The fonts named by `%Fl` and by the key `font`, in `fonts/`.
5. The backdrop named by the key `backdrop`, in `backdrops/`. A backdrop named by `%X` is a picture of the screen, in `wps/<name>/` (section 10.4).
6. The icon sets named by `iconset` and `viewers iconset`, in `icons/`.

The list of files is made **from the parse tree** of section 4, not by searching the text. The theme builder of the firmware finds pictures by looking in each line for a name that ends in `.bmp` between parentheses or commas. That misses a name that comes after other arguments of a tag, and it takes a comment for a tag. The tree has none of these problems: the list is the first argument of the picture and font tags that load a file, the argument of `%X`, and the file names of the bar words `image`, `slider` and `backdrop` (section 8.7.2).

Fonts, icons and backdrops are shared between themes, so packing does not rename them. If a file of the same name is already on the iPod with different contents, the installer asks before it replaces it. That rule belongs to the installer, not to this spec.

## 14. What the tests need for files

- **Folder of a picture** (section 10.2): from the path of a screen to the folder of its pictures, with the extension removed, for `.wps`, `.sbs` and `.fms`, and for a name that has a dot in it (`a.b.wps` gives the folder `a.b`).
- **Settings file** (section 11.1): a comment with spaces before the `#`, a line with no colon, two colons in a value, spaces at the end of a value, a name with spaces in it (`viewers iconset`), a line that ends in CR LF.
- **List of files** (section 13): a small theme made by hand with one picture loaded by `%xl`, one `%x`, one in a bar with `image`, one font, one backdrop and one font named only in the settings file. The expected list is checked in next to the theme. A picture named in a comment is **not** in the list.
- **Missing file** (section 10.6): the same theme without one picture, and without one font: the error names the file.
- **Fonts** (section 10.3): number 1, number 14, a name that does not end in `.fnt`, the same number twice.
- **Sizes** (section 12.1): one theme screen drawn on each of the five sizes, to see that the layout of section 5.2 keeps every viewport on the screen.

Documents from real themes are kept outside the repository, as in section 9. The default theme of the firmware gives one real document for each size, downloaded by the continuous integration with the rest of the tests.

## 15. Not decided yet

- What the firmware does with a comment at the end of a line that has other content (section 3.3). From reading the parser the line break goes with the comment, but this must be tried on the real firmware before the reader copies it. Until then, the editor never writes such a comment.
- What the `options` argument of `%if` really is worth (section 7.4). It is read as a decimal, so the number stored looks like ten times the number written. To be tried with a known theme.
- The font number `0` (sections 5.2 and 10.3): reading the firmware, any number below 1 gives the fixed built-in font and a debug message says not to use it. Not used by any theme seen so far. Not tried on a player.
- The exact spread of a quantity over the branches of a condition for each Q tag (section 7.2): volume, battery, progress, signal, pitch, speed. To be filled with fixtures made from the firmware simulator.
- Whether the viewport tag is accepted anywhere in a line or only at its start (section 5.1). The editor only writes it at the start.
- What the playlist tag `%Vp` really draws (section 8.9): read from the way its arguments are stored, not tried.
- The exact signature and the behavior of the touch tags (section 8.9). Not needed for the target iPods, so left for later.
- The exact result of `%ss` with a start that is past the end of the text (section 7.6): the firmware seems to give no text at all.
- Whether the cyan color (0, 255, 255) is replaced by the foreground color in every picture or only in some (section 10.2). Only its definition was read.
- What the firmware does with a picture name that has a slash in it (section 10.2). The editor never writes one.
- What happens when two screens give the same font number to two different fonts (section 10.3). The table of fonts looks shared, from reading only.
- The format of the file of file type colors (section 10.5). The editor does not write the key until it is read.
- What `%X(d)` means (section 10.4). It is accepted by the firmware and gives no backdrop for the screen, but it is not in the tag table. And what happens with a backdrop that is not the size of the screen.
- What the firmware shows in place of a theme screen that was dropped because of a missing file (section 10.6).
- What the player does with an unknown name in a settings file, and with a line longer than its 128 byte buffer (section 11.1).
- How the firmware turns a whole path in `wps`, `sbs` or `fms` into a name (section 11.2), and the syntax of the value of `ui viewport`.
- Whether the firmware checks the size of a theme screen against its display when it loads it (section 12.2).
- Where the files named by the words `image`, `slider` and `backdrop` of a bar are looked for (section 8.7.2). Assumed to be the folder of the pictures of the screen, as for `%x`.
- Which models have a clock, a radio, a touch screen or a way to record (section 7.3). These four features are facts of each model and go in `models.toml`, checked on two sources like the other facts, before the preview relies on them.
