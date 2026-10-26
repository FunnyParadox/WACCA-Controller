#ifndef WACCA_USB_SIM_H
#define WACCA_USB_SIM_H
#include "ch32v20x.h"
#include "usb_regs.h"
extern uint16_t sim_endpoints[8];
extern volatile uint32_t sim_daddr;
extern volatile uint32_t sim_pma[256];
extern uint16_t sim_cntr, sim_flags;
uint16_t sim_istr(void);
void sim_ep_write(unsigned ep,uint16_t value);
#undef _GetENDPOINT
#undef _SetENDPOINT
#undef _GetISTR
#undef _SetISTR
#undef _GetCNTR
#undef _SetCNTR
#undef _SetBTABLE
#undef DADDR
#define _GetENDPOINT(ep) (sim_endpoints[(ep)])
#define _SetENDPOINT(ep,value) sim_ep_write((ep),(uint16_t)(value))
#define _GetISTR() sim_istr()
#define _SetISTR(value) (sim_flags &= (uint16_t)(value))
#define _GetCNTR() sim_cntr
#define _SetCNTR(value) (sim_cntr=(uint16_t)(value))
#define _SetBTABLE(value) ((void)(value))
#define DADDR (&sim_daddr)
#endif
