# USB Interfaces

[← Back to README](../README.md)

## General: USB-CDC Command Interface

When PS3 Adapter mode is off, the device exposes a USB-CDC serial interface that accepts simple text commands. This can be used to change cards, channels, modes, PS2 variants, or to reboot the device without using the on-device controls.

Commands are lowercase and submitted with Enter:

```text
help
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

PS3 Adapter mode presents the currently selected PS1 or PS2 card over USB as a Sony PS3 Memory Card Adaptor, allowing compatible software to access the emulated card. Select the card and channel using the device controls, and select PS1 or PS2 mode as appropriate.

Enable or disable **PS3 Adapter** in the device menu, or set `MCA=ON` or `MCA=OFF` in the `[General]` section of the settings file. The USB connection changes between the adaptor and USB-CDC profiles; the serial command interface is unavailable while PS3 Adapter mode is enabled.
