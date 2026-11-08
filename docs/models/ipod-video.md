# iPod Video (5th generation)

The iPod Video is the first full-size iPod that plays video, sold by Apple from 2005 to 2007 in 30, 60 and 80 GB versions, and it belongs to the `ipod` family.

## At a glance

| | |
|---|---|
| **Id** | `ipod-video` |
| **Family** | ipod |
| **Screen** | 320 x 240, 16 bits, colour |
| **Storage** | hdd, 1.8in, 30, 60, 80 GB |
| **Install method** | partition |
| **Dual boot required** | no |
| **Bootloader target** | `ipodvideo` |
| **Support** | experimental |

## How to recognise it

It has a click wheel with the four buttons printed on the wheel itself, under a colour screen. The front is glossy white or black plastic and the back is polished metal. The screen is 320 x 240 pixels, larger and sharper than the 4th generation Color. The storage is a 1.8 inch hard disk. The model number is printed on the back.

## USB ids

| Mode | USB id | When you see it |
|---|---|---|
| Normal | `05ac:1209` | Plugged in, idle or syncing |
| Disk Mode | `05ac:1209` | Same id as normal. How to enter it on this model will be written after a test on real hardware |
| DFU | not available | This model has no DFU mode for the install |

## Installing TacetOS

1. Connect the iPod in Disk Mode and let the app detect it.
2. The app writes the bootloader into the firmware partition of the disk, through the helper. Development builds refuse to write to a real iPod.
3. The app copies the TacetOS files to the music partition and ejects the iPod.
4. The install is not dual boot only: the original firmware stays on the iPod and can be started instead of TacetOS (the exact key combination will be written here after the first test on real hardware).

To go back, use the uninstall option of the app (planned), which removes the bootloader and puts the original boot path back.

## What is tested

Nothing has been run on a real iPod Video yet. This model stays marked experimental until a real test is recorded here.

## Known issues

None known. Not tested on real hardware.

## Sources

- `usb-ids`: Public USB id database (usb.ids). https://www.linux-usb.org/usb.ids (read in the copy shipped by the hwdata package of the build machine). Checked 2026-11-08.
- `fw-config`: Device configuration header of the firmware base, one per target. firmware/export/config/<target>.h in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `fw-manual`: Manual pages of the firmware base: platform page per model and install chapter. manual/platform/<target>.tex and the install chapters in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `installer-devices`: Device list of the graphical installer shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.

Facts still in `to_verify`:

- `usb_disk_mode`: the id database lists one id for the model, so Disk Mode is assumed to share it. Needs a real iPod in Disk Mode.
- `storage`: the capacities come from general knowledge. Needs Apple's specification sheets.
