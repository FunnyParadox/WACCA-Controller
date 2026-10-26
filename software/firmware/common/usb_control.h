#ifndef WACCA_USB_CONTROL_H
#define WACCA_USB_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wacca_role { WACCA_ROLE_TOUCH = 1, WACCA_ROLE_LED = 2 };
enum wacca_control_result { WACCA_CONTROL_STALL, WACCA_CONTROL_IN, WACCA_CONTROL_OUT, WACCA_CONTROL_STATUS };
struct wacca_usb_setup { uint8_t type, request; uint16_t value, index, length; };
struct wacca_usb_control { enum wacca_role role; uint8_t address, configuration, halt_mask, lines, line_coding[7], scratch[64], pending_request, pending_value, pending_endpoint; bool pending; };

void wacca_usb_control_init(struct wacca_usb_control *c, enum wacca_role role);
enum wacca_control_result wacca_usb_setup(struct wacca_usb_control *c, const uint8_t setup[8], const uint8_t **data, uint16_t *length);
bool wacca_usb_control_out(struct wacca_usb_control *c, const uint8_t *data, size_t length);
void wacca_usb_control_commit(struct wacca_usb_control *c);
// PMA contains one 16-bit payload word per 32-bit slot
// No unaligned accesses, odd-length overreads, or writes to the unused half of each slot
void wacca_pma_write(volatile uint16_t *pma, const uint8_t *data, size_t length);
void wacca_pma_read(uint8_t *data, const volatile uint16_t *pma, size_t length);
#endif
