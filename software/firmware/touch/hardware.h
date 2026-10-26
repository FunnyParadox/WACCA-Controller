#ifndef WACCA_TOUCH_HARDWARE_H
#define WACCA_TOUCH_HARDWARE_H

#include <stdint.h>
#include "usb2422.h"

const struct wacca_hub_io *wacca_hub_interface(void);
void wacca_touch_irq_init(void);
uint16_t wacca_touch_irq_pending(void);

#endif
