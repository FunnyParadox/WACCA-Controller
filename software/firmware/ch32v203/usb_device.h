#ifndef WACCA_USB_DEVICE_H
#define WACCA_USB_DEVICE_H
#include "usb_control.h"

struct wacca_usb_stats { uint32_t resets, errors, received, transmitted, generation; };
void wacca_usb_init(enum wacca_role role);
bool wacca_usb_ready(void);
uint32_t wacca_usb_generation(void);
size_t wacca_usb_read(uint8_t *data, size_t capacity);
// One short message per call (1..64 bytes), copied before returning
// Returns false under backpressure; never waits for the host
bool wacca_usb_send(const uint8_t *data, size_t length);
void wacca_usb_get_stats(struct wacca_usb_stats *stats);

#endif
