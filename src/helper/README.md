# helper

The small program `tacet-helper`, started by the app, with a fixed list of commands: read the state of an iPod, read its device sheet, back up the firmware partition, install, update or remove the bootloader, restore a backup, format, convert, talk to an iPod Classic in DFU mode, install the udev rule. It refuses any disk that is not an identified iPod, removable and on USB. **Every disk write goes through it.** The messages, the commands and the refusals are in [`docs/specs/helper.md`](../../docs/specs/helper.md).

On Linux it runs as the user: the disk is opened through udisks2, which asks for the password, and `pkexec` is used once, only to put the udev rule in place. On Windows it runs as an administrator, on macOS as the user.

What is there so far: the udev rule that gives the logged-in user access to an iPod Classic in DFU mode (`05ac:1223`) and to nothing else, `70-tacetos-dfu.rules`.
