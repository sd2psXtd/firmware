![sd2psXtd Logo](doc/Logo.png)
*Logo by Berion ❤️*

# sd2psXtd Firmware

sd2psXtd is an extended firmware for the popular *Multipurpose MemoryCard Emulator* sd2psx by developer @xyzz (see [here](https://github.com/sd2psx)). It combines cutting-edge extended functionality (like game ID switching, file system access, and dynamic mode selection) with the rock-solid performance of the original sd2psx firmware.

It provides the same functionality as the official stable firmware and extends it with the following features:

- **PS2:** Game ID switching
- **PS2:** PS1 dynamic mode selection
- **PS2:** MMCEMAN and MMCEDRV support
- **PS2:** Instant card availability
- **PS2:** 1-128 MB card size support
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
- **General:** Splash Screen
- **General:** Game Image Screen
- **General:** USB-CDC command interface
- **General:** USB PS3 Memory Card Adaptor emulation

> [!CAUTION]
> If *sd2psx* is connected to a PS1 or a PS1 multitap in PS2 mode, there is always a risk of damaging your console / multitap. Safeguards have been implemented to minimize this risk, but they are not entirely foolproof. You have been warned!

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

## Documentation

- [PS1 and PS2 features](doc/features.md) — game ID switching, card sizes, console variants, controller shortcuts, and more.
- [Configuration](doc/configuration.md) — settings file, per-card options, and Game2Folder mapping.
- [USB interfaces](doc/usb.md) — PS3 Memory Card Adaptor emulation and USB-CDC commands.
- [Display customization](doc/customization.md) — startup splash and card images.

## Special Thanks to...

- **@xyz**: for sd2psx ❤️
- **sd2psXtd Team**: (you know who you are 😉 )
- **@El_isra**: for so much different stuff ❤️
- **@Berion**: Our new beautiful logo ❤️
- **8BitMods Team**: for helping out with card formatting and providing lots of other useful information ❤️
- **@Mena / PhenomMods**: for providing hardware to some team members ❤️
- **BitFunX**: for providing PSXMemcard and PSXMemcard Gen2 Hardware for dev ❤️
- **All Testers**: ripto, Vapor, seewood, john3d, rippenbiest, ... ❤️
- **@niemasd**: For his [*GameDB-PSX*](https://github.com/niemasd/GameDB-PSX) database - Game naming and ID on PS1 would not be possible without it! ❤️
