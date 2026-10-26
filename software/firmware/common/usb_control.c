#include "usb_control.h"
#include <string.h>

// pid.codes 1209:0001 is exclusively for private in-house TESTING
// Assign an authorized VID/PID before distributing devices or firmware
#ifndef WACCA_USB_VID
#define WACCA_USB_VID 0x1209
#endif
#ifndef WACCA_USB_PID
#define WACCA_USB_PID 0x0001
#endif

static const uint8_t device[] = { 18, 1, 0, 2, 2, 2, 1, 64, (WACCA_USB_VID >> 0) & 0xFF, (WACCA_USB_VID >> 8) & 0xFF, (WACCA_USB_PID >> 0) & 0xFF, (WACCA_USB_PID >> 8) & 0xFF, 0, 1, 1, 2, 3, 1 };
static const uint8_t configuration[] =
{
	9, 2, 67, 0, 2, 1, 0, 0x80, 0, // Permanent compound-hub child; hub budgets its power
	9, 4, 0, 0, 1, 2, 2, 1, 0, // CDC ACM communication interface
	5, 0x24, 0, 0x10, 1, // CDC 1.10
	5, 0x24, 1, 0, 1, // Call management; data interface 1
	4, 0x24, 2, 2, // SET/GET_LINE_CODING + control-line state
	5, 0x24, 6, 0, 1, // Union: master 0, slave 1
	7, 5, 0x81, 3, 16, 0, 16, // Notification IN
	9, 4, 1, 0, 2, 0x0A, 0, 0, 0,
	7, 5, 0x02, 2, 64, 0, 0, // Bulk OUT
	7, 5, 0x83, 2, 64, 0, 0, // Bulk IN
};
_Static_assert(sizeof(configuration) == 67, "CDC descriptor length");

static uint16_t le16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint8_t endpoint_bit(uint16_t endpoint) { switch (endpoint) { case 0x81: return 1; case 2: return 2; case 0x83: return 4; default: return 0; } }
static uint16_t string_descriptor(struct wacca_usb_control *c, uint8_t index)
{
	const char *s;
	switch (index)
	{
		case 0: c->scratch[0] = 4; c->scratch[1] = 3; c->scratch[2] = 9; c->scratch[3] = 4; return (4);
		case 1: s = "WACCA-Compatible Controller"; break;
		case 2: s = c->role == WACCA_ROLE_TOUCH ? "WACCA-Comp.Con: Touch Prototype" : "WACCA-Comp.Con: LED Prototype"; break;
		case 3: s = c->role == WACCA_ROLE_TOUCH ? "WCC-T-TEST" : "WCC-L-TEST"; break;
		default: return 0;
	}
	const size_t n = strlen(s);

	for (size_t i = 0; i < n; ++i)
	{
		c->scratch[2 + i * 2] = (uint8_t)(s[i]);
		c->scratch[3 + i * 2] = 0;
	}
	c->scratch[0] = (uint8_t)(2 + n * 2); c->scratch[1] = 3;

	return (c->scratch[0]);
}
void wacca_usb_control_init(struct wacca_usb_control *c, enum wacca_role role)
{
	memset(c, 0, sizeof(*c));
	c->role = role;
	const uint8_t coding[7] = { 0, 0xC2, 1, 0, 0, 0, 8 }; // 115200, informational only
	memcpy(c->line_coding, coding, sizeof(coding));
}
enum wacca_control_result wacca_usb_setup(struct wacca_usb_control *c, const uint8_t raw[8], const uint8_t **data, uint16_t *length)
{
	const struct wacca_usb_setup s = { raw[0], raw[1], le16(raw + 2), le16(raw + 4), le16(raw + 6)};

	c->pending=false;
	*data=NULL;
	*length=0;

