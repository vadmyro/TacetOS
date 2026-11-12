# Bootloader install, iPod Classic

How TacetOS installs its bootloader on an iPod Classic (6th generation, all four revisions). On this model the bootloader does not live on the disk: it is written into a small flash memory chip (the NOR) by a program that the computer sends to the iPod over USB, in the low-level mode called DFU. The model file is `docs/models/classic-6g.md`, and its install method is `dfu` in `models/models.toml`.

Sections 1 to 7 cover the modes of the iPod and the USB exchanges, in order. Sections 8 to 13 cover the image that is sent, the program that runs on the iPod, the layout of the NOR and the double boot. Sections 14 to 16 cover uninstall, the drivers of each system and the remaining errors. Section 17 lists what the tests need and section 18 what is not decided.

The numbers here are facts: the DFU protocol is a public USB standard, and the details that are specific to Apple come from public documentation and from a study of an existing installer, written in our own words. The parts that are our own design say "must". Where TacetOS does something on purpose that the existing installer does not, the text says so.

## 1. Words used here

- **NOR**: the small flash chip of the iPod that holds the first program run at power on. Apple's own boot code is there. The disk (the hard drive or the flash storage) is a separate device.
- **DFU mode**: a mode of the boot ROM of the iPod, in which it waits for one program over USB, checks it, and runs it. It does not depend on what is on the disk or in the NOR, so it is always there.
- **DFU image**: the file sent in DFU mode. It holds a small program (the **payload**) that runs on the iPod from memory. TacetOS sends two kinds: the **installer** payload, which writes the bootloader into the NOR, and the **uninstaller** payload, which puts back the state before install.
- **Double boot**: the only kind of install TacetOS does on this model. Apple's boot code stays in the NOR next to ours, and the user chooses at power on which firmware starts (project rule 5). The "single boot" of the existing tools, which destroys Apple's boot code, does not exist in TacetOS.
- **Host**: the computer, as opposed to the **device**, the iPod.
- **Request**: one USB control transfer from the host to the device or back (section 5).
- **Helper**: the small program with the right to talk to USB devices and disks. The app asks, the helper does it.

## 2. Modes of the iPod

The same iPod shows up on USB in different ways depending on its mode. The app must tell them apart by the USB id (vendor `05ac`, Apple), never by the name of the device.

| Mode | How the user gets there | USB id | What TacetOS does in it |
|---|---|---|---|
| **Normal** | The iPod is on and running Apple's firmware or ours | `05ac:1261` with Apple's firmware | Reads the iPod as a disk, if "disk use" is on |
| **Disk Mode** | Reset (hold Menu and Select until the Apple logo), then at once hold Select and Play | `05ac:1261`, to be confirmed (see `models.toml`, `to_verify`) | Reads and writes the music partition |
| **DFU** | Reset (hold Menu and Select), and keep holding both after the reset for about 8 more seconds. The screen stays black | `05ac:1223` | Sends the DFU image |
| **WTF** | Not reached by TacetOS. It is the second step of a restore by Apple's software, after Apple's own image was sent in DFU mode | `05ac:1241`, `1245`, `1247`, `1250` (one per revision of the Classic) | Nothing. Tells the user to reset the iPod |

Notes on the table:

- `05ac:1223` is shared by the Classic and the 3rd generation nano in DFU mode. TacetOS supports only the Classic, so the app must ask the user to confirm the model before sending anything when it sees `1223` and has not seen the same iPod in normal mode first (section 6).
- Reset takes about 6 seconds of holding Menu and Select. The point where DFU starts is not shown on the screen: the iPod only appears on USB. The app must watch USB while the user holds the buttons, and say "you can release the buttons" as soon as `1223` appears.
- In DFU mode the iPod has no disk on USB. The music partition cannot be read in this mode.
- The iPod leaves DFU mode on its own after a payload has run (it restarts), or when the user resets it again. A DFU iPod that receives nothing stays in that mode until reset.
- After the install, the iPod restarts in normal mode. With the TacetOS bootloader it starts our firmware by default; the buttons that choose Apple's firmware are part of the double boot (section 13).

## 3. What must be true before an install

These come from the existing installer and from the way the bootloader works, and the app checks them, in normal mode or Disk Mode, before it asks the user to enter DFU mode:

1. **Apple's firmware is on the iPod and runs.** The double boot needs Apple's boot code in the NOR, and Apple's firmware on the disk to start when the user asks for it. An iPod that does not start any more must first be restored by Apple's software.
2. **The firmware version is known.** The bootloader is only known to work with these versions of Apple's firmware:

   | Revision | Capacities | Apple firmware |
   |---|---|---|
   | Late 2007 | 80 GB, 160 GB | 1.1.2 |
   | Late 2008 | 120 GB | 2.0.1 |
   | Late 2009 | 160 GB | 2.0.4 |
   | Late 2012 | 160 GB | 2.0.5 |

   These are the last versions Apple released for each revision. Another version means the user has not updated: the app says so, and refuses to install until it is updated by Apple's software.
3. **The iPod is formatted for Windows** (a DOS partition table and a FAT32 music partition). The TacetOS firmware cannot read HFS+. A Mac iPod must be converted first, with the steps of `bootloader-legacy.md` section 21, which read the same on this model.
4. **The TacetOS firmware files are on the music partition** before the bootloader is installed, so that the first start after install finds them. The app copies them first, in Disk Mode or normal mode, then ejects the iPod, then asks for DFU mode.
5. **The battery is not empty.** The app cannot read it in DFU mode, so it asks the user to charge the iPod first. The NOR write is short, but a power cut in the middle of it is the one case where the iPod needs a restore by Apple's software (section 11).

## 4. The DFU protocol in short

DFU (Device Firmware Upgrade) is a public USB class. The iPod's boot ROM uses version 1.1 of it, with the changes of section 4.3. Everything goes through **control transfers** on endpoint 0, addressed to interface 0 of configuration 1.

### 4.1 Requests

Each request has a setup packet of 8 bytes: the request type (1 byte), the request number (1 byte), a value (2 bytes), an index (2 bytes, here always 0, the interface), and the length of the data that follows (2 bytes). Request type `0x21` means "class request, to an interface, host to device"; `0xA1` the same from device to host.

