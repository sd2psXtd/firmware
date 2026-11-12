# Configuration

[← Back to README](../README.md)

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