	if (s.type == 0x80 && s.request == 6 && s.length != 0)
	{
		switch (s.value >> 8)
		{
			case 1:
			{
				if ((s.value & 255) || s.index) break;
				*data = device;
				*length = (uint16_t)(sizeof(device));
				break;
			}
			case 2:
			{
				if ((s.value & 255) || s.index) break;
				*data = configuration;
				*length = (uint16_t)(sizeof(configuration));
				break;
			}
			case 3:
			{
				if (s.index != 0 && s.index != 0x0409) break;
				*length = string_descriptor(c, (uint8_t)s.value);
				*data = c->scratch;
				break;
			}
			default: break;
		}
	}
	else if (s.type == 0x80 && s.request == 8 && s.value == 0 && s.index == 0 && s.length == 1)
	{
		c->scratch[0] = c->configuration;
		*data = c->scratch;
		*length = 1;
	}
	else if (s.request == 0 && s.value == 0 && s.length == 2)
	{
		if ((s.type == 0x80 && s.index == 0) || (c->configuration && s.type == 0x81 && s.index < 2) ||  (s.type == 0x82 && (s.index == 0 || s.index == 0x80 || (c->configuration && endpoint_bit(s.index)))))
		{
			c->scratch[0] = (s.type == 0x82 && (c->halt_mask & endpoint_bit(s.index)));
			c->scratch[1] = 0;
			*data = c->scratch;
			*length = 2;
		}
	}
	else if (s.type == 0x81 && s.request == 10 && c->configuration && s.index < 2 && s.value == 0 && s.length == 1)
	{
		c->scratch[0] = 0;
		*data = c->scratch;
		*length = 1;
	}
	else if (c->configuration && s.type == 0xA1 && s.request == 0x21 && s.value == 0 && s.index == 0 && s.length == 7)
	{
		*data=c->line_coding;
		*length=7;
	}
	else if (c->configuration && s.type == 0x21 && s.request == 0x20 && s.value == 0 && s.index == 0 && s.length == 7)
	{
		c->pending_request=s.request;
		c->pending=true;
		*length=7;
		return (WACCA_CONTROL_OUT);
	}
	else if (s.length == 0)
	{
		const bool address = s.type == 0 && s.request == 5 && s.value < 128 && s.index == 0 && !c->configuration;
		const bool config = s.type == 0 && s.request == 9 && s.value <= 1 && s.index == 0 && c->address != 0;
		const bool iface = s.type == 1 && s.request == 11 && c->configuration && s.value == 0 && s.index < 2;
		const bool lines = s.type == 0x21 && s.request == 0x22 && c->configuration && s.value < 4 && s.index == 0;
		const bool halt = s.type == 2 && (s.request == 1 || s.request == 3) && c->configuration && s.value == 0 && endpoint_bit(s.index);
		if (address || config || iface || lines || halt)
		{
			c->pending = true;
			c->pending_request = s.request;
			c->pending_value = (uint8_t)s.value;
			c->pending_endpoint = endpoint_bit(s.index);
			return (WACCA_CONTROL_STATUS);
		}
	}
	if (*length == 0) return (WACCA_CONTROL_STALL);
	if (*length > s.length) *length = s.length;

	return (WACCA_CONTROL_IN);
}
bool wacca_usb_control_out(struct wacca_usb_control *c, const uint8_t *data, size_t length)
{
	if (!c->pending || c->pending_request != 0x20 || length != 7 || data == NULL) return (false);
	// Baud has no effect on USB transfer rate, accept only representable CDC settings
	if (data[4] > 2 || data[5] > 4 || (data[6] != 5 && data[6] != 6 && data[6] != 7 && data[6] != 8 && data[6] != 16)) return (false);
	memcpy(c->scratch,data,7);
	return (true);
}
void wacca_usb_control_commit(struct wacca_usb_control *c)
{
	if (c->pending) c->pending = false; else return;

	switch (c->pending_request)
	{
		case 5: c->address = c->pending_value; break;
		case 9: c->configuration = c->pending_value; c->halt_mask = 0; c->lines = 0; break;
		case 0x20: memcpy(c->line_coding, c->scratch, 7); break;
		case 0x22: c->lines = c->pending_value; break;
		case 1: c->halt_mask &= (uint8_t)(~c->pending_endpoint); break;
		case 3: c->halt_mask |= c->pending_endpoint; break;
		default: break;
	}
}
void wacca_pma_write(volatile uint16_t *pma, const uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; i += 2)
	{
		uint16_t word = data[i];
		if (i + 1 < length) word |= (uint16_t)((uint16_t)data[i + 1] << 8);
		pma[i] = word;
	}
}
void wacca_pma_read(uint8_t *data, const volatile uint16_t *pma, size_t length)
{
	for (size_t i = 0; i < length; i += 2)
	{
		const uint16_t word = pma[i]; data[i] = (uint8_t)word;
		if (i + 1 < length) { data[i + 1]=(uint8_t)(word >> 8); }
	}
}
