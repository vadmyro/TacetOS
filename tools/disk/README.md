# tacet-disk

A console tool for development. It mounts, unmounts or ejects the iPod that is plugged into this computer, through the disk service of the desktop (udisks2), the same way the app does. It never runs as root and never calls `sudo`: the service asks for the permission itself when it needs it.

**Build**: it is built with everything else (`cmake --build build/debug`) and is not installed. Linux only, and only when Qt has its D-Bus part. The binary is `build/debug/tools/disk/tacet-disk`.

**Run**: `tacet-disk mount [--ro]`, `tacet-disk unmount` or `tacet-disk eject`. Run it in a real terminal on a desktop session, because it talks to the system D-Bus.

**Which iPod**: the one found by the live detection (normal mode only). When several iPods are plugged in, give `--port <port>` (the port is the one `tacet-watch` prints, for example `1-2`); without it the tool lists them and stops. It only acts on a disk under an Apple USB device, never on any other disk.

**Output**: the iPod and its disk, then the mount point (`mount`), or the number of volumes unmounted (`unmount`, `eject`). On a failure it prints the short message of the app and, on the next line, the detail given by the service, and the exit code is 1.