| Number | Name | Type | Value | Data | Used by TacetOS |
|---|---|---|---|---|---|
| 0 | DETACH | `0x21` | | none | No: the iPod's ROM does not implement it |
| 1 | DNLOAD | `0x21` | block number | the block, at most 2048 bytes, or none for the last one | Yes |
| 2 | UPLOAD | `0xA1` | block number | | No |
| 3 | GETSTATUS | `0xA1` | 0 | 6 bytes back (section 4.2) | Yes |
| 4 | CLRSTATUS | `0x21` | 0 | none | Yes, to leave the error state (section 7) |
| 5 | GETSTATE | `0xA1` | 0 | 1 byte back: the state | Yes |
| 6 | ABORT | `0x21` | 0 | none | Yes, to stop before the last block (section 7) |

### 4.2 Status and states

GETSTATUS returns 6 bytes:

| Byte | Meaning |
|---|---|
| 0 | Status: 0 for no error, else an error code (below) |
| 1 to 3 | Poll timeout: how many milliseconds the host must wait before the next GETSTATUS, a 24-bit little-endian number |
| 4 | State (below) |
| 5 | Index of a text describing the status, not used |

States, by number: 0 appIDLE, 1 appDETACH, 2 **dfuIDLE**, 3 dfuDNLOAD-SYNC, 4 **dfuDNBUSY**, 5 **dfuDNLOAD-IDLE**, 6 dfuMANIFEST-SYNC, 7 **dfuMANIFEST**, 8 dfuMANIFEST-WAIT-RESET, 9 dfuUPLOAD-IDLE, 10 **dfuERROR**. The states in bold are the ones the iPod is seen in.

Status codes, by number: 0 OK, 1 errTARGET, 2 errFILE, 3 errWRITE, 4 errERASE, 5 errCHECK_ERASED, 6 errPROG, 7 errVERIFY, 8 errADDRESS, 9 errNOTDONE, 10 errFIRMWARE, 11 errVENDOR, 12 errUSBR, 13 errPOR, 14 errUNKNOWN, 15 errSTALLEDPKT. The app shows the name in the log and maps the ones that matter to a message (section 7).

### 4.3 What is specific to the iPod

- **Block size**: 2048 bytes. Every block but the last is exactly 2048 bytes long.
- **Block numbers** start at 0 and grow by one for each block, including the empty block at the end.
- **The whole file is one DFU image of at most 128 KiB** (`0x20000` bytes), checked by the ROM. Its format is in section 8.
- **A checksum is appended by the host**, after the image and before sending: 4 bytes, little-endian, holding the bitwise NOT of the usual CRC-32 of the image (the one with the polynomial `0xEDB88320`, used by zip and Ethernet). The ROM checks it. Sent data = image followed by these 4 bytes, so its length is the image length plus 4.
- **The end of the transfer is a DNLOAD with no data.** It tells the ROM that the file is complete.
- **No detach, no upload.** The iPod enters DFU mode by its buttons only.
- **The end of the exchange is not the end of DFU 1.1.** After the last block, the ROM checks the image, runs the payload, and the USB device disappears. The last GETSTATUS often fails because the device is already gone: for this iPod that failure is the sign of success (section 5, step 6).

## 5. The exchanges, in order

This is what the helper does once the app has the DFU image ready (section 8) and the user has put the iPod in DFU mode.

1. **Find the device.** List the USB devices and keep those with vendor `05ac` and product `1223`. None: wait, the user may still be holding the buttons (the app polls every second, for 60 seconds, then says "the iPod did not appear in DFU mode"). More than one: refuse, ask to plug only one iPod. A WTF id of section 2: say the iPod is in the wrong mode and how to reset it.
2. **Open it.** Select configuration 1. If the OS has a driver bound to interface 0, detach it for the time of the transfer (Linux only), then claim interface 0. Section 6 has what differs per OS.
3. **Check the state.** Send GETSTATE. The answer must be 2 (dfuIDLE). If it is 10 (dfuERROR), send CLRSTATUS once and check again. Anything else: stop, nothing has been sent yet.
4. **Send the blocks.** For block number `n` from 0, take up to 2048 bytes of the data (image plus the 4 checksum bytes), and:
   1. send DNLOAD with value `n` and the block;
   2. send GETSTATUS;
   3. while the state is 4 (dfuDNBUSY), wait the poll timeout it gave (at least 1 ms, at most 5 seconds), and send GETSTATUS again. Stop after 20 GETSTATUS requests for the same block, the first one included;
   4. the state must now be 5 (dfuDNLOAD-IDLE) with status 0. Otherwise stop (section 7).
5. **Send the end.** DNLOAD with the next block number and no data. Then GETSTATUS. The usual answer is state 7 (dfuMANIFEST). A status 10 (errFIRMWARE) here means the ROM refused the image (bad checksum or bad signature): stop, and say the image is broken. Any other state: stop.
6. **Wait for the payload to start.** Wait the poll timeout of the last answer, then send GETSTATUS once more. Three outcomes:
   - **The request fails, or the device is gone**: the payload is running. This is the normal case on this iPod. Go to step 7.
   - **State 8 (dfuMANIFEST-WAIT-RESET)**: the ROM waits for a USB reset. Send it, then go to step 7. This case is in the standard, not seen on this iPod so far.
   - **Anything else, or a status 7 (errVERIFY)**: the ROM refused the image after checking it. Stop.
7. **Release the device.** Release interface 0, give the driver back if it was detached in step 2, close.
8. **Wait for the result.** The payload writes the NOR (a few seconds), plays a tone, and restarts the iPod (section 12 has the tones). The app watches USB for the iPod in normal mode (`05ac:1261`) for 60 seconds, then reads it as in normal mode and checks that the bootloader is there (the detection spec says how).

Every request has a timeout of 500 ms, except the waits of steps 4.3 and 6, which follow the poll timeout of the iPod. The whole transfer of a 128 KiB image is 65 blocks of data (64 full ones and a short one) plus the empty block, and takes about a second.

The existing installer waits only once when the iPod answers "busy", then treats a second "busy" as an error. TacetOS polls until the state changes, with the limit of step 4.3. It also never sends anything before the state check of step 3.

## 6. What differs per system

The exchanges are the same everywhere. Only the way to reach the device differs. This section covers the access; the drivers and what the user must do on each system are in section 15.

