# Backports

Newer commits brought in on top of the pinned base, one at a time. Each one is fetched from the address in `base.lock` when the firmware is built, and applied by `apply.sh` before our own patches. None of their code is stored in this repository. The commit hashes themselves are listed in `base.lock`, in the order they are applied.

| Commit | Model | What it fixes | Status |
|---|---|---|---|
| `94313491ab314634bdca8d727e95b37c983f6142` | iPod Classic | The drive lost power before its write cache was flushed, which could corrupt the filesystem. Now each step of the flush is checked, and power stays on if the flush fails or times out. | Applies cleanly on the base, to be tested on a device |
| `68a5524a48fe08d930c193d843dcd5dcdd4e98a4` | iPod Classic | On the two newer screen types (2 and 3), the deep standby mode used when the screen is off made the device crash a few seconds later. It is no longer used there, so the screen can sleep without a crash, at the cost of a little more power while it is off. The older screen types are not affected. | Applies cleanly on the base, to be tested on a device |
| `9644bb5d9d441597eb28036eabae4c7c1fb9c414` | iPod Classic | A CE-ATA drive always has sectors of 4 KB at least, and its size was read wrongly. Both 512 byte and 4 KB sectors are now supported and read correctly, so such drives work. | Applies cleanly on the base, to be tested on a device |
| `6340e79cda14d665fc0a64171fb27cc39ee92ffa` | iPod Classic | Drives of the CE-ATA kind (some replacement drives and adapters) do not always report their power saving features, though they always support them. The drive is now put to sleep and its cache flushed on those drives too, so they use less power when idle. | Applies cleanly on the base, to be tested on a device |
| `b217a550598e2fc7fbb76924cb6758b18bfeff83` | iPod Classic | The play/pause and volume buttons of the inline remote on the earphone cable did nothing. They are now read from the remote's controller and sent as media keys, so they work on every screen. | Applies cleanly on the base, to be tested on a device |
| `ad6cc2f099bd8877a335b47bc150e14067adabfe` | iPod Classic | The first iPod Classic models (80 GB and 160 GB "fat", and the 120 GB) can only address 128 GiB with their Apple firmware, which wrecks the partition table and the filesystem of a larger drive. The bootloader now checks whether the installed drive needs large disk addressing (LBA48) and whether the flashed Apple firmware supports it. If not, it refuses to start the Apple firmware, disk mode included. Holding Left overrides the check. The later 160 GB "thin" model is not affected. | Applies cleanly on the base, to be tested on a device |

## Adding one

1. Add the full commit hash to `BACKPORTS` in `base.lock`.
2. Add a row above: the commit, the models it touches, what it changes in plain words.
3. Run `apply.sh`: it must apply without conflict. A conflict in a file listed in `BACKPORT_LOOSE_FILES` (`base.lock`) does not count: the change to that file is dropped and the rest of the commit is kept.
4. Build every model it can affect, then test on a device before a release.
