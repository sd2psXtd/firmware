#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum single MCA bulk transfer supported by the device endpoint. */
#define MCA_DEVICE_MAX_TRANSFER 1156u

/* Endpoints of the MCA vendor interface. */
#define MCA_EP_IN  0x81
#define MCA_EP_OUT 0x02
#define MCA_EP_INT 0x83

typedef struct {
    /* Called synchronously from TinyUSB's OUT completion callback. `data` is
     * valid only for the duration of the call; copy it if it must be retained.
     * Keep callbacks short and non-blocking. */
    void (*out_received)(const uint8_t *data, uint16_t length);
    void (*in_complete)(bool success, uint32_t transferred);
    void (*bus_reset)(void);
    /* The host reset the bulk pipes (SET_CONFIGURATION, SET_INTERFACE or
     * CLEAR_FEATURE(ENDPOINT_HALT)). Whatever was queued on them has been
     * discarded; no completion will be reported for it. */
    void (*pipes_reset)(bool in, bool out);
} mca_device_callbacks_t;

/* Select the USB identity before TinyUSB starts. A true profile is the MCA
 * vendor device; false preserves the Pico CDC and reset-interface profile. */
void mca_device_init(bool mca_profile, const mca_device_callbacks_t *callbacks);
bool mca_device_profile_is_mca(void);

/* Task-side operations. The caller owns `data`; it must be 4-byte aligned and
 * remain valid until in_complete, pipes_reset(in) or bus_reset. Sending fails
 * if the endpoint is unavailable, a transfer is already pending, or the
 * payload exceeds the device limit. */
bool mca_device_send(const uint8_t *data, size_t length);
/* Arm the next OUT packet only while the protocol can accept it. */
void mca_device_receive(void);
void mca_device_set_presence(bool present);
/* Call from the normal USB/task loop: finishes host pipe resets and delivers
 * pending presence updates. */
void mca_device_task(void);