| System | Access | Notes |
|---|---|---|
| Linux | libusb | The helper needs write access to the USB device node. A udev rule installed with the app gives it to the logged-in user for `05ac:1223` only, so that nothing runs as root. No kernel driver binds to the iPod in DFU mode in practice, but step 2 detaches one if it is there and gives it back at step 7. |
| Windows | libusb with the WinUSB driver, or Apple's own driver | DFU mode needs a driver bound to `05ac:1223`. Apple's driver (installed with iTunes) works through its own device interface; WinUSB works through libusb. Apple's helper program, when it runs, opens the iPod first and must be stopped. |
| macOS | IOKit, or libusb on top of it | No driver to install. Apple's helper process for iTunes may open the iPod first; it must be paused for the transfer. The USB reset of step 6 does nothing on recent versions of macOS, which is one more reason the normal end is "the device is gone". |

On all systems: the iPod's product id is checked at step 1, and the helper refuses any device that is not `05ac:1223`, even if the user picks it. A DFU transfer to the wrong device could brick it.

The id `1223` is also the 3rd generation nano in DFU mode (section 2). The app knows it is a Classic only if it saw the same iPod in normal mode just before (same session, the user followed the steps). Otherwise it asks the user to confirm "iPod Classic" before step 4.

## 7. When the transfer fails

Before step 4, nothing has been sent: the iPod is untouched. During steps 4 and 5, the ROM has only received part of a file, held in memory: the NOR has not been touched yet, because the payload has not run. So a failure of the transfer itself never harms the iPod. In every such case the helper:

1. sends ABORT (if the device still answers), then GETSTATE, and CLRSTATUS if the state is 10;
2. releases the device (step 7 of section 5);
3. tells the user to reset the iPod (hold Menu and Select) and try again.

| Failure | Where | Message |
|---|---|---|
| No device after 60 s | Step 1 | "The iPod did not appear in DFU mode." With the button steps again |
| Permission denied | Step 2 | "No permission to use the iPod over USB." With the fix for this OS (section 15) |
| Device busy | Step 2 | "Another program is using the iPod." Names Apple's helper if it runs |
| Not in dfuIDLE | Step 3 | "The iPod is not ready. Reset it and enter DFU mode again." |
| Request timeout or error | Steps 4, 5 | "The transfer was interrupted. The iPod is not changed." |
| errFIRMWARE or errVERIFY | Steps 5, 6 | "The iPod refused the install file." Logged with the hash of the image. Not retried with the same file |
| No normal mode after 60 s | Step 8 | "The iPod did not restart." Handled by sections 11 and 12, because the payload may have run |

Once the payload runs (step 6 of section 5), the outcome is no longer a USB matter: what can go wrong in the NOR, and how the payload reports it, is in sections 11 and 12.

## 8. The DFU image

The image is what section 5 sends. Its first bytes are read by the boot ROM, which does not run code that Apple has not signed. The image therefore uses two quirks of the ROM's checks (a special signature offset, and a certificate offset that wraps around) so that the ROM accepts it. The values involved are fixed and the same in every image.

### 8.1 Layout

Offsets are from the start of the image. Numbers longer than one byte are little-endian, like everything else in this image.

| Offset | Size | Content |
|---|---|---|
| `0x000` | `0x50` | The header (section 8.2) |
| `0x050` | 698 bytes | The certificate area: a fixed block of bytes. Its last 4 bytes (offset `0x306`) are replaced by the **entry address**, `0x2200030C` |
| `0x30A` | 2 | Zeros, to bring the payload to a multiple of 4 |
| `0x30C` | rest | The payload (sections 8.3 and 8.4) |

The ROM copies the image to memory at `0x22000000`, so the payload lands at `0x2200030C`, which is the entry address. The first `0x30C` bytes are the same in every image. The whole image, before the 4 checksum bytes of section 4.3, must not exceed `0x20000` bytes.

### 8.2 Header

| Offset | Size | Value |
|---|---|---|
| `0x00` | 4 | The text `8702`, the name of the chip |
| `0x04` | 3 | The text `1.0` |
| `0x07` | 1 | Encryption type: 3 |
| `0x08` | 4 | Entry offset: 0 |
| `0x0C` | 4 | Data size: 0 |
| `0x10` | 4 | Signature offset: `0x00000300` (the special value) |
| `0x14` | 4 | Certificate offset: `0xFFFFF850`, that is minus `0x7B0`. Added to `0x800` (the size of a normal header) it wraps around to `0x50`, where the certificate area really is |
| `0x18` | 4 | Certificate size: 698 (`0x2BA`) |
| `0x1C` | 36 | Zeros |
| `0x40` | 16 | A fixed signature of the header, the same in every image |

The 16 bytes at `0x40` and the 698 bytes of the certificate area are arbitrary binary values, so this text does not repeat them. Section 18 says where they come from.

### 8.3 The installer image

After the first `0x30C` bytes, the installer image holds, in this order:

1. **The installer payload**: the program of section 11, compiled for the iPod. Its length is fixed for a given version of the program (about 5.4 KB for the one studied). Its **last `0x50` bytes** are a second header, called the **bootloader header** below, which the program signs and writes to the NOR together with the bootloader.
2. **Zeros**, up to the point where the bootloader body starts exactly `0x800` bytes after the start of the bootloader header.
3. **The bootloader body**: the bootloader file of section 9 without its 8-byte prefix, padded with zeros to a multiple of 16 bytes.

The builder fills these fields of the bootloader header, and nothing else:

| Field | Value |
|---|---|
| Text, version | `8702`, `1.0` |
| Encryption type | 2: the body is stored in the clear, but signed |
| Data size | the padded size of the body (a multiple of 16) |
| First byte of the last 16 bytes (the field that normally holds the header signature) | **0 for a double boot**. Any other value makes the program write a single boot |

The signatures of the body and of the bootloader header are **not** in the image: the program computes them on the iPod, with the key that lives in the iPod's hardware and that the host never has. That is also why the program works on every iPod of the family.

Size of the whole image: `0x30C` + the length of the installer payload + `0x7B0` + the padded size of the body. The `0x7B0` is `0x800` minus `0x50`. With the program studied, this leaves room for a body of about 120 KiB. The builder must compute the size before it reads anything else of the file, and refuse an image over `0x20000` bytes.

**Rule 5 in the code**: the builder has no option to set the single boot byte, it always writes 0, and the helper refuses to send an installer image whose byte is not 0 (it re-reads the byte in the image it is about to send). A test covers both (section 17).

### 8.4 The uninstaller image

The first `0x30C` bytes, then the uninstaller payload of the later part, and nothing else: no bootloader header, no padding. Its size is `0x30C` plus the length of the payload.

### 8.5 The checksum

Section 4.3 has it: 4 bytes appended after the image, the NOT of the CRC-32. It is added when the image is sent, not when it is built, so the saved image and the image checked in section 17 do not include it.

