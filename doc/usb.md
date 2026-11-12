# USB Interfaces

[← Back to README](../README.md)

## USB Modes

The **USB Mode** submenu offers **Serial** (normal operation with CDC), **PS3 MCA** (Sony adaptor emulation), and **Mass** (microSD mass storage). Serial and PS3 MCA persist through the `MCA` setting. Mass is temporary: ejecting restores the selected normal profile, and reboots never retain Mass.

## USB Mass Storage

> [!CAUTION]
> The RP2040's USB 1.1 Full Speed link is limited to 12 Mbit/s before protocol and SD-card overhead, so file transfers are substantially slower.

Select **USB Mode → Mass** in the device menu to access the entire microSD card from a computer. Card emulation stops while the USB icon is displayed, and buttons are ignored. Eject the drive on the computer to return to normal operation; suspension, inactivity, and unplugging are not eject events.

CDC remains available in Mass mode, with `help` and `version` commands. Commands that access files or change device state are rejected while storage owns the card. USB re-enumerates on entry and exit, so reopen the serial terminal when changing sessions. On exit the firmware remounts the card and restarts the previous console mode and USB profile.

## General: USB-CDC Command Interface

Select **USB Mode → Serial** to use the USB-CDC serial interface, which accepts simple text commands. This can be used to change cards, channels, modes, PS2 variants, or to reboot the device without using the on-device controls.

Commands are lowercase and submitted with Enter:

```text
help
version
reset bl
reset dev
channel up
channel down
set channel <idx>
card up
card down
set card <idx> [channel <idx>]
set game <id> [channel <idx>]
set mode <ps1|ps2>
set variant <retail|proto|conquest|arcade>
```

`reset bl` resets to the bootloader. `reset dev` restarts the firmware. Card, channel, and game commands affect the currently active mode. Setting a PS2 variant also switches the device to PS2 mode.

## General: USB PS3 Memory Card Adaptor Emulation

**PS3 MCA** mode presents the currently selected PS1 or PS2 card over USB as a Sony PS3 Memory Card Adaptor, allowing compatible software to access the emulated card. Select the card and channel using the device controls, and select PS1 or PS2 mode as appropriate.

Select **USB Mode → PS3 MCA** for adaptor emulation or **USB Mode → Serial** for normal operation. These correspond to `MCA=ON` and `MCA=OFF` in the `[General]` section of the settings file. The serial command interface is unavailable while PS3 MCA mode is enabled.
