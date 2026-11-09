# tacet-watch

A console tool for development. It watches the iPods plugged into this computer and prints a line, with the time, each time one is added, changed or removed. It only reads: it never writes to a disk and never needs root.

**Build**: it is built with everything else (`cmake --build build/debug`) and is not installed. Linux only. The binary is `build/debug/tools/watch/tacet-watch`.

**Run**: `tacet-watch`, then plug or unplug an iPod. Press Ctrl+C to quit. The iPods that are already plugged in are printed first. Use `--models <file>` to read another model list than `models/models.toml`.

**What it uses**: the same live detection as the app (`src/device`): the Linux readers, the live tracker and its driver, with the udev monitor and the mount table watch when they are available. Without udev it polls, and says so at start.

**Output**: one line per event, `HH:mm:ss`, the event (`added`, `changed`, `removed`), the USB port, then what is known about the iPod: model, USB id, mode, state message, firmware line (once a volume is mounted), disk and mount point.
