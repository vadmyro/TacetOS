# iPod Nano (2nd generation)

The second iPod Nano is an aluminium flash player sold by Apple from 2006 to 2007 in 2, 4 and 8 GB versions, and it belongs to the `nano` family.

## At a glance

| | |
|---|---|
| **Id** | `nano-2g` |
| **Family** | nano |
| **Screen** | 176 x 132, 16 bits, colour |
| **Storage** | flash, chip, 2, 4, 8 GB (raw NAND) |
| **Install method** | partition |
| **Dual boot required** | no |
| **Bootloader target** | `ipodnano2g` |
| **Support** | experimental |

## How to recognise it

It has an aluminium case sold in several colours, a click wheel and a small colour screen. The 1st generation Nano is plastic and white or black, and the 3rd generation Nano (not supported) is short and wide with a bigger screen.

## USB ids

| Mode | USB id | When you see it |
|---|---|---|
| Normal | `05ac:1260` | Plugged in, idle or syncing |
| Disk Mode | `05ac:1260` | Same id as normal. How to enter it on this model will be written after a test on real hardware |
| DFU | `05ac:1220`, `05ac:1240` | Low-level mode. The sources disagree on which id is which, see below. Not used by the install |

## Installing TacetOS

1. Connect the iPod in Disk Mode and let the app detect it.
2. The app installs the bootloader through the disk, in the firmware partition, through the helper. This model uses its own bootloader file format, which the app handles. Development builds refuse to write to a real iPod.
3. The app copies the TacetOS files to the music partition and ejects the iPod.
4. The install is not dual boot only: the original firmware stays on the iPod (the exact key combination to start it will be written here after the first test on real hardware).

To go back, use the uninstall option of the app (planned).

## What is tested

Nothing has been run on a real Nano 2G yet. This model stays marked experimental until a real test is recorded here.

## Known issues

None known. Not tested on real hardware. The storage is raw NAND flash (not seen as an ATA drive like on the Nano 1G), so this model is the least similar to the other disk-style installs and needs a careful first test.

## Sources

- `usb-ids`: Public USB id database (usb.ids). https://www.linux-usb.org/usb.ids (read in the copy shipped by the hwdata package of the build machine). Checked 2026-11-08.
- `fw-config`: Device configuration header of the firmware base, one per target. firmware/export/config/<target>.h in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `fw-manual`: Manual pages of the firmware base: platform page per model and install chapter. manual/platform/<target>.tex and the install chapters in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `installer-devices`: Device list of the graphical installer shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `dfu-pid-table`: Table of known DFU product ids in the DFU tool shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.

Facts still in `to_verify`:

- `usb_disk_mode`: the id database lists one id for the model, so Disk Mode is assumed to share it. Needs a real iPod in Disk Mode.
- `usb_dfu`: the id database calls `05ac:1240` "DFU mode", while the DFU tool calls it "WTF mode" and lists `05ac:1220` as the DFU id. Both ids are kept. Needs a real iPod in each low-level mode.
- `storage`: the capacities come from general knowledge. Needs Apple's specification sheets.
