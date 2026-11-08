# iPod Nano (1st generation)

The first iPod Nano is a flash player sold by Apple from 2005 to 2006 in 1, 2 and 4 GB versions, and it belongs to the `nano` family.

## At a glance

| | |
|---|---|
| **Id** | `nano-1g` |
| **Family** | nano |
| **Screen** | 176 x 132, 16 bits, colour |
| **Storage** | flash, chip, 1, 2, 4 GB |
| **Install method** | partition |
| **Dual boot required** | no |
| **Bootloader target** | `ipodnano1g` |
| **Support** | experimental |

## How to recognise it

It is a very thin player with a click wheel and a small colour screen, with a white or black front and a polished metal back. It is much smaller than the Video. The 2nd generation Nano looks similar but has an aluminium case in several colours, so check the back and the colour before you pick this model.

## USB ids

| Mode | USB id | When you see it |
|---|---|---|
| Normal | `05ac:120a` | Plugged in, idle or syncing |
| Disk Mode | `05ac:120a` | Same id as normal. How to enter it on this model will be written after a test on real hardware |
| DFU | not available | This model has no DFU mode for the install |

## Installing TacetOS

1. Connect the iPod in Disk Mode and let the app detect it.
2. The app writes the bootloader into the firmware partition, through the helper. The flash is presented to the computer like a small drive. Development builds refuse to write to a real iPod.
3. The app copies the TacetOS files to the music partition and ejects the iPod.
4. The install is not dual boot only: the original firmware stays on the iPod (the exact key combination to start it will be written here after the first test on real hardware).

To go back, use the uninstall option of the app (planned).

## What is tested

Nothing has been run on a real Nano 1G yet. This model stays marked experimental until a real test is recorded here.

## Known issues

None known. Not tested on real hardware. The firmware header notes that disk DMA is switched off on this model because of reported instabilities, so transfers may be slower than on the Video.

## Sources

- `usb-ids`: Public USB id database (usb.ids). https://www.linux-usb.org/usb.ids (read in the copy shipped by the hwdata package of the build machine). Checked 2026-11-08.
- `fw-config`: Device configuration header of the firmware base, one per target. firmware/export/config/<target>.h in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `fw-manual`: Manual pages of the firmware base: platform page per model and install chapter. manual/platform/<target>.tex and the install chapters in the firmware base (see firmware/base.lock). Checked 2026-11-08.
- `installer-devices`: Device list of the graphical installer shipped with the firmware base. utils/ folder of the firmware base (see firmware/base.lock). Checked 2026-11-08.

Facts still in `to_verify`:

- `usb_disk_mode`: the id database lists one id for the model, so Disk Mode is assumed to share it. Needs a real iPod in Disk Mode.
- `storage`: the capacities come from general knowledge. Needs Apple's specification sheets.
