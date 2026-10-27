#ifndef WACCA_TOUCH_HARDWARE_H
#define WACCA_TOUCH_HARDWARE_H

#include <stdint.h>

void wacca_touch_irq_init(void);
uint16_t wacca_touch_irq_pending(void);

#endif
