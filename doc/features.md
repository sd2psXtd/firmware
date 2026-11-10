# PS1 and PS2 Features

[← Back to README](../README.md)

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
> If *sd2psx* is connected to a PS1 or a PS1 multitap in PS2 mode, there is always a risk of damaging your console / multitap. Safeguards have been implemented to minimize this risk, but they are not entirely foolproof. You have been warned!

## PS2: MMCEMAN and MMCEDRV Support

*MMCEMAN* is a PS2 module for interacting with *Multipurpose Memory Card Emulators*. Its main use cases include:

- **Card Switching:** MMCEMMAN can request a card change on *MMCEs*, such as setting a channel or selecting a specific card.
- **Game ID Communication:** MMCEMAN can send a game ID to the *MMCE*, which may in turn switch to a dedicated card for this ID if activated.
- **File System Access:** MMCEMAN allows access to *MMCEs* filesystem through standard POSIX file I/O calls
- **Game loading:** MMCEDRV allows for loading games off of *MMCEs* with performance equal to, or in most cases, better than MX4SIO.

## PS2: Instant Card Availability

If using 8MB cards, *sd2psXtd* firmware exposes the card to the PS2 while it is still being transferred to PSRAM. This enables using FMCB/PS2BBL at boot time without additional waiting scripts.
Very helpful for PlayStation 2 models with simpler OSDSYS programs, that result on faster boot times (like PSX DESR and Arcade PS2)

## PS2: 1-128 MB Card Size Support

Support for card sizes between 1 and 128 MB has been added. Cards larger than 8 MB rely heavily on quick SD card access, so on older or lower-quality SD cards, these larger cards may become corrupt.

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

The Controller Combos can be activated by a setting in the PS1 Settings menu.

## PS1: Super fast FreePSXBoot

*sd2psXtd* allows super fast booting of FreePSXBoot by using some non standard card communication.
Please note: This is only possible using a special FreePSXBoot Version provided at https://sd2psXtd.github.io

## PS1: Net Yaroze Support

*sd2psXtd* will act as a Net Yaroze Access Card, if used with the Net Yaroze Software.
