#include "platform.h"
#include "diagnostics.h"
#include <string.h>

struct boot_diagnostics { uint32_t magic, stage, clock_hz, hub_result, pending_sectors; };
volatile struct boot_diagnostics wacca_touch_boot;
static uint8_t hub_image[WACCA_HUB_CONFIG_SIZE];

int main(void)
{
    uint32_t marker;
    memcpy(&marker, "WCON", sizeof(marker));
    wacca_touch_boot.magic = marker; // Preserve the volatile destination access.
    wacca_touch_boot.clock_hz = SystemCoreClock;
    wacca_touch_boot.stage = 1;
    if (wacca_clock_state != WACCA_CLOCK_READY) while (true) { }

    wacca_time_init();
    wacca_touch_irq_init();
    wacca_touch_boot.stage = 2;

    if (!wacca_usb2422_config(hub_image, sizeof(hub_image), 500, 100)) while (true) { };

    const enum wacca_hub_result status = wacca_usb2422_start(wacca_hub_interface(), hub_image, sizeof(hub_image));
    wacca_touch_boot.hub_result = (uint32_t)status;
    if (status != WACCA_HUB_OK)
    {
        wacca_touch_boot.stage = 0xE0;
        while (true) { }
    };
    wacca_usb_init(WACCA_ROLE_TOUCH);
    wacca_touch_boot.stage = 3;

    while (true)
    {
        wacca_touch_boot.pending_sectors = wacca_touch_irq_pending();
        wacca_diagnostics_poll(WACCA_ROLE_TOUCH, wacca_touch_boot.stage, wacca_touch_boot.pending_sectors, wacca_touch_boot.hub_result);
    }
}

