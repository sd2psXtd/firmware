#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum single MCA bulk transfer supported by the device endpoint. */
#define MCA_DEVICE_MAX_TRANSFER 1156u

typedef struct {
    /* Called synchronously from TinyUSB's OUT completion callback. `data` is
     * valid only for the duration of the call; copy it if it must be retained.
     * Keep callbacks short and non-blocking. */
    void (*out_received)(const uint8_t *data, uint16_t length);
    void (*in_complete)(bool success, uint32_t transferred);
    void (*bus_reset)(void);
    void (*presence_complete)(bool success, uint32_t transferred);
} mca_device_callbacks_t;

/* Select the USB identity before TinyUSB starts. A true profile is the MCA
 * vendor device; false preserves the Pico CDC and reset-interface profile. */
void mca_device_init(bool mca_profile, const mca_device_callbacks_t *callbacks);
bool mca_device_profile_is_mca(void);

/* Task-side operations. The caller owns `data`; it must be 4-byte aligned and
 * remain valid until in_complete or bus_reset. Sending fails if the endpoint is
 * unavailable, a transfer is already pending, or the payload exceeds the
 * device limit. */
bool mca_device_send(const uint8_t *data, size_t length);
void mca_device_set_presence(bool present);
/* Call from the normal USB/task loop to deliver pending presence updates. */
void mca_device_task(void);
