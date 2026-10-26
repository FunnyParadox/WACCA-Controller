#ifndef WACCA_DIAGNOSTICS_H
#define WACCA_DIAGNOSTICS_H
#include "usb_device.h"
void wacca_diagnostics_poll(enum wacca_role role, uint32_t boot_stage, uint32_t pending_sectors, uint32_t hub_result);
#endif
