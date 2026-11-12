# tacet-dfu

A console tool for development. `tacet-dfu --scan` lists the iPods that are in DFU mode on the USB bus (`05ac:1223`), and the ones in the wrong mode (section 2 of `docs/specs/bootloader-classic.md`). It is read only: it reads the device descriptors, opens no device and sends nothing.

**Build**: it is built with everything else (`cmake --build build/debug`) and is not installed. Linux only, and only when libusb 1.0 is found (`libusb-1.0-0-dev` on Debian and Ubuntu). The binary is `build/debug/tools/dfu/tacet-dfu`.

**Run**: `tacet-dfu --scan`. Each device is printed with its port, bus, address and id, then one line says whether exactly one iPod is in DFU mode. The exit code is 0 when the scan worked and 1 when libusb failed. It needs no permission, because it never opens a device.
