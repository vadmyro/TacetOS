# Backports

Newer commits brought in on top of the pinned base, one at a time. Each one is fetched from the address in `base.lock` when the firmware is built, and applied by `apply.sh` before our own patches. None of their code is stored in this repository. The commit hashes themselves are listed in `base.lock`, in the order they are applied.

| Commit | Model | What it fixes | Status |
|---|---|---|---|
| `94313491ab314634bdca8d727e95b37c983f6142` | iPod Classic | The drive lost power before its write cache was flushed, which could corrupt the filesystem. Now each step of the flush is checked, and power stays on if the flush fails or times out. | Applies cleanly on the base, to be tested on a device |

## Adding one

1. Add the full commit hash to `BACKPORTS` in `base.lock`.
2. Add a row above: the commit, the models it touches, what it changes in plain words.
3. Run `apply.sh`: it must apply without conflict.
4. Build every model it can affect, then test on a device before a release.