## 9. The bootloader file given to the builder

The bootloader is built by the CI (it is not in the repository) and delivered to the app as one file, whose name is in the constants file of rule 2. The file is an 8-byte prefix followed by the body:

| Offset | Size | Content |
|---|---|---|
| 0 | 4 | A checksum, **big-endian** |
| 4 | 4 | The model tag, the text `ip6g` |
| 8 | rest | The body: the bootloader program, in the clear |

The checksum is the model number, **71**, plus the sum of every byte of the body, each taken as an unsigned number 0 to 255, kept on 32 bits (it wraps around). The model number and the tag are facts of this model and go in the constants file as well.

The builder checks, in this order, and stops at the first failure:

1. The file has at least 9 bytes (a prefix and one byte of body).
2. The tag is `ip6g`. Another tag: "This file is a bootloader for another model, or not a bootloader."
3. The checksum matches. Otherwise: "The bootloader file is damaged."
4. The padded body fits in the image (section 8.3). Otherwise: "The bootloader is too big for the iPod."

The app reads the file once, checks it, and builds the image from the bytes it has checked. It never reads the file a second time for the image.

## 10. The NOR and the layout of a double boot

The NOR chip is 1 MiB (`0x100000`) and is organised in sectors of 4096 bytes (`0x1000`). A sector is erased as a whole, and an erased sector holds only `0xFF` bytes. What matters here:

| Range | Content |
|---|---|
| `0x00000` to `0x07FFF` | Identity of the iPod (serial number, hardware ids). Never read or written by TacetOS |
| `0x08000` | The **boot slot**: the image that the boot ROM loads and runs at power on. It may be up to `0x20000` bytes (128 KiB) long and ends at `0x28000` at the latest |
| `0x28000` up to the lowest file | Unused |
| Files, then the directory | Small files of Apple's firmware, listed in a directory in the last `0x200` bytes of the chip. Each entry gives the offset of its file. Never written by TacetOS |

The image format in the boot slot is the same as in section 8, with a header of `0x800` bytes (not the shortened header of the DFU image) followed by the body. Two kinds exist: **type 1**, where the body is encrypted with the key of the iPod, and **type 2**, where the body is in the clear but the signatures are the same. The boot ROM accepts both.

**An iPod as Apple made it**: the boot slot holds Apple's boot image, type 1.

**An iPod with the double boot**, where S is the size of TacetOS's image (header and body) rounded up to a multiple of 4096:

| Range | Content |
|---|---|
| `0x08000` | The TacetOS bootloader, type 2 |
| `0x08000` + S | Apple's boot image, type 2: the same program, decrypted, with its header signature recomputed |

Nothing stores where Apple's copy is. The bootloader knows its own size from its header, rounds it up to 4096, and adds `0x8000`. So the two images must stay one behind the other, and S can only be changed by an install or an update.

**What a valid image is**, for both the installer and the bootloader when they look at a slot. All of these must hold, otherwise the slot is "not valid" and the number is the reason in the log:

1. The first 4 bytes are the text `8702` (otherwise: no image).
2. The signature of the header (the first `0x40` bytes) is the right one for this iPod.
3. The body size is at most `0x20000 - 0x800`, and the entry offset is not past the end of the body.
4. The encryption type is 1 or 2.
5. After the body has been read (and decrypted for type 1), its signature matches the one in the header.

**Telling Apple's image from ours.** The program has the list of the signatures (the decrypted ones) of the boot images that Apple shipped for the four revisions of section 3. An image whose signature is in the list is Apple's, whatever it is called. **Any other valid image is taken to be a TacetOS bootloader**, and Apple's copy is then looked for behind it. This means that a new boot image from Apple, one day, would be taken for a bootloader by the program: it is why section 3 refuses versions other than the four.

**Free space.** The program reads the directory, finds the file that starts lowest in the chip, and the free space is that offset minus `0x28000`. It must be at least S.

## 11. What the installer payload does

The payload is the program that runs on the iPod after step 6 of section 5. It runs from memory, with no operating system, and it is the only part that writes the NOR. These are its steps; each failure goes to the end with the tone given (section 12). Nothing in the NOR has changed before step 6 of this list.

1. **Start.** Play the first tone. Set the bus speed to the chip.
2. **Sign.** Compute the signature of the bootloader body and of the bootloader header (section 8.3). If the single boot byte is not 0, do the single boot: write the bootloader header and body to `0x8000` without looking at what is there, and go to the end. **TacetOS never gets here** (section 8.3).
3. **Read the boot slot** (section 10, valid image). If it is not valid: stop with the one-tone error. Nothing has been written.
4. **Find Apple's image.** If the slot holds an image of Apple's, keep it. If it holds another valid image, it is a bootloader to update: read the slot at `0x8000` + the size of that image, rounded up to 4096. It must be valid and must be Apple's. Otherwise stop with the one-tone error.
   An image of Apple's that was read from a type 1 slot is now in memory in the clear. One that was read from behind a bootloader already was.
5. **Check the space** (section 10). If the free space is under S, stop with the two-tone error. The test uses all of S even when it is an update, so it is stricter than needed in that case.
6. **Write Apple's copy, in the clear, at `0x8000` + S**, then **the bootloader at `0x8000`**. The write goes sector by sector: if the sector already holds the right bytes it is left alone; otherwise it is erased if it was not empty, then written, then compared with what was meant. The whole range is compared once more at the end.
7. **If any write or comparison failed**, put Apple's image back where it was: encrypt it again (type 1, signatures redone) and write it at `0x8000`. If that works, stop with the one-tone error: the iPod is as before. If that fails too, stop with the three-tone error.
8. **End.** Wait until at least 2 seconds have passed since the start, play the final tone, and reset the iPod with the watchdog.

Two properties of this order:

- **Apple's copy is written before the bootloader**, so as long as the bootloader is not written, the old slot is still the one the ROM boots from. During an **update**, if the new bootloader is smaller (S is smaller than before), the new copy of Apple's image starts inside the old bootloader. A power cut in that window leaves a slot that does not boot. DFU mode still works then, but the install program of the next try finds "no valid image" and refuses, so the iPod needs a restore by Apple's software. So **a release of the bootloader must never have a smaller S than the one before**: the CI compares S with the previous release and fails the build otherwise.
- **The comparison is done by the iPod itself**, not by the host, so the app sees success or failure only through the tone and the restart.

**Battery.** The payload does not check it. That is why section 3 asks the user to charge the iPod.

## 12. The tones

