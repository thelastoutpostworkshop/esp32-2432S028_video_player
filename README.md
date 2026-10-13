# Cheap Yellow Display Video Player (ESP32-2432S028)

<a href="https://www.buymeacoffee.com/thelastoutpostworkshop" target="_blank">
<img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee">
</a>

## Youtube Tutorial
> Warning: Make sure you have the board model ESP32-2432S028 with the **ILI9341 Display controller** - not the ST7789 (parallel) controller, which is not supported by the graphic library [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX)

[<img src="https://github.com/thelastoutpostworkshop/images/blob/main/Cheay%20Yellow%20Display-3.png" width="500">](https://youtu.be/jYcxUgxz9ks)

## Project Description
This project turns the ESP32-2432S028 "Cheap Yellow Display" (2.8" or 2.4") with the **ILI9341** controller into a standalone SD-card video player. The sketch scans a `/mjpeg` folder on the SD card, loads Motion JPEG (`.mjpeg`) files, and plays them back full-screen on the built-in 240x320 display. It is designed for simple offline playback with minimal setup: copy converted MJPEG files to the card, power the board, and the videos play in sequence on a loop.

## Features
- Plays `.mjpeg` videos directly from the SD card on the ESP32-2432S028
- Automatically scans the `/mjpeg` folder and builds a playlist from the files it finds
- Loops through all detected videos continuously
- Lets the user skip to the next video with the onboard `BOOT` button
- Uses the Arduino GFX and JPEGDEC libraries for MJPEG decoding and display output
- Initializes the display and SD card independently for reliable playback on the CYD hardware
- Shows centered startup messages when the SD card is unavailable or no video files are found
- Prints playback statistics to the Serial Monitor, including frame count, average FPS, timing breakdown, and detected video size
- Includes sample SD card content and a simple Video Conversion Studio workflow for preparing compatible video files

## What You Can Customize
- `DISPLAY_SPI_SPEED`: switch between `80000000L` and `40000000L` if your display is unstable at 80 MHz
- `BL_PIN`: change the display backlight pin for board variants that use a different backlight connection
- `BOOT_PIN` and `BOOT_BUTTON_DEBOUCE_TIME`: remap or adjust the skip button behavior
- `SD_CS`, `SD_MISO`, `SD_MOSI`, `SD_SCK`, and `SD_SPI_SPEED`: adapt SD card wiring or bus speed if needed
- `ENABLE_SD_DIAGNOSTICS`: enabled by default; set to `false` to disable detailed startup SD diagnostics
- `MJPEG_FOLDER`: point the sketch to a different folder name on the SD card
- `MAX_FILES`: increase or decrease the maximum number of videos loaded into the playlist
- `gfx->setRotation(0)`: change screen orientation to match your enclosure or video layout
- `gfx->invertDisplay(true)`: enable inversion for CYD variants that require it
- Video settings such as orientation, resolution, quality, frame rate, and brightness can be adjusted in Video Conversion Studio before copying files to the SD card

## Notes
> Some model of Cheap Yellow Display works only at speed of 40Mhz, change the DISPLAY_SPI_SPEED to 40000000L:
```cmd
#define DISPLAY_SPI_SPEED 40000000L // 40MHz
```

## SD Card Diagnostics

If the card is missing or cannot be mounted, the display shows
"SD card not found" with instructions to insert/check the card and reset.
An SD SPI initialization failure shows "SD card unavailable" instead.
If `/mjpeg` is missing, cannot be opened, is not a directory, or contains
no non-empty `.mjpeg` files, the display shows "No playable files" and asks
the user to verify `.mjpeg` files in the SD card's `/mjpeg` folder.
All messages are centered horizontally and vertically using the existing
Arduino GFX library's built-in font; no additional graphics library is used.
Playback remains idle and the message stays visible until the device is reset.
The startup scan does not validate JPEG decoding; corrupt videos can still
produce playback errors.

The display uses an explicit `SPIClass(HSPI)` passed to `Arduino_HWSPI`;
the SD card uses `SPIClass(VSPI)`. Do not remove the display's SPI argument:
`Arduino_HWSPI` defaults to the global `SPI` object, which also uses VSPI
on the ESP32. Different pins and separate SPI objects do not isolate
devices assigned to the same hardware host. This conflict can let the
startup SD scan succeed, then break SD reads when playback writes to
the display.

Copy the contents of [SD Content](./SD%20Content/) to the card root so that
the card contains `mjpeg/example.mjpeg`, not `SD Content/mjpeg/example.mjpeg`
or `sd/mjpeg/example.mjpeg`. Insert the card before powering on or resetting
the device; the sketch mounts and scans the card only during startup.

`SD.begin(..., "/sd")` sets the ESP32's internal filesystem mount point.
Arduino's `SD.open()` and `SD.exists()` add that mount point automatically,
so `MJPEG_FOLDER` must be `"/mjpeg"` and file paths must be
`"/mjpeg/example.mjpeg"`. Using `"/sd/mjpeg"` makes the library look for
`"/sd/sd/mjpeg"` internally and fails unless the card has a nested `sd` folder.

If the Serial Monitor shows files being found in `/mjpeg` but playback fails with a message like this:

```cmd
ERROR: Failed to open /mjpeg/example.mjpeg file for reading
```

SD diagnostics are currently enabled in the sketch:

```cpp
#define ENABLE_SD_DIAGNOSTICS true
```

The current SD card SPI speed is 20 MHz:

```cpp
#define SD_SPI_SPEED 20000000L
```

For troubleshooting unreliable reads, you can lower it further:

```cpp
#define SD_SPI_SPEED 4000000L
```

Upload the sketch again, open the Serial Monitor at `115200`, and copy the output from:

```cmd
SD diagnostics start
```

through:

```cmd
SD diagnostics end
```

The diagnostics print the SD card type and size, list the `/mjpeg` folder
entries, and read the first real `.mjpeg` file from the directory scan.
They then write to the display before reopening and reading that file
by full path. Both read tests should report `read=4` with the JPEG start
bytes `FF D8`. Files prefixed with `._` are macOS metadata and are excluded
from playback.

## Convert Videos with Video Conversion Studio

<p align="center">
  <a href="https://thelastoutpostworkshop.github.io/video_conversion/">
    <img src="https://img.shields.io/badge/Open-Video%20Conversion%20Studio-FF6A00?style=for-the-badge" alt="Open Video Conversion Studio">
  </a>
</p>

<p align="center">
  FFmpeg is no longer necessary. There are no complex commands to learn. Just use <a href="https://thelastoutpostworkshop.github.io/video_conversion/"><strong>Video Conversion Studio</strong></a> to prepare MJPEG files for this player.
</p>
