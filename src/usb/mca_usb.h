#pragma once

#include <stdbool.h>

void mca_usb_init(void);  /* Select transport; connect Pico CDC immediately. */
void mca_usb_start(void); /* Connect deferred MCA USB once the card is ready. */
void mca_usb_task(void);
void mca_usb_reject_pending_request(void);
void mca_usb_card_switch_begin(void); /* After emulator exit. */
void mca_usb_card_switch_delay(void); /* After closing the old card. */
void mca_usb_card_switch_end(void);   /* After opening the new card, before enter. */
void mca_usb_set_enabled(bool enabled);
bool mca_usb_is_enabled(void);
bool mca_usb_is_started(void);
