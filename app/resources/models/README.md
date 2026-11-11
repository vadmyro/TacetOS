# Model drawings

One flat drawing per iPod model, shown on the home screen. They are **stylised, not to scale**: the proportions are close, the details are ours.

## Files

- `<id>.svg`: the drawing of the model with that `id` in `models/models.toml`.
- `generic.svg`: neutral grey with a dashed outline, for an iPod the list does not know.
- The files are embedded in the app and read as `:/models/<id>.svg`. They are listed in `app/CMakeLists.txt`.

## Style

- Same style for all: soft rounded outline, a screen, a wheel and a centre button. Flat colours, no gradient, no shadow.
- The `viewBox` always starts at `0 0` and is **200 high**. The width follows the model (120 for the full-size line, 111 for the Mini, 90 for the Nano).
- Colour of the most common face: white, black for the Video, silver for the Classic and the Minis.
- The screen is pale for a monochrome display and blue for a colour one.
- The 1st and 2nd generation wheel has notches and four buttons around it (it turns). The 3rd generation has four buttons in a row above the wheel. The others have a click wheel with four dots.
- No text, no picture, no logo, and no part of a photo or of a drawing made by someone else.

## Format

SVG Tiny 1.2 only, which is what `QSvgRenderer` reads: shapes (`rect`, `circle`, `line`, `path`) with presentation attributes (`fill`, `stroke`, `stroke-width`). No `style` attribute, no CSS, no filter, no `text`, no `image`. The root element carries `baseProfile="tiny"`.

## Add a model

1. Add the model to `models/models.toml`.
2. Draw `<id>.svg` in this folder, with the same `viewBox` height as the others.
3. List it in `app/CMakeLists.txt`.
4. Run `tst_drawings`: it checks that every `id` has a valid drawing, a `viewBox`, no forbidden element, and that no drawing is left without a model.
