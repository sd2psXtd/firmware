#pragma once

#include <stdbool.h>

/*
 * Firmware-wide USB lifecycle. One USB profile (Serial/CDC or MCA) is active
 * per session; it is chosen from the settings before each mode starts and only
 * changes after the session ends.
 */
/* Call after settings_init(); starts the Serial profile from the flash settings. */
void usb_session_init(void);
/* Pick the profile for the next session (after the SD settings reload). */
void usb_session_prepare(void);
/* Profile captured before enumeration, independent of later settings requests. */
int usb_session_get_mode(void);
void usb_session_start(void);
/* Main-loop TinyUSB polling; a no-op while the Serial profile is serviced
 * in the background. */
void usb_session_poll(void);
/* Keep the session running while the mode runs or MCA work is still owned
 * by the card worker. */
bool usb_session_continue(bool mode_running);
void usb_session_end_mode(void);
