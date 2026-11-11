# app

The Qt 6 desktop app: main window, install wizard, library, settings, themes.

## Style

The colours are in `style/palette.cpp` and nowhere else. The app follows the light or dark look of the system.

The font is Adwaita Sans (SIL Open Font License 1.1). The file is never stored in this repository. CMake looks for `AdwaitaSans-Regular.ttf` in the font folders of the system and embeds it when found. With `-DTACETOS_FETCH_FONT=ON` it downloads a fixed version with a checked hash into the build folder (the CI does). Without the file, the app uses the font of the system.

## Home screen

`home/` holds the home screen: `HomeScreen` (header, one `IpodCard` per iPod, empty state) and its card. They show the list they are given and send `actionRequested` and `refreshRequested`; the wiring to the device code is in the main window. The look of the dots, banners and buttons comes from the roles in `style/palette.cpp`, never from a colour written in `home/`.

## Wiring

`MainWindow` puts the home screen in the middle and builds the device part: the Linux reader, the live tracker and driver (with the udev and mount watchers when they exist), the operation runner with the real disk actions, the state model, and the journal, written to `logs/` in the data folder of the app. The list of models is embedded in the app. On a system without detection the screen says so. A second constructor takes the source, the clock, the disk actions, the executor and the function that opens a folder, so the tests never touch the real system.
