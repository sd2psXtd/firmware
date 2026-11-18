#pragma once
#include "mca_bridge.h"
#include "mca_protocol.h"
#include "settings.h"
#include <assert.h>
#include <string.h>

extern mca_bridge_t mca_bridge;
static uint64_t now;
static const uint8_t *sent;
static size_t sent_length;
static bool complete_on_usb_task, card_present;
static bool mca_enabled = true;
static unsigned mca_setting_updates;
static bool auth_valid = true, card_accessible = true;
static int card_mode = MODE_PS2, card_variant = PS2_VARIANT_RETAIL;
static int usb_mode = USB_MODE_MCA;
static uint32_t card_capacity = 8u * 1024 * 1024;
uint64_t time_us_64(void) { return now; }
void sleep_ms(uint32_t milliseconds) { now += (uint64_t)milliseconds * 1000; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) {
    (void)timeout_ms; (void)in_isr;
    if (complete_on_usb_task) {
        complete_on_usb_task = false;
        mca_protocol_callbacks()->in_complete(true, sent_length);
    }
}
int settings_get_mode(bool temporary) { (void)temporary; return card_mode; }
int settings_get_usb_mode(bool current) { return current ? usb_mode : USB_MODE_MCA; }
bool settings_get_mca_enabled(void) { return mca_enabled; }
void settings_set_mca_enabled(bool enabled) { ++mca_setting_updates; mca_enabled = enabled; }
int settings_get_ps2_variant(void) { return card_variant; }
bool ps2_cardman_is_accessible(void) { return card_accessible; }
bool ps2_cardman_is_idle(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return card_capacity; }
bool ps2_mc_auth_isValid(void) { return auth_valid; }
void mca_device_set_presence(bool present) { card_present = present; }
void mca_device_task(void) {}
void mca_device_receive(void) {}
bool mca_device_profile_is_mca(void) { return true; }
bool mca_device_send(const uint8_t *data, size_t length) {
    assert(((uintptr_t)data & 3u) == 0);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_RESPONSE);
    sent = data;
    sent_length = length;
    return true;
}
