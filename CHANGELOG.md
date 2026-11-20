# Changelog

## Unreleased

### Features

- Added template cards: new PS1 and PS2 cards are created as a copy of `.sd2psx/ps1.mcd` or `.sd2psx/ps2.mcd` when present (#90, #134).
- Added the `set mode mass` serial command to switch to USB mass storage, for devices without buttons such as the PSxMemCard Gen1 (#129).
- Added an experimental automatic switch from PS1 back to PS2 mode when a PS2 accesses the card, e.g. for a DKWDRV and FMCB workflow (#130).

### Fixes

- Fixed the PS2 card disappearing in MCA mode while it was still being loaded, which could cause boot loops on the PS3 (#128).
- Fixed the PS1 backend pause and teardown lifecycle, and released the PS2 backend when entering USB mass storage (#132).
- Fixed PS1 page writes with a bad checksum or truncated data changing the card, and the order of PS1 reads and writes (#132).
- Fixed the reserved byte in the PS1 MMCE reset reply.
- Fixed MCA mode losing replies after the PS3 reopens the adapter (for example when leaving and re-entering the XMB memory card screen), which could hide the card and freeze the XMB for about 5 seconds (#128).

### Changed behavior

- Switching between modes is faster: USB is only restarted when the USB profile changes.
- Reduced RAM usage, including the buffer used to create template cards.

## 1.5.0 - 2026-11-13

### Features

- Added PS3 Memory Card Adaptor emulation for accessing PS1 and PS2 cards over USB (#122).
- Added USB mass storage mode for accessing the microSD card from a computer. Thanks to @warksmith for the inspiration! (#123)
- Added support for GPT and superfloppy SD card layouts, with faster mounting (#112).
- Added separate MaxChannels settings for PS1 and PS2, and automatic formatting for PS2 cards smaller than 8 MB (#121).
- Added `version`, `ls`, and `stat` serial commands. Thanks to @mathewbeall! (#106)

### Fixes

- Improved PS1 protection when the device is connected in PS2 mode, and fixed potential save corruption in some games. Thanks to @max-las! (#103, #105)
- Improved arcade compatibility and card-switch handling: arcade variants no longer need a second console reset after a card switch. Thanks to @mathewbeall for the merged fix and work on the card-switch problem! (#104, #107)
- Fixed PS1 ACK timing and first-run reset behavior, PS2 game ID detection, and several card-switch and MMCEMAN issues (#59, #97, #115).
- Fixed potential card corruption when switching from PS2 to PS1 mode.
- Improved SD card error messages.

### Changed behavior

- Reorganized the documentation (#124).
- Updated the splash screens, including a new USB mass storage splash. Thanks to @Berion!

## 1.4.0 - 2026-07-08

### Features

- Added a USB-CDC command interface for card/channel switching, Game ID selection, PS1/PS2 mode changes, PS2 variant changes, help, and device/bootloader reset commands.
- Added PS2 card-size handling for larger cards, including 128 MB card creation from settings/GUI and validation for existing images up to 2 GB.
- Added PS2 MMCEMAN commands for atomic card-and-channel selection and filesystem rename operations.
- Added PS1 MMCE reset support and direct PS1 MMCE set-card/set-channel handling.
- Added settings and GUI controls for enabling/disabling PS1 controller combos and limiting the maximum card index per mode.
- Added SD-root `splash.bin` deployment so a splash image can be imported into flash automatically at boot.
- Added support for different-sized flash chips to make firmware builds more flexible because of the current chip supply crisis.
- Added SD-card CID/details to diagnostics and the on-device Info menu.
- Updated splashgen with a modern simple/advanced UI, histogram preview, Otsu/adaptive thresholding, dithering, input levels, invert output, improved selection behavior, and UF2 validation.

### Fixes

- Fixed PS1 Game2Folder mapping so the mapped folder is not overwritten by the raw parent Game ID.
- Fixed Game ID, card, and channel edge cases around boot cards, Game ID cards, and card switching.
- Fixed multiple overflow and bounds risks in Game ID parsing, card configuration parsing, MMCEMAN paths, and filesystem command handling.
- Fixed stale PS2 data-interface operations and history-tracker slot data during card changes.
- Fixed invalid PS2 card images causing fatal errors by moving corrupt/invalid images aside and recreating them.
- Fixed PS2 MagicGate key-select handling to use only the documented low parameter bits.
- Improved SD write reliability with retries and more useful SD failure diagnostics.
- Fixed PMC three-button handling and related input initialization issues.

### Changed behavior

- USB-CDC stdio is now enabled for release builds so the serial command interface is available outside debug firmware.
- Settings serialization now includes `EnableControllerCombo` and `MaxCardIdx`, and settings files are rewritten with truncation to remove stale content.
- Flash layout is now derived from detected flash capacity, with fallback handling for the legacy splash offset.
- Card selection now honors configured maximum-card-index limits for PS1 and PS2.
- Channel up/down behavior on non-GUI builds now clamps at the valid range edge instead of wrapping invisibly.
- PS2 card formatting now builds the superblock and FAT data dynamically for the selected card size.
- Imported SD-root `splash.bin` files are removed after successful deployment to flash.

## 1.3.0 - 2025-11-02

### Features

- Added a customizable splash screen with the splashgen UF2 workflow and persistent flash storage.
- Added game image screens for memory-card folders, with channel-specific images taking precedence over card-level images.
- Added full SoulCalibur 2 Conquest Card support, including the required Conquest-specific ECC behavior.
- Added the splashgen utility and updated database tooling support.

### Fixes

- Improved the invalid virtual memory-card size error screen.
- Fixed GUI issues around menu state, card switching while in menus, and display refresh behavior.
- Fixed splash and animation states that could run forever or leave the device stuck on the splash screen.
- Reduced unnecessary PS1 GUI refreshes.
- Fixed display-related issues and removed the need for a Python virtual environment when building the database.

### Changed behavior

- Reworked the GUI around explicit splash, main, menu, switching, and game-image states.
- Game image lookup now checks for a channel-specific `<card>-<channel>.bin` file before falling back to `<card>.bin`.
- Custom splash data persists through normal firmware updates.
- Unknown PSRAM IDs no longer immediately trigger a fatal error or enter QSPI mode unless the detected ID is known-good.
- Conquest mode now uses its own CRC/ECC handling instead of the standard PS2 ECC path.

## 1.2.1 - 2025-07-18

### Features

- Added support for the second arcade/Conquest mode, including the `CONQUEST` setting, `MemoryCards/SC2` path, and arcade key support.
- Extended MagicGate key-select handling to match the official `0x7F` command behavior.
- Added DMA-backed I2C display transfers and refined PIO loading/unloading behavior.

### Fixes

- Fixed maximum-channel handling for card configurations.
- Fixed Game ID mapping to use the parent Game ID when transmitted.
- Switched back to the default card when an invalid Game ID is received or after boot-card handling completes.
- Improved PS1 memory-card emulation, ACK timing, PS2 multitap handling, and card-selection tracking.
- Improved CIV confirmation behavior.
- Fixed DMA channel claiming so channels are claimed once instead of repeatedly during transfers.
- Ignored the `0x7F` key-select command outside retail mode.

### Changed behavior

- The second arcade mode is serialized as `CONQUEST` and uses `MemoryCards/SC2` for card storage.
- Channel getters no longer clamp values above the previous maximum, allowing per-card maximum channel settings to work correctly.
- Mode lookups can use the current temporary mode during transitions.
- PS2 data PIO now keeps ACK low for the full transfer for faster and more stable operation.

## 1.2.0 - 2025-04-27

### Features

- Added Net Yaroze Access Card support.
- Added a PS1 controller combo to switch to the boot card with `L1 + R1 + L2 + R2 + SELECT`.
- Added onboard PMC+/PMCZero button support for boot-card loading and card/channel switching.
- Added LED status support, including WS2812/PMCZero LED handling and activity indications for writes, card switches, and MMCE file-system activity.
- Replaced the old PS1 ODEMAN flow with a PS1 MMCE task and command handling.
- Added the new project logo.

### Fixes

- Fixed button initialization and controller-combo handling.
- Fixed card switching becoming stuck on Core 1.
- Fixed PS2 multitap behavior with the Net Yaroze Access Card.
- Improved reset detection.
- Kept authentication valid after the first successful auth instead of resetting after a later transient failure.
- Ensured the PS2 history tracker completes before card switching.
- Reduced RAM usage and adjusted file limits for PSRAM-less devices.
- Fixed LED, log-level, and missing-include issues.

### Changed behavior

- PS1 card initialization now tries the Game ID card before falling back to the boot card.
- PS2 card manager now starts in an unknown/inaccessible state until the card has been opened.
- SD-mode accessibility now allows use except while a card is being created.
- PS1 card switching now waits for controller input to be released, flushes the current card, and reopens the selected card before entering emulation.
- LED code moved from the flat `src/led.c` implementation into the `src/led/` library.

## 1.1.1 - 2025-03-04

### Features

- No user-facing features; this was a maintenance release.

### Fixes

- Improved SD-mode detection for PS2 cards, especially on PSRAM-less devices and large-card configurations.

### Changed behavior

- PS2 card accessibility and loading decisions now use the data-interface SD-mode state instead of raw PSRAM and card-size checks.

## 1.1.0 - 2025-03-03

### Features

- Added PS1 controller combos for card and channel switching.
- Added support for superfast FreePSXBoot via the PS1 long-read command.
- Improved PS1 controller transfer handling and ACK timing.

### Fixes

- Fixed card creation on slow SD cards.
- Fixed PS1 Get Page handling.
- Fixed mode handling, deployment behavior, activity reporting, MMCE file modes, and settings serialization/deserialization.
- Fixed the data interface on PSRAM-less devices.
- Fixed dynamic card switching during card creation.
- Improved timing and corruption behavior on SD-mode cards.

### Changed behavior

- PS2 data-interface processing now waits until the PS2 card manager is idle.
- PS1 memory-card command handling was refactored around explicit command handlers.
- PS1 ACK timing was adjusted for faster and more reliable transfers.
- The PSXMemCard firmware variant is documented as `psxmemcard`.
- INI files are documented as requiring a trailing empty line.

## 1.0.0 - 2025-01-31

### Features

- Initial stable sd2psXtd firmware release.
- Added PS1 and PS2 virtual memory-card emulation with PS1/PS2 mode switching.
- Added card, channel, and boot-card switching behavior.
- Added PS2 Game ID switching, game-name database support, Game2Folder mapping, and per-card configuration.
- Added MMCE file-system support and CIV deployment support.
- Added support for retail, development, prototype, and arcade PS2 variants.
- Added support for RP2040-based hardware variants including sd2psx, PMC+, PMCZero, PSXMemCard, and PSXMemCard Gen2.
- Added OLED GUI support with progress, activity, error, and display-setting screens.
- Added SD-card settings support, PSRAM-backed operation where available, and release/nightly build versioning infrastructure.

### Fixes

- Established stable card creation and loading behavior for the first stable release.
- Improved SD error handling and memory-card state reporting.
- Improved PS1/PS2 card switching reliability.
- Added baseline error reporting, build, and release infrastructure fixes needed for stable firmware distribution.

### Changed behavior

- Introduced the stable release structure for firmware builds and version information.
- Standardized SD-card folder and settings behavior for PS1, PS2, and variant-specific cards.
- Established the initial user-facing behavior for boot cards, dynamic card switching, display settings, and per-card configuration.
