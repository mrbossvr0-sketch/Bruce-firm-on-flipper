# Bruce Lite for Flipper Zero — build-ready source

This is a **Bruce-inspired Flipper Zero launcher demo**, not the full Bruce firmware.
It is an external Flipper app (`.fap`), not a replacement firmware image.

## What this app currently does

- Flipper-native menu: Infrared, Sub-GHz, NFC, Wi-Fi, Lucky Tap Game, About
- Navigation using the Flipper buttons
- Infrared / Sub-GHz / NFC info screens: **no hardware operation implemented**
- Wi-Fi: **no scan or network operation**; external ESP32 is required for future Wi-Fi functionality
- Lucky Tap: selecting it increments a score internally (currently a simple demo)

## Get the compiled `.fap` with GitHub Actions (no PC compiler needed)

1. Create an empty GitHub repository.
2. Upload the **contents** of this directory to the repository root, including the hidden `.github` directory. Do not upload the enclosing `BruceLiteFlipper` folder itself.
3. Open the repository **Actions** tab and choose **Build Bruce Lite FAP**. Click **Run workflow** or open the run started by your first push.
4. When the run finishes successfully, open the run and download its **BruceLite-...** artifact ZIP.
5. Extract `bruce_lite.fap`; copy it to `SD Card/apps/Tools/` on your Flipper Zero using qFlipper or a microSD reader.

This workflow uses Flipper's official uFBT GitHub Action and the **release** SDK.

## Compile locally

Install Python 3 and Git. In this folder on Linux/macOS, run:

```sh
sh build.sh
```

On Windows, open a command prompt in this folder and run:

```bat
build.bat
```

Or manually:

```sh
python -m pip install --upgrade ufbt
ufbt update --channel release
ufbt
```

Find `bruce_lite.fap` in the `dist/` directory after a successful build.

## Compatibility and limitations

The `.fap` has to be built for a compatible firmware SDK/API. This project is
**not compiled or tested** in this package because the current environment
cannot download the uFBT toolchain. A completed GitHub Actions build is necessary
to produce a usable binary. No precompiled FAP is included or claimed.

Source files: `application.fam` and `bruce_lite.c`.

Use radio/NFC features only on your own equipment or when you have permission.
