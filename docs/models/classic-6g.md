# iPod Classic (6th generation)

The iPod Classic is the last hard disk iPod, sold by Apple from 2007 to 2014 in three revisions (80, 120 and 160 GB), and it belongs to the `classic` family.

## At a glance

| | |
|---|---|
| **Id** | `classic-6g` |
| **Family** | classic |
| **Screen** | 320 x 240, 16 bits, colour, 2.5 inches |
| **Storage** | hdd, 1.8in, 80, 120, 160 GB |
| **Install method** | dfu |
| **Dual boot required** | yes |
| **Bootloader target** | `ipod6g` |
| **Support** | experimental |

## How to recognise it

It has a click wheel under a 2.5 inch colour screen, an aluminium front face and a stainless steel back. It is thicker than the Nano. The screen shows cover art and a menu with a coloured side panel. The storage is a 1.8 inch hard disk, not flash. If you are not sure, the model number is printed on the back, under the capacity line.

## USB ids

| Mode | USB id | When you see it |
|---|---|---|
| Normal | `05ac:1261` | Plugged in, idle or syncing |
| Disk Mode | `05ac:1261` | Same id as normal. How to enter it on this model will be written after a test on real hardware |
| DFU | `05ac:1223` | Low-level mode used for the install. The screen stays black |

The Disk Mode id is the one fact here with a single source, see "Sources".

## Installing TacetOS

1. Connect the iPod and let the app detect it.
2. The app asks for a dual boot install. This model only allows dual boot: the original firmware stays on the iPod and TacetOS is the second system.
3. Put the iPod in DFU mode when the app asks. The app waits for the DFU id above.
4. The app sends the bootloader over USB. All disk writes go through the helper, and development builds refuse to write to a real iPod.
5. The iPod restarts. Starting it normally runs TacetOS, and the original firmware stays reachable (the exact key combination will be written here after the first test on real hardware).

To go back to the original firmware only, use the uninstall option of the app (planned). It restores the original boot path.

## What is tested

Nothing has been run on a real iPod Classic yet. This model stays marked experimental until a real test is recorded here, with the date and the person who ran it.

## Known issues

None known. Not tested on real hardware.

## Sources

- `usb-ids`: Public USB id database (usb.ids). https://www.linux-usb.org/usb.ids (read in the copy shipped by the hwdata package of the build machine). Checked 2026-11-08.
- `fw-config`: Device configuration header of the firmware base, one per target. firmware/export/config/<target>.h in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `fw-manual`: Manual pages of the firmware base: platform page per model and install chapter. manual/platform/<target>.tex and the install chapters in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `installer-devices`: Device list of the graphical installer shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `dfu-pid-table`: Table of known DFU product ids in the DFU tool shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.

Facts still in `to_verify`:

- `usb_disk_mode`: only the USB id database was read. Needs a real iPod in Disk Mode, or a second source.
- `storage`: only the device header was read. Needs the capacities from Apple's specification sheets.