The payload has no screen. It talks to the user with the piezo buzzer. A tone is a pitch and a length.

| When (step of section 11 or 14.3) | Tone | Meaning |
|---|---|---|
| Step 1 | one short high beep (2000 Hz, 100 ms) | The program runs |
| Step 8, success | a low beep then a high one (1000 Hz for 100 ms, then 2000 Hz for 150 ms) | Done. The iPod restarts |
| Step 8, failure | one low long beep (about 330 Hz, 500 ms) | The install did not happen, or was undone. The iPod is as before and should start with Apple's firmware |
| Step 8, no space | two low long beeps | Not enough free space in the NOR. Nothing was written |
| Step 8, NOR damaged | three low long beeps | The restore of step 7 failed. The iPod will probably not start. Restore with Apple's software |

The buzzer is inside the iPod, so no headphones are needed. The app does not depend on the tones, because the user may not hear them: after the restart it looks at the state of the iPod (section 5, step 8). The app still tells the user to listen for them, and what each one means.

If the iPod restarts and shows the Apple logo with the install not done, the app tells the user that the install failed and that the iPod is as before. If it does not restart at all (no USB for 60 seconds, no screen), the app says to hold Menu and Select for about 6 seconds to reset, and, if that does not help, to enter DFU mode and restore with Apple's software.

## 13. The double boot at power on

This is what the TacetOS bootloader does at every power on and every reset, and what the user sees. It is the reason the install is "double": both firmwares stay on the iPod and the buttons choose.

### 13.1 The order of events

Times are from power on and are approximate, from comments and measurements in the studied code. They differ by a hundred milliseconds depending on the screen revision.

1. **The boot ROM loads the bootloader** from the boot slot (about 0.25 s). The bootloader keeps its own header in memory, because it holds the size that locates Apple's copy (section 10).
2. **Waking from hibernation.** If the iPod was only asleep (Apple's firmware puts it into hibernation after about 30 minutes idle, with the memory kept alive), the bootloader starts Apple's copy at once, so that Apple's firmware can restore its state. No buttons are read.
3. **Button check** (under one second after power on). See 13.2.
4. **Screen and disk.** The bootloader lights the screen, waits for the disk to spin up (until about 2.6 to 2.8 s on a hard disk model), and on those models first waits for the battery to have enough charge to spin it.
5. **Hold switch.** If it is locked, the bootloader starts Apple's firmware.
6. **Otherwise it loads the TacetOS firmware** from the music partition: a file of at most 8 MiB, whose name is in the constants file (rule 2). It runs it, about 3 s after power on.

### 13.2 The buttons

The state of the buttons is read once, under one second after power on. One exception: if Select and Menu are both held, the bootloader waits until they are released and waits one more second before reading again. The user who is entering DFU mode (section 2) therefore never starts the disk.

| Held at power on | Result |
|---|---|
| Nothing, or anything not listed | TacetOS firmware |
| Menu | Apple's firmware |
| Select and Left | Apple's diagnostics (through Apple's copy) |
| Select and Play | Apple's Disk Mode (through Apple's copy) |
| Select and Right | The bootloader's own USB mode: the iPod shows itself to the computer without starting any firmware |
| Select and Menu, kept | The iPod resets after about 5 to 6 s, and enters DFU mode after about 8 s more (section 2). This is Apple's reset and it works with any bootloader |

Apple's copy is started by jumping into it after reading it into memory. It is a normal start for Apple's code, so Apple's Disk Mode works as it always did, and **the `05ac:1261` of section 2 is Apple's**.

### 13.3 Choosing the firmware, as the user does it

- **From Apple's to TacetOS.** Stop playing and wait a few seconds for the disk to stop. Hold Select and Menu until the screen goes black (about 5 to 6 s), then release **at once**, so as not to enter DFU mode. The iPod starts the bootloader, which starts TacetOS.
- **From TacetOS to Apple's.** Turn the iPod off, then turn it on while holding Menu, for at least one second. Two other ways, also from the studied tools' documentation and not checked by us: lock the hold switch before the loading message appears, or turn the iPod off, lock the hold switch and connect it to the computer.

The app shows these steps in its help (they replace the "single boot" warnings of other tools). Whether one of the ways costs the user their settings is in section 18.

### 13.4 When the bootloader cannot start something

- **No TacetOS firmware** (the file is missing or unreadable, or no partition is found): the screen says so, the bootloader's USB mode starts by itself once a cable is plugged (to put the files back), and the screen then says to hold Menu and Select to reset.
- **Apple's copy cannot be loaded** (damaged, or not found): the screen says so and says to reset and start TacetOS. This should not happen after a successful install.
- **Disk not found**: the screen says to reset, then to enter Apple's Disk Mode with Select and Play.

The exact wording of these screens belongs to the bootloader's own spec (P6). What is fixed here is that **every error screen names a way out** and that the DFU mode of section 2 is always still reachable, because the ROM provides it.

## 14. Uninstall

Uninstall puts the NOR back to what an iPod as Apple made it holds (section 10): Apple's boot image, in its encrypted form, at the start of the boot slot, and nothing behind it. It is a DFU transfer like the install, with the uninstaller image of section 8.4. It does not touch the disk, Apple's firmware, or the TacetOS files on the music partition.

### 14.1 When it is offered

Uninstall is **always offered** once an iPod Classic is in DFU mode, whatever the app believes about the bootloader. The reason is that the payload below checks the NOR itself and does nothing when there is no bootloader, so a user who is not sure of the state, or who lost the install record, can run it without harm. The app also offers it first when the TacetOS firmware cannot start (section 13.4), because it is the way back to a plain Apple iPod.

The checks of section 3 do **not** apply, except the battery (check 5). Uninstall does not need the firmware version, the partition format or the firmware files. It is also the right answer when the user will sell or give the iPod away.

### 14.2 The exchanges

The same as section 5, with the uninstaller image. The app shows the same button steps (section 2) and the same wait (section 16.1). The image is much smaller than an installer image, about 6 KB with the payload studied, so the transfer is a few blocks long. The helper refuses to send an uninstaller image that is larger than `0x30C` plus a payload of 16 KiB, which is far above what is needed and catches an installer image given by mistake. Rule 5 does not apply here (there is no single boot byte in this image), but the helper still checks that the first four bytes are the image tag of section 8.2, and that the image is exactly the prefix and the uninstaller program, with nothing after (the length of the program comes from the same generated parts).

### 14.3 What the uninstaller payload does

