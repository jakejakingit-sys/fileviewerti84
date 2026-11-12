# FILEVIEW — TI-84 Plus CE

FILEVIEW is a three-tab viewer for calculator content: **TEXT**, **IMAGES**, and **GIFS**. It is a CEdev C program named `FILEVIEW` (eight characters, the TI variable-name limit) and displays `FILEVIEWER` in its interface. On newer CE operating systems, arTIfiCE is the launcher that permits assembly/C programs to run; it is not the source-code editor.

## What it can open

- **TEXT:** TI string variables, TI-BASIC program variables, and plain-text AppVars prepared with the `FVTX` header.
- **IMAGES:** native `Image0`–`Image9` background-image variables and `Pic0`–`Pic9` picture variables. Image Vars open as a pixel preview. Pic Vars are detected and selectable, but this first version currently shows an in-app note instead of rendering the Pic bitmap.
- **GIFS:** animation AppVars made with `tools/gif_to_8xv.py`. The calculator does not store animated GIF files natively; the helper converts one to a compact frame-pack AppVar that FILEVIEW can play.

GIF packs use 64×48 RGB332 frames, at most 20 frames per AppVar, and a 1–25 frame delay. To convert one, install Pillow on your computer and run:

```text
python tools/gif_to_8xv.py animation.gif
```

Send the generated `.8xv` to the calculator with TI Connect CE. The output AppVar name is based on the GIF filename and limited to eight characters. The GIF category lists those frame packs.

## Controls

- **Left / Right:** switch TEXT, IMAGES, GIFS tabs
- **Up / Down:** select an item
- **Enter:** open selected item
- **Clear:** return to the list or quit FILEVIEWER
- In a GIF: **Left / Right** change frame; playback continues automatically

## Build

Upload this folder's contents to a GitHub repository and run **Actions → FILEVIEW 8XP build**. Download `FILEVIEW-8XP`, then transfer `FILEVIEW.8xp` to the calculator. The workflow enforces the 75 KiB program limit.

The TI-84 file formats are quite different from PC files. The operating system exposes Image Vars at 83×133 pixels in 16-bit color; FILEVIEW draws these in its viewer. GIF playback requires the provided conversion step; a raw `.gif` cannot simply be copied to the calculator and played as-is. FILEVIEW does not modify or delete calculator variables.
