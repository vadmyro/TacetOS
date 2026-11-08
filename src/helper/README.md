# helper

A small privileged program (pkexec on Linux, UAC on Windows, authorization on macOS) with a fixed list of commands: format, convert, install or remove the bootloader, back up the firmware partition, install the udev rule. It refuses any disk that is not an identified iPod. **Every disk write goes through it.**
