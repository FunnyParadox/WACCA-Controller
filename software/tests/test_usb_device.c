#include "usb_sim.h"
#include "usb_device.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint16_t sim_endpoints[8],sim_cntr,sim_flags;
volatile uint32_t sim_daddr,sim_pma[256];
struct sim_exten sim_exten;
struct sim_exti sim_exti;
void GPIO_Init(void *gpio,const GPIO_InitTypeDef *pins) { (void)gpio; assert(pins->GPIO_Pin == ((1U<<11)|(1U<<12))); }
void RCC_APB1PeriphClockCmd(uint32_t peripheral,int enabled) { (void)peripheral; (void)enabled; }
void NVIC_EnableIRQ(int irq) { (void)irq; }
void NVIC_DisableIRQ(int irq) { (void)irq; }
void wacca_delay_us(uint32_t us) { (void)us; }
void USB_LP_CAN1_RX0_IRQHandler(void);
void sim_ep_write(unsigned ep,uint16_t value)
{
	assert(ep<8);
	const uint16_t old=sim_endpoints[ep];
	const uint16_t toggle=(uint16_t)((old^value)&(EP_DTOG_RX|EP_DTOG_TX|EPRX_STAT|EPTX_STAT));
	const uint16_t fields=value&(EP_T_FIELD|EP_KIND|EPADDR_FIELD);
	const uint16_t events=old&value&(EP_CTR_RX|EP_CTR_TX);
	const uint16_t setup=(events&EP_CTR_RX) ? old&EP_SETUP : 0;
	sim_endpoints[ep]=(uint16_t)(toggle|fields|events|setup);
}
uint16_t sim_istr(void)
{
	for (unsigned ep=0; ep<8; ++ep) {
		if (sim_endpoints[ep]&EP_CTR_RX) { return (uint16_t)(sim_flags|ISTR_CTR|ISTR_DIR|ep); }
		if (sim_endpoints[ep]&EP_CTR_TX) { return (uint16_t)(sim_flags|ISTR_CTR|ep); }
	}
	return sim_flags;
}
static uint16_t count(unsigned ep) { return (uint16_t)(sim_pma[(ep*8+2)/2]&0x3FF); }
static void out(unsigned ep,const uint8_t *bytes,uint16_t n,bool setup)
{
	assert(ep<4 && n<=64);
	assert(setup || (sim_endpoints[ep]&EPRX_STAT) == EP_RX_VALID);
	const uint16_t address=(uint16_t)sim_pma[(ep*8+4)/2];
	wacca_pma_write((volatile uint16_t *)sim_pma+address,bytes,n);
	sim_pma[(ep*8+6)/2]=0x8400U|n;
	sim_endpoints[ep]=(uint16_t)((sim_endpoints[ep]&~EPRX_STAT)|EP_RX_NAK|EP_CTR_RX|(setup ? EP_SETUP : 0));
	USB_LP_CAN1_RX0_IRQHandler();
}
static uint16_t in(unsigned ep,uint8_t *bytes)
{
	assert((sim_endpoints[ep]&EPTX_STAT) == EP_TX_VALID);
	const uint16_t n=count(ep); assert(n<=64);
	const uint16_t address=(uint16_t)sim_pma[(ep*8)/2];
	wacca_pma_read(bytes,(const volatile uint16_t *)sim_pma+address,n);
	sim_endpoints[ep]=(uint16_t)((sim_endpoints[ep]&~EPTX_STAT)|EP_TX_NAK|EP_CTR_TX);
	USB_LP_CAN1_RX0_IRQHandler(); return n;
}
static void setup(uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint16_t length)
{
	const uint8_t packet[8]={type,request,(uint8_t)value,(uint8_t)(value>>8),(uint8_t)index,(uint8_t)(index>>8),(uint8_t)length,(uint8_t)(length>>8)};
	out(0,packet,8,true);
}
static void status_request(uint8_t type,uint8_t request,uint16_t value,uint16_t index)
{
	uint8_t bytes[64]; setup(type,request,value,index,0); assert(in(0,bytes) == 0);
}
static void test_enumeration(void)
{
	uint8_t bytes[128];
	wacca_usb_init(WACCA_ROLE_TOUCH); assert(!wacca_usb_ready());
	assert((sim_daddr&0x7F) == 0);
	setup(0x80,6,0x100,0,18); assert(in(0,bytes) == 18 && bytes[0] == 18 && bytes[4] == 2); out(0,NULL,0,false);
	setup(0,5,23,0,0); assert((sim_daddr&0x7F) == 0); assert(in(0,bytes) == 0); assert((sim_daddr&0x7F) == 23);
	setup(0x80,6,0x200,0,255); assert(in(0,bytes) == 64); assert(in(0,bytes+64) == 3);
	assert(bytes[0] == 9 && bytes[2] == 67); out(0,NULL,0,false);
	setup(0,9,1,0,0); assert(!wacca_usb_ready()); assert(in(0,bytes) == 0); assert(wacca_usb_ready());
	setup(0x21,0x20,0,0,7);
	const uint8_t coding[7]={0x80,0x25,0,0,0,0,8}; out(0,coding,7,false); assert(in(0,bytes) == 0);
	setup(0xA1,0x21,0,0,7); assert(in(0,bytes) == 7 && memcmp(bytes,coding,7) == 0); out(0,NULL,0,false);
	// A malformed setup stalls safely, and the next valid SETUP recovers.
	const uint8_t malformed[7]={0}; out(0,malformed,7,true);
	assert((sim_endpoints[0]&EPTX_STAT) == EP_TX_STALL);
	setup(0x80,8,0,0,1); assert(in(0,bytes) == 1 && bytes[0] == 1); out(0,NULL,0,false);
	setup(0x21,0x20,0,0,7); out(0,malformed,6,false); assert((sim_endpoints[0]&EPTX_STAT) == EP_TX_STALL);
	status_request(0x21,0x22,3,0); assert(in(1,bytes) == 10 && bytes[8] == 3);
}
static void test_bulk(void)
{
	uint8_t bytes[64],readback[64];
	for (unsigned i=0; i<64; ++i) { bytes[i]=(uint8_t)i; }
	assert(wacca_usb_send(bytes,64)); assert(!wacca_usb_send(bytes,1));
	assert(in(3,readback) == 64 && memcmp(bytes,readback,64) == 0);
	assert(!wacca_usb_send(bytes,1)); assert(in(3,readback) == 0); // terminating ZLP
	assert(wacca_usb_send(bytes,1)); assert(in(3,readback) == 1);
	for (unsigned i=0; i<8; ++i) { out(2,bytes,64,false); }
	assert((sim_endpoints[2]&EPRX_STAT) == EP_RX_NAK);
	for (unsigned i=0; i<8; ++i) { assert(wacca_usb_read(readback,64) == 64 && memcmp(bytes,readback,64) == 0); }
	assert(wacca_usb_read(readback,64) == 0 && (sim_endpoints[2]&EPRX_STAT) == EP_RX_VALID);
	// Cross the 16-bit ring counter wrap multiple times without losing ordering.
	for (unsigned i=0; i<2200; ++i) { bytes[0]=(uint8_t)i; out(2,bytes,64,false); assert(wacca_usb_read(readback,64) == 64 && memcmp(bytes,readback,64) == 0); }
	status_request(2,3,0,0x83); assert(!wacca_usb_send(bytes,1));
	status_request(2,1,0,0x83); assert(wacca_usb_send(bytes,1)); assert(in(3,readback) == 1);
	status_request(2,3,0,2); assert((sim_endpoints[2]&EPRX_STAT) == EP_RX_STALL);
	status_request(2,1,0,2); assert((sim_endpoints[2]&EPRX_STAT) == EP_RX_VALID);
}
static void test_reset_suspend(void)
{
	uint8_t data[64]={1};
	sim_flags=ISTR_SUSP; USB_LP_CAN1_RX0_IRQHandler(); assert(!wacca_usb_ready() && !wacca_usb_send(data,1));
	sim_flags=ISTR_WKUP; USB_LP_CAN1_RX0_IRQHandler(); assert(wacca_usb_ready());
	assert(wacca_usb_send(data,10)); out(2,data,10,false);
	sim_flags=ISTR_SUSP; USB_LP_CAN1_RX0_IRQHandler(); assert(sim_cntr & CNTR_FSUSP);
	const uint32_t before=wacca_usb_generation();
	// Hardware reset clears endpoint status/toggles before raising RESET.
	memset(sim_endpoints,0,sizeof(sim_endpoints)); sim_flags=ISTR_RESET | ISTR_SUSP | ISTR_WKUP; USB_LP_CAN1_RX0_IRQHandler();
	assert(!wacca_usb_ready() && wacca_usb_generation() != before && wacca_usb_read(data,64) == 0);
	assert((sim_cntr & (CNTR_FSUSP | CNTR_LPMODE)) == 0);
	status_request(0,5,7,0); status_request(0,9,1,0);
	assert(wacca_usb_send(data,1)); assert(in(3,data) == 1);
	struct wacca_usb_stats stats; wacca_usb_get_stats(&stats); assert(stats.errors == 0);
}
int main(void)
{
	test_enumeration(); test_bulk(); test_reset_suspend();
	wacca_usb_detach();
	assert(!wacca_usb_ready() && wacca_usb_tx_idle());
	assert((EXTEN->EXTEN_CTR & EXTEN_USBD_PU_EN) == 0);
	puts("USBD simulated enumeration, flow control, halt, reset and suspend passed"); return 0;
}


