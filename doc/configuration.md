# Configuration

[← Back to README](../README.md)

## SD Card Layouts

SD cards can use a GPT partition table or a superfloppy layout, with the filesystem directly on the card and no partition table. Both layouts have been tested on hardware.

## General: Settings File

*sd2psXtd* generates a settings file (`.sd2psx/settings.ini`) that allows you to edit some settings through your computer. This is useful when using one SD card with multiple *sd2psx* devices or *MMCE* devices without a display to change settings.

A settings file has the following format:

```ini
[General]
Mode=PS2
FlippedScreen=OFF
MCA=OFF
[PS1]
Autoboot=ON
GameID=ON
EnableControllerCombo=ON
MaxCardIdx=0
MaxChannels=8
[PS2]
Autoboot=ON
GameID=ON
CardSize=16
Variant=RETAIL
MaxCardIdx=0
MaxChannels=8
```

Possible values are:

| Setting       | Values                                |
|---------------|---------------------------------------|
| Mode          | `PS1`, `PS2`                          |
| AutoBoot      | `OFF`, `ON`                           |
| GameID        | `OFF`, `ON`                           |
| CardSize      | `1`, `2`, `4`, `8`, `16`, `32`, `64`, `128` |
| Variant       | `RETAIL`, `PROTO`, `ARCADE`, `CONQUEST`     |
| FlippedScreen | `ON`, `OFF`                           |
| MCA           | `ON`, `OFF`                           |
| EnableControllerCombo | `ON`, `OFF`                   |
| MaxCardIdx    | `1`–`255`; `0` means unlimited        |
| MaxChannels   | `1`–`255`                             |

`MCA=ON` selects the PS3 MCA USB profile; `MCA=OFF` selects Serial. Mass storage is temporary and is selected from the [USB Mode menu](usb.md).

`MaxCardIdx` limits the numbered cards and `MaxChannels` sets the default number of channels per card. Set these separately in the `[PS1]` and `[PS2]` sections. The default channel limit is 8, and a per-card `MaxChannels` setting overrides it. `MaxCardIdx=0` is the default unlimited setting; the INI parser accepts positive values, so entering `0` does not reset an existing limit.

*Note: Make sure there is an empty line at the end of the ini file.*

## General: Per Card Config

There are some configuration values that can be modified on a per card base within a config file named  `CardX.ini` in a card folder, where `X` is the card index.

*Note 1: The `CardSize` setting is only used for PS2 cards and can be `1`, `2`, `4`, `8`, `16`, `32`, `64`, or `128` MB.*
*Note 2: The BOOT folder should contain a file named `BootCard.ini`*
*Note 3: Make sure there is an empty line at the end of the ini file.*

`MaxChannels` accepts values from `1` to `255` and overrides the default for the current PS1 or PS2 mode.

```ini
[ChannelName]
1=Channel 1 Name
2=Channel 2 Name
3=Channel 3 Name
4=Channel 4 Name
5=Channel 5 Name
6=Channel 6 Name
7=Channel 7 Name
8=Channel 8 Name
[Settings]
MaxChannels=8
CardSize=8
```


## General: Template Cards

By default, a newly created memory card is empty but formatted. If you want new cards to start with some content, such as a system configuration save, icons, or a tool you always need, you can provide a template image. *sd2psXtd* then creates every new card as a copy of that template.

Place the template in the `.sd2psx` folder on the root of the SD card:

| Mode | Template file      | Required size                                          |
|------|--------------------|--------------------------------------------------------|
| PS1  | `.sd2psx/ps1.mcd`  | exactly 128 KB (131072 bytes)                          |
| PS2  | `.sd2psx/ps2.mcd`  | a supported PS2 card size, e.g. 8 MB (8388608 bytes)   |

The easiest way to make a template is to set up a card on *sd2psXtd* the way you want it, then copy its `.mcd` file from the `MemoryCards` folder to `.sd2psx/ps1.mcd` or `.sd2psx/ps2.mcd`.

The template is used whenever a card image does not exist yet and is created. This applies to:

- regular cards and channels
- Game ID cards
- the BootCard
- PS2 cards that are recreated because the existing image had an invalid size

Existing card images are never changed.

*Note 1: The template must be a raw image in the same format sd2psXtd uses for its own cards. Images that include ECC data (for example PCSX2 `.ps2` files, which are 8650752 bytes instead of 8388608) or a file header (for example `.gme`) are not valid templates.*
*Note 2: For PS2, the size of the template decides the size of the new card. It takes precedence over `CardSize` in `CardX.ini` and over the card size in the settings.*
*Note 3: If a template has an invalid size, it is ignored and an empty card is created instead. A warning is written to the debug log.*
*Note 4: For PS2, a card created from a template only becomes available once the whole template has been copied. Unlike a newly generated empty 8 MB card, it is not available while it is still being created (see [Instant Card Availability](features.md#ps2-instant-card-availability)).*
*Note 5: The same PS2 template is used for all PS2 variants (retail, Arcade, Conquest, and Prototype).*

## General: Game2Folder mapping

There are some games, that share save data for multiple game ids (like the Singstar series etc). For these cases, a custom game to folder mapping can be created.

If a game with a mapped id is loaded, instead of using the game id based folder, the mapped folder is used for storing the card.

The mapping needs to be defined in ```.sd2psx/Game2Folder.ini``` in the following way:

```ini
[PS1]
SCXS-12345=FolderName1
[PS2]
SCXS-23456=FolderName2
```

*Note: Be aware: Long folder names may not be displayed correctly and may result in stuttering of MMCE games due to scrolling.*
*Note 2: Make sure there is an empty line at the end of the ini file.*