It runs from memory, like the installer payload (section 11), and uses the same routines to read, check and write images. These are its steps; each failure goes to the end with the tone given (section 12).

1. **Start.** Play the first tone. Set the bus speed to the chip.
2. **Read the boot slot** (section 10, valid image). If it is not valid: stop with the one-tone error. Nothing has been written.
3. **Look at what it holds.** If it is an image of Apple's, there is no bootloader: stop with the success tone and nothing written. Uninstalling twice is not an error.
4. **Find Apple's copy.** The slot holds another valid image, taken to be the bootloader. Read the slot at `0x8000` + the size of that image, rounded up to 4096. It must be valid **and be Apple's** (its signature is in the list of section 10). Otherwise stop with the one-tone error: nothing has been written, and the iPod needs a restore by Apple's software if it does not start.
5. **Encrypt it again.** Apple's copy is in memory in the clear. Encrypt it with the key of the iPod and redo its signatures, so that it is a type 1 image again.
6. **Write it at `0x8000`**, with the sector by sector method of section 11, step 6. If this fails, stop with the three-tone error: the bootloader's first sectors are already overwritten and the slot may not boot.
7. **Erase what is left.** The bootloader and the clear copy of Apple's image were behind the restored image. Erase the sectors from the end of the restored image (rounded up to 4096) for the size of the bootloader (rounded up to 4096). **A failure here changes nothing for the user**: the slot is valid and nobody reads those sectors, and the next install overwrites them (it erases a sector that is not empty). The payload does not report it.
8. **End.** The same as section 11, step 8: wait until 2 seconds have passed since the start, play the final tone, reset the iPod with the watchdog.

There is **one short window of risk**: step 6 starts by erasing the sector that holds the bootloader's header. A power cut between that point and the end of the write leaves a slot that does not boot. DFU mode still works then, but the iPod needs a restore by Apple's software. The window is about a second long. This is the same kind of risk as the update of section 11, and the same remedy: the battery check, and nothing else touching the iPod at that time.

### 14.4 After the restart

The iPod starts with Apple's firmware at once, with no bootloader and no choice of buttons. The app watches for it in normal mode (section 5, step 8) and tells the user:

- that the bootloader is gone and the iPod is as Apple made it;
- that the TacetOS files are still on the music partition, in the folder named in the constants file, and that removing them is a separate action (they take space and do no harm). That action goes through the helper like every disk write; it is described with the firmware install, not here;
- if the install record exists, that the app has deleted it.

If the three-tone error was heard, or the iPod does not restart, the messages of section 12 apply: reset, then restore with Apple's software.

Before an update of Apple's firmware by Apple's software, the app advises to uninstall the bootloader first, to be safe (what Apple's software does with a bootloader in the NOR is not checked by us, section 18).

## 15. Drivers and access, system by system

Section 6 has the table of what is used. This section has what the user and the app must do on each system, and what the failures look like. The helper maps each failure to one of the messages of sections 7 and 16.

### 15.1 Around the transfer, on every system

Before the app asks for DFU mode:

1. **Eject the iPod.** Every partition of the iPod is unmounted through the helper, and the app waits until the system no longer lists it. If the unmount fails: "Close the programs using the iPod", and the app tries again every 3 seconds. The step is not skipped, because a mounted disk in DFU mode is a disk that disappears under the system's feet.
2. **Wait for the disk to stop.** On the models with a hard disk, about 18 seconds after the eject, the time the studied installer allows for the disk to spin down by itself. This is so as not to cut the power of a spinning disk when the iPod resets into DFU mode.
3. **Stop Apple's programs** (sections 15.3 and 15.4).

After the transfer, **in every case, success or failure**, the helper resumes what it paused. If the app or the helper stops without doing it (a crash, a power cut of the computer), the app notes what it paused in a small state file, and at its next start it resumes those processes before anything else.

### 15.2 Linux

- **Library**: libusb 1.0, installed with the app, or listed as a dependency of the package.
- **Permission**: a udev rule file that matches the USB subsystem, vendor `05ac` and product `1223`, and tags the device for the logged-in user (the `uaccess` tag). It has no other id, so that the rule never touches the iPod in normal mode. It is installed in the rules folder of the system by the package of the app.
- **Without a package** (a tarball or a portable build): the app shows the rule and the two commands to run in a terminal, to copy it to the rules folder and to reload the rules. The app never runs `sudo`. The user then enters DFU mode again, because the rule is applied when the device appears.
- **Failures**: `LIBUSB_ERROR_ACCESS` at open: the rule is missing or not loaded (message "No permission to use the iPod over USB", with the rule). `LIBUSB_ERROR_BUSY` at claim: another program holds the interface. `LIBUSB_ERROR_NO_DEVICE` anywhere before step 6: the cable was pulled or the iPod reset.
- A virtual machine or a container that does not pass USB devices through shows no device: the "did not appear" message of section 7 is the only symptom. The help mentions it.

### 15.3 Windows

Three situations are possible when the iPod is in DFU mode. The helper finds out in this order:

| Situation | How it shows | What the helper does |
|---|---|---|
| **Apple's driver is bound** (the Apple Mobile Device USB driver, installed with iTunes, with Apple's devices app, or alone) | A device interface of Apple's DFU class (GUID `B8085869-FEB9-404B-8CB1-1E5C14FA8C54`) is listed for `05ac:1223` | Opens the interface path and then the pipe `PIPE0` below it. A control request is one write (host to device) or one read (device to host) on that pipe, whose data starts with the 8-byte setup packet of section 4.1 |
| **WinUSB is bound** (installed by the user, or by TacetOS if decided so, see section 18) | libusb opens the device | Uses libusb, as on Linux, without the detach step |
| **No usable driver** | The iPod shows in the Device Manager as an unknown device, or libusb fails at open with `LIBUSB_ERROR_NOT_SUPPORTED` | Stops. The app shows the help below |

A third kind of driver gives a confusing failure: with the **libusb-win32 driver** (the old one), libusb opens the device but fails to select the configuration with `LIBUSB_ERROR_NOT_FOUND`. The helper reports "The driver bound to the iPod is not one TacetOS can use", and the help tells the user to replace it with WinUSB.

**The help for "no usable driver"** has two ways, and the user picks one:

1. Install Apple's driver. TacetOS does not ship or download it: it is Apple's software, and the user gets it from Apple.
2. Bind WinUSB to `05ac:1223` with a driver tool such as Zadig, which the help links to. The help states to pick the device with the id `05AC 1223` and the driver WinUSB (or libusbK), and warns that a wrong pick binds the driver to another device. The app **cannot** do it on its own, because it needs the administrator's approval and a driver-signing step that the app does not own (open point in section 18).

