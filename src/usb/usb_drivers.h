#pragma once

/* Application class drivers registered with TinyUSB by usb_session.c. */

#include "device/usbd.h"
#include "device/usbd_pvt.h"

usbd_class_driver_t const *mca_device_driver(void);
usbd_class_driver_t const *usb_reset_itf_driver(void);
