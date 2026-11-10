# Display Customization

[← Back to README](../README.md)

## General: Splash Screen (1.3)

By default, sd2psXtd comes with a special splash screen resembling the project's logo.

You can customize this by going to the splashgen page on the project website!

From there, you can customize a splash screen to your needs. Once you are happy with the result displayed in the preview, press the **Download UF2** button. This will generate a UF2 file containing your splash screen.

You can flash this splash screen just like any other firmware update.

Alternatively, place a generated `splash.bin` in the root of the SD card. On startup, the firmware imports it into flash and removes `splash.bin` from the SD card.

The flashed splash screen is maintained after a firmware update, so you probably only need to upload it once.

*Note: When combining the splash screen with a firmware UF2 (such as for mass production or flashing multiple sd2psXtd with the same splash and firmware combination), it's strongly recommended to flash the combined image with `picotool`, since uploading using the usual firmware update procedure often does not work. To do so, please install `picotool` and run:*

```sh
picotool load <combined_file_name>.uf2
```

*while having the sd2psx to be updated connected in bootloader mode.*

## General: Game Image Screen (1.3)

Add a splash image that the device shows in the card main view. Use the splashgen page on the project website to convert a source image to the device `.bin` format, then place the generated file in the card folder on your SD card.

Naming and behavior:

- Folder-level splash (default for the folder):
    - Path: `MemoryCards/<Variant>/<card_folder>/<card_folder>.bin`
    - Example: `MemoryCards/<Variant>/SuperGame/SuperGame.bin` — used when no channel-specific image exists.
- Channel-specific splash (overrides folder-level for that channel):
    - Path: `MemoryCards/<Variant>/<card_folder>/<card_folder>-<channel_number>.bin`
    - Example: `MemoryCards/<Variant>/SuperGame/SuperGame-1.bin` — shown only for channel 1 of that card folder.
- Fallback rules:
    - If a channel-specific file exists it is used.
    - Otherwise the folder-level `<card_folder>.bin` is used.
    - If neither exists, no splash is shown.

Practical notes
- `<channel_number>` matches the on-device channel index (1..N).
- Filenames must match exactly; FAT SD cards are usually case-insensitive but keep names consistent.
- Use [`misc/splashgen/splashgen.html`](../misc/splashgen/splashgen.html) to produce correctly-sized and packed `.bin` files for the OLED.
- Keep names short — very long filenames may cause display or performance issues.
- Store splash `.bin` files alongside that card's `CardX.ini` and save data in the same folder.
