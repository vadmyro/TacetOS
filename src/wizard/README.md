# wizard

The logic of the install wizard: the steps of an install and an uninstall, run one after another, and the file that lets a flow start again after a cut. Qt Core only, no user interface: the screens are in `app/wizard/`. The rules are in `docs/specs/install-wizard.md`.

- `stepsFor()`: the list of steps of a flow, from its family, its kind, its path and whether the iPod is in Mac format (section 2.3). A pure function.
- `Progress` and `ProgressStore`: the progress file of one iPod, `wizard/<serial>.json` in the data folder, written in one move, and refused when it cannot be trusted (section 3).
- `WizardRunner`: runs the steps on a worker thread, writes the progress file before and after each step, waits for the user when a step asks, and resumes a flow at the right step. One flow per iPod, guarded by a lock file (section 3.5).
- `Step` and `StepContext`: what a step is given. The real steps come with their own sections of the spec.
- `diagnose()`: the checks of the computer and the iPod, in their order (section 4), a pure function. `DiagnoseStep` gathers them through `DiagnoseSources`, one interface for every source, and asks the battery question. `readOsRelease()` and `folderInfo()` are the real readers of the system release file and of the backup folder; the other sources are given by the app.
- `BackupStep`: the steps `backup-full`, `backup-keep` and `backup-settings` (section 5). It copies the files of the volume through the file system into `<folder>/<serial>/<date>/volume/`, checks each copy, keeps `manifest.partial` so that a cut run goes on, has the firmware partition copied by the helper (`FirmwarePartition`, the real one is `HelperFirmwarePartition`), then writes `manifest.json` last. `restoreFromBackup()` puts a `keep` or `settings` backup back in one click, every sha256 checked before anything is written.
