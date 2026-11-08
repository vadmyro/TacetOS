# firmware

The TacetOS firmware is **not a diverging fork**: it is a short stack of patches on top of a pinned base.

| File | Role |
|---|---|
| `base.lock` | The base sources and the exact commit we build on |
| `patches/` | Our patches, applied in order |
| `backports.md` | Newer features brought in one at a time |
| `apply.sh` | Downloads the base at the locked commit and applies the patches |
| `build.sh` | Builds the firmware, bootloader or simulator for one model |

Firmware builds run in CI.
