# update

The firmware update channel (`docs/specs/update-channel.md`): versions and their order, the index of a release, the address of the channel and the rules of each read. `UpdateCheck` reads the index only and says whether the channel has a newer TacetOS for one model. The download and the update itself belong to the wizard (`src/wizard/download.h`, flow `update`).
