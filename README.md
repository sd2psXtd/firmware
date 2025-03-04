# sd2psXtd Firmware

sd2psXtd is an extended firmware for the popular *Multipurpose MemoryCard Emulator* (MMCE) sd2psx by developer @xyzz (see [here](https://github.com/sd2psx)). This firmware is designed for use with **PlayStation 1, PlayStation 2, and arcade machines** based on those system's hardware, utilizing their memory card slots. It combines cutting-edge extended functionality, like Game ID switching, file system access, and dynamic mode selection, with the rock-solid performance of the original sd2psx firmware. **The official MMCEMAN driver can be found [here](https://github.com/ps2-mmce/mmceman).**

It provides the same functionality as the official stable firmware and extends it with the following features:

-   **PS1/PS2:** Instant card availability/PSRAM support (requires devices with PSRAM)
-   **PS1/PS2:** Automatic Game ID Virtual Memory Card Switching
-   **PS1/PS2:** BootCard Mechanics
-   **PS1:** Super fast FreePSXBoot
-   **PS1:** Card Switch Controller Combo Support
-   **PS2:** Dynamic PS1 Mode Selection
-   **PS2:** MMCEMAN and MMCEDRV support
-   **PS2:** 1-64 MB card size support
-   **PS2:** Support for developer (`DTL-H` & `DTL-T`), Arcade (`COH-H`) and Prototype (`EB`?) models is available.
-   **General:** Settings file
-   **General:** Support for other RP2040-based MMCE devices
-   **General:** Per Card Config
-   **General:** Game2Folder mapping

## PS1/PS2: Automatic Game ID Virtual Memory Card Switching

*sd2psXtd* firmware includes an advanced feature that automatically detects the Game ID of the game being launched and switches to a corresponding virtual memory card image specific to that game. An example path for a PS1 game with Game ID `SLES-02618` and Channel 1 would be `sdcard root: MemoryCards/PS1/SLES-02618/SLES-02618-1.mcd`.

## PS1/PS2: Instant Card Availability / PSRAM Support

The *sd2psXtd* firmware supports using PSRAM to serve memory cards. For cards that are 8MB or smaller, the firmware exposes the card to the PS2 while it is still being transferred to PSRAM. This allows for immediate use of FMCB/PS2BBL at boot time without needing additional waiting scripts. This feature is particularly beneficial for PlayStation 2 models with simpler OSDSYS programs, resulting in faster boot times (e.g., PSX DESR and Arcade PS2). For PS1, the memory card is always loaded into PSRAM, ensuring instant availability. **Note: This feature is only available on devices equipped with PSRAM.**

### PS1: Game ID Switching

For PS1, the technical implementation details of Game ID switching are not provided in this README.

### PS2: Automatic Game ID Switching

The PS2 supports two methods for automatic Game ID switching:

1. **History File Tracking:**
    When a game is launched, the PS2 writes its Game ID to a history file on the active memory card. The *sd2psXtd* firmware monitors this file and identifies the newly written Game ID. Once detected, the firmware mounts the corresponding virtual memory card image for that game and makes it available to the PS2.

2. **MMCEMAN Game ID:**
    *MMCEMAN* is a custom IOP module designed to interact with Multipurpose Memory Card Emulators. This method is primarily intended for developers who need to explicitly communicate a specific Game ID to the device. The firmware then uses this information to switch to the appropriate virtual memory card image.

## PS1/PS2: BootCard Mechanics

When BootCard functionality is activated (option `Autoboot=ON` in the appropriate device section inside `.sd2psx/settings.ini`), the device starts with a specific card image at startup, identified by the Game ID `BOOT`. This is particularly useful for loading exploits that utilize memory cards. If BootCard is not activated, the card index and channel from the last session are restored automatically.

> [!NOTE]
> The virtual memory card image location is `sdcard root: MemoryCards/PS1/BOOT/BootCard-%d.mcd` where `PS1` can be `PS2` depending on the mode, and `%d` is the card channel (1 by default). The card channel number is restored from the last session.

## PS1: Super fast FreePSXBoot

*sd2psXtd* allows super fast booting of FreePSXBoot by using some non standard card communication.
Please note: This is only possible using a special FreePSXBoot Version provided at [https://sd2psxtd.github.io/exploits](https://sd2psxtd.github.io/exploits)

## PS1: Card Switch Controller Combo Support

Controller Button Mapping for Card and Channel Switching

The following button combinations are used to perform card and channel switches:

-   L1 + R1 + L2 + R2 + Up: Switch to the Next Card
-   L1 + R1 + L2 + R2 + Down: Switch to the Previous Card
-   L1 + R1 + L2 + R2 + Right: Switch to the Next Channel
-   L1 + R1 + L2 + R2 + Left: Switch to the Previous Channel

These mappings require that all four buttons (L1, R1, L2, R2) are held down in combination with one of the directional inputs.

## PS2: Dynamic PS1 Mode Selection

When launching in PS2 mode, commands sent to *sd2psx* are monitored. Because the PS1 sends controller messages on the same bus as memory card messages, if a controller message is detected, *sd2psx* automatically switches to PS1 mode.

While in general this should be safe behavior, if *sd2psx* is used mainly in PS1, manual mode selection is recommended.

> [!CAUTION]
> **Note 1:** If *sd2psx* is connected to a PS1 in PS2 mode, there is always a risk of damaging your PS1 console. You have been warned!
>
> [!CAUTION]
> **Note 2:** Do not use *sd2psx* in dynamic mode on a PS1 multitap, as this **WILL** damage your PS1 multitap device.

## PS2: MMCEMAN and MMCEDRV Support

*MMCEMAN* is a PS2 module designed to interact with *Multipurpose Memory Card Emulators* (MMCEs). Its primary functions include:

-   **Card Switching:** MMCEMAN can request a card change on MMCEs, such as setting a channel or selecting a specific card.
-   **Game ID Communication:** MMCEMAN can send a Game ID to the MMCE, which can then switch to a dedicated card for this ID if enabled.
-   **File System Access from PS2:** MMCEMAN allows the PS2 to access the MMCE's filesystem (the exFAT partition on the SD card) using standard POSIX file I/O calls. This enables full access to files on the SD card, such as backups of purchased PS2 games, from the PS2.
-   **File System Access from PC:** The firmware does not provide access to the SD card filesystem through the USB port. To transfer data to and from the SD card, it must be connected to the PC separately.
-   **Game Loading:** MMCEDRV allows games to be loaded from MMCEs with performance equal to or better than MX4SIO.


## PS2: 1-64 MB Card Size Support

Support for memory card sizes of 1, 2, 4, 8, 16, 32, and 64 MB has been added. Cards larger than 8 MB rely heavily on quick SD card access, so on older or lower-quality SD cards, these larger cards may become corrupt.

> [!NOTE]
> While this feature has been extensively tested, it is still recommended to use 8 MB cards, as this is the official specification for PS2 memory cards.

## PS2: Support for Developer, Arcade and Prototype PS2s

PS2 memory cards have been used in variations of PS2 like: *DevKits*, *TestKits*, *Arcades* and *Prototypes*.

*sd2psXtd* firmware supports these devices by configuring the variant within the PS2 settings.

These PlayStation 2 variations use different magicgate keysets to ensure their memory cards are inaccessible in other devices. For example, opening a developer memory card on a normal PS2. This is why SD2PSX must actively support them.

> [!NOTE]
> **Devkit/DTL-H owners**:
> as you may notice, SD2PSX has no `DEVELOPER` mode, this is because sd2psxtd is mimicking the behavior of licensed retail card. To use the device on developer hardware, set the card to `RETAIL` mode [^1]

[^1]: Devkits: official retail memory cards use developer magicgate by default until the console actively requests to use retail magicgate with a dedicated command

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
| FlippedScreen | `ON`, `OFF`                           |
| AutoBoot      | `ON`, `OFF`                           |
| GameID        | `ON`, `OFF`                           |
| CardSize      | `1`, `2`, `4`, `8`, `16`, `32`, `64`  |
| Variant       | `RETAIL`, `PROTO`, `ARCADE`           |

*Note: `Variant=ARCADE` is for Namco System 246/256 or Konami Python 1 arcade machines and only the first memory card slot is supported. `RETAIL` is for regular PS2 machines. `PROTO` is for unknown prototype models.*

*Note: Make sure there is an empty line at the end of the ini file.*

## General: Support for Other RP2040-Based MMCE Devices

Support for different MMCE devices that share the same MCU has been added:

-   **PicoMemcard+/PicoMemcardZero:** DIY devices by dangiu (see [here](https://github.com/dangiu/PicoMemcard?tab=readme-ov-file#picomemcard-using-memory-card)) without PSRAM. Use *PMC+* or *PMCZero* firmware variant.
-   **PSXMemCard:** A commercial device by BitFunX sharing the same architecture as *PMC+*. Use *psxmemcard* firmware variant.
-   **PSXMemCard Gen2:** A commercial device by BitFunX, sharing the same architecture as *sd2psx*. Use *sd2psx* firmware variant.

For each device, follow the flashing instructions provided by the creator, using the corresponding *sd2psXtd* firmware file.

## General: Per Card Config

There are some configuration values that can be modified on a per card base within a config file named `CardX.ini` in a card folder, where `X` is the card index.

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

**Channel:** The "Channel" is used for switching between multiple memory card images for a single, specific Game ID. It's represented by a number. Within the folder named after the Game ID, a file named `GameID-3` represents Channel 3.

## General: Game2Folder mapping

There are some games, that share save data for multiple Game IDs (like the Singstar series etc). For these cases, a custom game to folder mapping can be created.

If a game with a mapped id is loaded, instead of using the Game ID based folder, the mapped folder is used for storing the card.

The mapping needs to be defined in ```.sd2psx/Game2Folder.ini``` in the following way:

```ini
[PS1]
SCXS-12345=FolderName1
[PS2]
SCXS-23456=FolderName2
```

*Note: Be aware: Long folder names may not be displayed correctly and may result in stuttering of MMCE games due to scrolling.*
*Note 2: Make sure there is an empty line at the end of the ini file.*

# Firmware update

Follow the instructions for your specific device. In general, you need to put the device in firmware update mode and connect it to a PC via a cable. Transfer the .uf2 file to the SD card device that appears, and wait until the device automatically disappears.

*Note:* To get Magicgate support in PS2 mode, you need the proprietary file `civ.bin` (8 bytes) in your SD card root on the first boot after the firmware upgrade. Magicgate is used by some games and exploits.

## Special Thanks to...

-   **@xyz**: for sd2psx ❤️
-   **sd2psXtd Team**: (you know who you are 😉 )
-   **@El_isra**: for so much different stuff ❤️
-   **8BitMods Team**: for helping out with card formatting and providing lots of other useful information ❤️
-   **@Mena / PhenomMods**: for providing hardware to some team members ❤️
-   **BitFunX**: for providing PSXMemcard and PSXMemcard Gen2 Hardware for dev ❤️
-   **All Testers**: ripto, Vapor, seewood, john3d, rippenbiest, ... ❤️
