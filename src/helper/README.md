# helper

The small program `tacet-helper`, started by the app, with a fixed list of commands: read the state of an iPod, read its device sheet, back up the firmware partition, install, update or remove the bootloader, restore a backup, format, convert, talk to an iPod Classic in DFU mode, install the udev rule. It refuses any disk that is not an identified iPod, removable and on USB. **Every disk write goes through it.** The messages, the commands and the refusals are in [`docs/specs/helper.md`](../../docs/specs/helper.md).

On Linux it runs as the user: the disk is opened through udisks2, which asks for the password, and `pkexec` is used once, only to put the udev rule in place. On Windows it runs as an administrator, on macOS as the user.

What is there so far: the udev rule that gives the logged-in user access to an iPod Classic in DFU mode (`05ac:1223`) and to nothing else, `70-tacetos-dfu.rules`.

The skeleton of the program is there too. `tacet-helper` reads one JSON request per line on its standard input, refuses anything that is not exactly in its table of commands (`commands.cpp`), takes a lock per disk (`lock.cpp`), and answers with events on its standard output. The gate of development builds is in `gate.h`. One command is wired so far, `read-state`, on disk image files (a real disk is refused for now). `HelperClient` (`helperclient.h`) is the class of the app that starts the program with a `QProcess`, sends the requests and turns the `log` events into entries of the journal. Still to come: `busy` and `cancel` (they need commands that last), the other commands, and the Linux layer.

For the tests of a cut in the middle of an operation there is a second program, `tacet-helper-killtest`, built only with the tests (`killpoint.h`). It is the same helper with a variable that kills it with SIGKILL at the n-th read, write or flush of a command. The normal `tacet-helper` is built without it and does not read the variable; a test looks for the name in the file.

In the same program, `fullpoint.h` makes the data folder answer "no space left" (the variable `TACETOS_TEST_FULL`, `backup` or `record`), for the tests of a full disk. The normal helper always answers "no fault" there.