**Apple's programs.** If iTunes or Apple's devices app runs, the app asks to quit it and waits (polling every second, with no limit but the user's Cancel). The background process (`iTunesHelper.exe`, and the device service of the same package) is **paused**, not killed, with the system's process suspend call, and resumed as in section 15.1. If the pause fails: "Stop it from the Task Manager and try again".

**Disk use.** If the user manages the iPod with iTunes, the iPod appears as a disk only if "Enable disk use" is on. Without it, the checks of section 3 cannot be done. The app says where the setting is (Summary, Options). This is only for the check in normal mode: DFU mode does not use it.

**Administrator rights** are not needed for the transfer once a driver is bound. The helper does not ask for them.

### 15.4 macOS

- **No driver to install.** The helper uses IOKit's USB device interface (or libusb on top of it): it opens the device with exclusive access and sends the control requests through it.
- **Exclusive access.** Another process may have opened the iPod first: Apple's sync helper (`iTunesHelper` on older systems, a device agent of the Music app or of the Finder on recent ones), or a previous run of TacetOS that did not release. The open fails with the IOKit error `0xE00002C5` (exclusive access) or with a generic "busy" error. The app then pauses the process it knows (as on Windows, with `SIGSTOP`, resumed with `SIGCONT`), waits two seconds, and tries again once. If it still fails: "Another program is using the iPod", naming the processes it found.
- **Signed and hardened builds.** A build that uses the hardened runtime needs the USB entitlement (`com.apple.security.device.usb`) in the helper, or the open fails with a permission error. The helper is not sandboxed. This is a build setting that the CI checks (the helper is tested on a runner, with no iPod, for the entitlement only).
- **The USB reset of step 6 does nothing** on recent versions of the system (section 6). The normal end is "the device is gone".
- **No administrator rights** are needed to talk to a USB device on macOS.

## 16. More error cases

Section 7 has the USB failures, and section 11 the failures inside the NOR. These are the ones left. Every case gives one short message, the details go to the log, and **every message names a way out**.

### 16.1 The buttons are still held

If the user keeps Select and Menu held after the iPod entered DFU mode, the ROM resets the iPod about 8 seconds later, and the iPod disappears from USB for a moment, then comes back in normal mode. A reset in the middle of the NOR write is not a case the program survives, so the helper never opens the device in the first seconds. The rule:

1. When `1223` appears, the app says "release the buttons now" and **waits 10 seconds**, with a countdown.
2. At the end of the wait it runs step 1 of section 5 again. The device must be there, with the same bus address as at the start of the wait.
3. If it has gone, or has a new address, the iPod was reset: the countdown starts again from the next `1223`. After three resets in a row the app stops and says "The iPod keeps leaving DFU mode. Release the buttons as soon as the message appears".

The existing installer waits the same 10 seconds before sending. TacetOS adds the check of the address at the end.

### 16.2 After the transfer

| What the app sees | What it says | What it does |
|---|---|---|
| The iPod is back in normal mode, and is the state expected (bootloader present after an install, absent after an uninstall) | "Done" | Writes or deletes the install record |
| The iPod is back in normal mode, and is the state it was in before | "The iPod restarted but the change was not made. It is as before" | Offers to try again once, and shows the table of tones of section 12 for what the user heard |
| The iPod is back, and the app cannot tell the state | "The iPod restarted. The change could not be checked" | Shows the check by hand: with the bootloader the TacetOS start screen shows, and Select with Menu held at start gives DFU |
| The iPod does not come back in 60 seconds | As in section 12 | Reset, then DFU, then a restore by Apple's software |
| The iPod comes back in DFU mode (`1223` again) | "The iPod went back to DFU mode" | The buttons were held, section 16.1. Not an error of the install |
| The iPod comes back as a WTF id | "The iPod is in the wrong mode. Reset it" | Section 2 |

A `1223` seen right after the last request is the iPod that has not left USB yet, so it only counts as "back in DFU mode" once the iPod has been absent from the bus at least once.

**How the state is read after the restart** belongs to the detection spec (section 18). This section uses three answers: present, absent, unknown.

### 16.3 Retries

- After a failure **before** step 6, the iPod is untouched, and the user can try again at once, with no limit.
- After a **one-tone** error (section 12), the iPod is as before. The app does **not** loop. It tries again once if the user asks, and after a second one-tone error it stops and says what the common causes are: a version of Apple's firmware that is not on the list of section 3, an iPod that was restored in an unusual way, or a boot slot that does not hold a valid image. The log has the numbers of section 10.
- After a **two-tone** error (no space), nothing was written. The app says that the install cannot go on, and asks the user to report it: a Classic with that little free space has data in the NOR that TacetOS does not know.
- After a **three-tone** error, or when the iPod does not restart, the app **does not offer to install again**. It shows the restore by Apple's software only (the DFU mode and Apple's own program), with the steps of the user's system. The install program of the next try would refuse anyway (section 11, step 3).

### 16.4 The computer

- **The computer sleeps or the app closes during the transfer** (before step 6): the iPod stays in DFU mode with a part of the file in memory, and nothing changed. After step 6 the payload runs on its own and the computer is not needed. The cable can be pulled at that point: only the iPod's own battery matters.
- **The battery of the iPod is empty or nearly empty**: the app cannot read it in DFU mode (section 3). If the first tone of section 12 is not heard and the iPod does not restart, the help says to charge the iPod for an hour and to try again.
- **Two iPods connected**: section 5, step 1 refuses. The message asks to unplug the other one, even if it is in normal mode, because a second iPod can be reset by mistake when the user holds the buttons.
- **The iPod is a 3rd generation nano** (the same DFU id): the user confirms the model (section 6). If the user says "Classic" for a nano, the payload would run on the wrong chip. The app asks the question in clear words (name, capacity, the back of the case) and the log records the answer.

## 17. What the tests need

No test of this part talks to a real iPod. The helper's USB access is behind one small interface (open, control transfer, reset, close), and the tests use a fake device that plays the iPod's side of section 5 from a script:

