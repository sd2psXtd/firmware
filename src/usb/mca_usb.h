#pragma once

#include <stdbool.h>
#include <stdint.h>

void mca_usb_init(void);  /* Select transport; connect Pico CDC immediately. */
void mca_usb_prepare(void); /* Apply settings while core 1 is stopped, before mode init. */
void mca_usb_start(void); /* Connect deferred MCA USB once the card is ready. */
void mca_usb_task(void);
/* Thin card-emulator hooks: receive on core 1, exit on core 0 after stopping it. */
uint8_t mca_usb_receive_first(uint8_t *byte, volatile int *exit_request);
void mca_usb_card_exit(void);
void mca_usb_card_switch_begin(void); /* After emulator exit. */
void mca_usb_card_switch_delay(void); /* After closing the old card. */
void mca_usb_card_switch_end(void);   /* After opening the new card, before enter. */
bool mca_usb_profile_is_mca(void); /* USB identity selected for this connection. */
bool mca_usb_is_started(void);
