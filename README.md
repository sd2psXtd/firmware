![sd2psXtd Logo](doc/Logo.png)
*Logo by Berion ❤️*

# sd2psXtd Firmware

sd2psXtd is an extended firmware for the popular *Multipurpose MemoryCard Emulator* sd2psx by developer @xyzz (see [here](https://github.com/sd2psx)). It combines cutting-edge extended functionality (like game ID switching, file system access, and dynamic mode selection) with the rock-solid performance of the original sd2psx firmware.

It provides the same functionality as the official stable firmware and extends it with the following features:

- **PS2:** Game ID switching
- **PS2:** PS1 dynamic mode selection
- **PS2:** MMCEMAN and MMCEDRV support
- **PS2:** Instant card availability
- **PS2:** 1-64 MB card size support
- **PS2:** Support for developer (`DTL-H` & `DTL-T`), Arcade (`COH-H`) and Prototype (`EB`?) models is available.
- **PS1:** BootCard mechanics
- **PS1:** PSRAM support
- **PS1:** Card Switch Controller Combo Support
- **PS1:** Super fast FreePSXBoot
- **PS1:** Net Yaroze Support
- **General:** Settings file
- **General:** Support for other RP2040-based MMCE devices
- **General:** Per Card Config
- **General:** Game2Folder mapping

## PS2: Game ID Switching

Like on PS1, *sd2psXtd* can detect the game ID of a PS2 console and switch to a dedicated card per game. Game ID switching can be turned off in the device settings for PS2.

This is done in two ways:

### History File Tracking

When starting a game, the PS2 writes its game ID to a history file on the current memory card. *sd2psXtd* tracks the write to this file and detects which game ID has just been written. After that, a game card for this game is mounted and exposed to the PS2.

### MMCEMAN Game ID

*MMCEMAN* is a custom IOP module to communicate with Multipurpose Memory Card Emulators. This can be integrated with OPL so OPL can directly send the game ID of a launched game to *sd2psx*.

## PS2: PS1 Dynamic Mode Selection

When launching in PS2 mode, commands sent to *sd2psx* are monitored. Since PS1 sends controller messages on the same bus as memory card messages, if a controller message is detected, the PS2 switches to PS1 mode.

While in general this should be safe behavior, if *sd2psx* is used mainly in PS1, manual mode selection is recommended.

> [!CAUTION]
> **Note 1:** If *sd2psx* is connected to a PS1 in PS2 mode, there is always a risk of damaging your PS1 console. You have been warned!

> [!CAUTION]
> **Note 2:** Do not use *sd2psx* in dynamic mode on a PS1 multitap, as this **WILL** damage your PS1 multitap device.

## PS2: MMCEMAN and MMCEDRV Support

*MMCEMAN* is a PS2 module for interacting with *Multipurpose Memory Card Emulators*. Its main use cases include:

- **Card Switching:** MMCEMMAN can request a card change on *MMCEs*, such as setting a channel or selecting a specific card.
- **Game ID Communication:** MMCEMAN can send a game ID to the *MMCE*, which may in turn switch to a dedicated card for this ID if activated.
- **File System Access:** MMCEMAN allows access to *MMCEs* filesystem through standard POSIX file I/O calls
- **Game loading:** MMCEDRV allows for loading games off of *MMCEs* with performance equal to, or in most cases, better than MX4SIO.

## PS2: Instant Card Availability

If using 8MB cards, *sd2psXtd* firmware exposes the card to the PS2 while it is still being transferred to PSRAM. This enables using FMCB/PS2BBL at boot time without additional waiting scripts.
Very helpful for PlayStation 2 models with simpler OSDSYS programs, that result on faster boot times (like PSX DESR and Arcade PS2)

## PS2: 1-64 MB Card Size Support

Support for card sizes between 1 and 64 MB has been added. Cards larger than 8 MB rely heavily on quick SD card access, so on older or lower-quality SD cards, these larger cards may become corrupt.

> [!NOTE]
>  While the feature has been extensively tested, it is still recommended to use 8MB cards, as this is the official specification for memory cards.

## PS2: Support for Developer, Arcade and Prototype PS2s

PS2 memory cards have been used in variations of PS2 like: *DevKits*, *TestKits*, *Arcades* and *Prototypes*.

*sd2psXtd* firmware supports these devices by configuring the variant within the PS2 settings.

These PlayStation 2 variations use different magicgate keysets to ensure their memory cards are inaccessible in other devices (e.g., opening a developer memory card on a normal PS2). This is why SD2PSX must actively support them.

> [!NOTE]
> **Devkit/DTL-H owners**:
> as you may notice, SD2PSX has no `DEVELOPER` mode, this is because sd2psxtd is mimicking the behavior of licensed retail card. To use the device on developer hardware, set the card to `RETAIL` mode [^1]

[^1]: Devkits: official retail memory cards use developer magicgate by default until the console actively requests to use retail magicgate with a dedicated command

## PS1: BootCard Mechanics

If BootCard functionality is activated, the PS1 starts with BootCards at startup. If BootCard is not activated, the card index and channel from the previous session are restored automatically.

## PS1: PSRAM Support

*sd2psXtd* firmware allows PS1 cards to be served from PSRAM. While this is mainly an under-the-hood change, it provides more flexibility in RAM usage.

## PS1: Card Switch Controller Combo Support


Controller Button Mapping for Card and Channel Switching

The following button combinations are used to perform card and channel switches:

- L1 + R1 + L2 + R2 + Up: Switch to the Next Card
- L1 + R1 + L2 + R2 + Down: Switch to the Previous Card
- L1 + R1 + L2 + R2 + Right: Switch to the Next Channel
- L1 + R1 + L2 + R2 + Left: Switch to the Previous Channel
- L1 + R1 + L2 + R2 + SELECT: Switch to Boot Card

These mappings require that all four buttons (L1, R1, L2, R2) are held down in combination with one of the directional inputs.

## PS1: Super fast FreePSXBoot

*sd2psXtd* allows super fast booting of FreePSXBoot by using some non standard card communication.
Please note: This is only possible using a special FreePSXBoot Version provided at https://sd2psXtd.github.io

## PS1: Net Yaroze Support

*sd2psXtd* will act as a Net Yaroze Access Card, if used with the Net Yaroze Software.


## General: Settings File

*sd2psXtd* generates a settings file (`.sd2psx/settings.ini`) that allows you to edit some settings through your computer. This is useful when using one SD card with multiple *sd2psx* devices or *MMCE* devices without a display to change settings.

A settings file has the following format:

```ini
[General]
Mode=PS2
FlippedScreen=OFF
[PS1]
Autoboot=ON
GameID=ON
[PS2]
Autoboot=ON
GameID=ON
CardSize=16
Variant=RETAIL
```

Possible values are:

| Setting       | Values                                |
|---------------|---------------------------------------|
| Mode          | `PS1`, `PS2`                          |
| AutoBoot      | `OFF`, `ON`                           |
| GameID        | `OFF`, `ON`                           |
| CardSize      | `1`, `2`, `4`, `8`, `16`, `32`, `64`  |
| Variant       | `RETAIL`, `PROTO`, `ARCADE`, `ARCADE2`|
| FlippedScreen | `ON`, `OFF`                           |

*Note: Make sure there is an empty line at the end of the ini file.*

## General: Support for Other RP2040-Based MMCE Devices

Support for different MMCE devices that share the same MCU has been added:

- **PicoMemcard+/PicoMemcardZero:** DIY devices by dangiu (see [here](https://github.com/dangiu/PicoMemcard?tab=readme-ov-file#picomemcard-using-memory-card)) without PSRAM. Use *PMC+* or *PMCZero* firmware variant.
- **PSXMemCard:** A commercial device by BitFunX sharing the same architecture as *PMC+*. Use *psxmemcard* firmware variant.
- **PSXMemCard Gen2:** A commercial device by BitFunX, sharing the same architecture as *sd2psx*. Use *sd2psx* firmware variant.

For each device, follow the flashing instructions provided by the creator, using the corresponding *sd2psXtd* firmware file.

*New in 1.2*:
PMC+ and PMCZero now support using the onboard buttons. They are assigned in the following way (according to the markings ons their board):

- **Button 1**: Load BootCard
- **Button 2**:
    - Short Press: Previous Channel
    - Long Press: Previous Card
- **Button 3**:
    - Short Press: Next Channel
    - Long Press: Next Card

## General: Per Card Config

There are some configuration values that can be modified on a per card base within a config file named  `CardX.ini` in a card folder, where `X` is the card index.

*Note 1: The `CardSize` setting is only used for PS2 cards and can only be either of `1`, `2`, `4`, `8`, `16`, `32`, `64`.*
*Note 2: The BOOT folder should contain a file named `BootCard.ini`*
*Note 3: Make sure there is an empty line at the end of the ini file.*

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

## Special Thanks to...

- **@xyz**: for sd2psx ❤️
- **sd2psXtd Team**: (you know who you are 😉 )
- **@El_isra**: for so much different stuff ❤️
- **@Berion**: Our new beautiful logo ❤️
- **8BitMods Team**: for helping out with card formatting and providing lots of other useful information ❤️
- **@Mena / PhenomMods**: for providing hardware to some team members ❤️
- **BitFunX**: for providing PSXMemcard and PSXMemcard Gen2 Hardware for dev ❤️
- **All Testers**: ripto, Vapor, seewood, john3d, rippenbiest, ... ❤️