- The normal transfer of a 128 KiB image: 64 full blocks, one short block, the empty block, then the device disappears at step 6.
- An image whose length is an exact multiple of 2048 (so the last data block is full).
- A device that answers dfuDNBUSY once, three times, and 25 times (the last one must stop).
- A device in dfuERROR at step 3 (one CLRSTATUS, then idle), and one still in error after it.
- errFIRMWARE after the empty block, and state 8 at step 6 (one reset expected).
- A timeout in the middle of the blocks: ABORT is sent, the error is the one of the table in section 7.
- Two DFU devices at once, a WTF id, and a device `05ac:1261` given to the DFU code by mistake (refused).
- The checksum: a few fixed images with their expected 4 bytes, computed by a second CRC-32 implementation in the tests.

The expected results are the list of requests sent (type, number, value, length) and the final message.

For the image and the double boot (sections 8 to 13):

- The first `0x30C` bytes of an image: the header fields of section 8.2 at their offsets, the entry address at `0x306`, the payload at `0x30C`.
- The size formulas with a bootloader body of 1 byte, of 16 bytes, of 17 bytes (padded to 32), and of the largest size that fits. One byte more is refused, and nothing is built.
- The bootloader header of an installer image: encryption type 2, the padded size, all other bytes zero except the ones the program fills. The single boot byte is 0. A test patches it to 1 in a copy of the image and checks that the helper refuses to send it (rule 5).
- The bootloader file: a good one, a wrong tag, a wrong checksum by one, a file of 8 bytes, a body whose byte sum wraps around 32 bits.
- The uninstaller image: header, certificate area and payload, with nothing after.
- CI only (D22): the image built by TacetOS is identical, byte for byte, to the one built by the existing tool from the same bootloader file and the same payload. The existing tool is downloaded for the test and is never added to the repository.
- The program of section 11 is tested on the host if its logic is ours (section 18). The NOR is a 1 MiB array of bytes, and the cases are: a new iPod, an update with a larger bootloader, an update with the same size, a write that fails in the bootloader, a write that fails in Apple's copy, a restore that fails too, a NOR with no space, a slot with no image, a slot with a damaged signature, and a slot with a bootloader but no Apple image behind it. For each, the expected result is the bytes of the NOR and the tone.
- The rule on S (section 11): a script compares S of the new bootloader with S of the previous release.

For the uninstall, the drivers and the error cases (sections 14 to 16):

- The uninstaller payload on the same NOR array, if its logic is ours: an iPod with a bootloader (the NOR must be the one Apple made, byte for byte, in the boot slot and the sectors after it), an iPod with no bootloader (nothing written, success tone), a slot with no image, a slot with a bootloader and no Apple image behind it, a slot with a bootloader followed by an image that is not on Apple's list, a write that fails in the restore (three tones), and an erase that fails (success tone, valid slot). An install followed by an uninstall must give back the NOR it started from.
- The uninstaller image: smaller than the limit of section 14.2, refused when larger, refused when it does not start with the image tag, and an installer image given to the uninstall code is refused.
- The wait of section 16.1: a fake device that disappears and comes back during the wait (the countdown starts again), one that comes back with another address, and three resets in a row (the app stops).
- The sequence around the transfer (section 15.1): the eject fails twice then works, the helper process is paused then resumed, and it is resumed when the transfer fails, when the app is cancelled, and at the next start after a crash (a state file is left behind in the test).
- The failures of each system, from a fake access layer that returns the error codes of section 15 and a table of the messages expected: access denied, busy, no device, unsupported (Windows, no driver), not found at configuration (Windows, old driver), exclusive access (macOS).
- The Windows request mapping: a request becomes a write or a read on the pipe, with the setup packet in front of the data, checked on the bytes of a fake pipe.
- The table of section 16.2: for each line, the state before, the state after and the message, with the three answers present, absent and unknown.
- The retries of section 16.3: a second one-tone error stops the app, a three-tone error removes the offer to install.
- The udev rule file: the test reads it and checks that it matches `05ac` and `1223` and nothing else. On macOS, a CI step checks the USB entitlement in the signed helper.

## 18. Not decided yet

- The USB id in Disk Mode (assumed equal to normal mode, see `models.toml`).
- The exact hold times for reset and DFU mode differ a little between revisions. The app follows the USB bus instead of a timer, so this only changes the text of the help.
- Whether the ROM of the late 2012 revision behaves the same at step 6. All public reports agree, but none was checked by us.
- **Where the fixed blocks of the image come from** (section 8.2), decided: the 16-byte header signature and the 698-byte certificate area are built by CI from the pinned base sources, never stored in the repository. The CI writes them to a folder outside the repository, given to CMake as `TACETOS_DFU_PARTS_DIR`, as `header-signature.bin` and `certificate.bin`. They are inputs of the builder, which only places them in the image.
- **Where the installer and uninstaller payloads come from** (sections 8.3, 8.4, 11), decided: the two programs are built by CI from the pinned base sources, never stored in the repository, and written to the same folder as `installer-payload.bin` and `uninstaller-payload.bin`. The list of Apple's boot image signatures (section 10) is inside them, so a new Apple version needs a new payload. A build without that folder answers "Classic install is not available in this build".
- Whether the TacetOS firmware clears its settings when the hold switch is locked at start, as the studied tools' documentation says of their firmware (section 13.3). If so, the third way to reach Apple's firmware is removed from the help.
- The time of the button check varies between 0.4 s (a timer in the code), 0.65 s (a comment) and 0.8 s (the documentation). The help text says "hold the button as soon as you turn the iPod on" and does not give a time.
- How the app reads the version of Apple's firmware in normal mode (section 3, check 2). It is in a file on the music partition; the detection spec will say which.
- **Whether TacetOS ships its own WinUSB driver package** for `05ac:1223` on Windows (section 15.3). It needs a signed driver package, and the signing is a cost and a process of its own. Until it is decided, the help points to Apple's driver and to a driver tool.
- Whether the new Apple devices app of Windows, and the device agents of recent macOS versions, take the iPod in DFU mode before the helper does, and under which process names (sections 15.3, 15.4). The names in this spec come from the studied tools and from older systems and are not checked by us.
- What Apple's software does when the bootloader is installed and the user updates or restores the iPod with it (section 14.4). Assumed: it writes its own boot image over the slot, and the restore of section 12 is the way back.
- Whether the NOR after an uninstall is identical to the NOR before the install, in the sectors of the boot slot. The program is made to give it back that way and the test of section 17 compares, but no real iPod was compared by us.
- Whether a failed erase at the end of the uninstall (section 14.3, step 7) can be told to the user. The payload has only the tones, and the app cannot hear them, so today it cannot.
- The 10 seconds of section 16.1 and the 18 seconds of section 15.1 come from the studied installer. A measure on a real iPod could shorten them.
