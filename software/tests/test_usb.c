#include "usb_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct wacca_usb_control c;
static enum wacca_control_result request(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t len,const uint8_t **data,uint16_t *n)
{
	const uint8_t raw[8]={type,req,(uint8_t)value,(uint8_t)(value>>8),(uint8_t)index,(uint8_t)(index>>8),(uint8_t)len,(uint8_t)(len>>8)};
	return wacca_usb_setup(&c,raw,data,n);
}
static void status_request(uint8_t type,uint8_t req,uint16_t value,uint16_t index)
{
	const uint8_t *p; uint16_t n;
	assert(request(type,req,value,index,0,&p,&n) == WACCA_CONTROL_STATUS);
	wacca_usb_control_commit(&c);
}
static void test_descriptors(void)
{
	const uint8_t *p; uint16_t n;
	assert(request(0x80,6,0x100,0,65535,&p,&n) == WACCA_CONTROL_IN);
	assert(n == 18 && p[0] == 18 && p[4] == 2 && p[5] == 2 && p[7] == 64);
	assert(request(0x80,6,0x200,0,65535,&p,&n) == WACCA_CONTROL_IN);
	assert(n == 67 && p[2] == n && p[4] == 2 && p[7] == 0x80 && p[8] == 0);
	unsigned endpoints=0, interfaces=0;
	for (unsigned off=0; off<n; off+=p[off]) {
		assert(p[off]>=2 && off+p[off]<=n);
		if (p[off+1] == 4) { ++interfaces; }
		if (p[off+1] == 5) { ++endpoints; assert(p[off+2] == 0x81 || p[off+2] == 2 || p[off+2] == 0x83); }
	}
	assert(endpoints == 3 && interfaces == 2);
	assert(request(0x80,6,0x200,0,9,&p,&n) == WACCA_CONTROL_IN && n == 9);
	for (uint16_t i=0; i<4; ++i) {
		assert(request(0x80,6,(uint16_t)(0x300U+i),0x0409,255,&p,&n) == WACCA_CONTROL_IN);
		assert(n <= sizeof(c.scratch) && p[0] == n && p[1] == 3 && n%2 == 0);
	}
	assert(request(0x80,6,0x304,0,255,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0x00,6,0x100,0,18,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0x80,6,0x101,0,18,&p,&n) == WACCA_CONTROL_STALL);
}
static void test_control(void)
{
	const uint8_t *p; uint16_t n;
	assert(request(0,5,42,0,0,&p,&n) == WACCA_CONTROL_STATUS && c.address == 0);
	wacca_usb_control_commit(&c); assert(c.address == 42);
	status_request(0,9,1,0); assert(c.configuration == 1);
	assert(request(0x21,0x20,0,0,7,&p,&n) == WACCA_CONTROL_OUT);
	const uint8_t coding[7]={0x80,0x25,0,0,0,0,8};
	assert(wacca_usb_control_out(&c,coding,7));
	assert(memcmp(c.line_coding,coding,7) != 0);
	wacca_usb_control_commit(&c); assert(memcmp(c.line_coding,coding,7) == 0);
	assert(request(0xA1,0x21,0,0,7,&p,&n) == WACCA_CONTROL_IN && n == 7 && memcmp(p,coding,7) == 0);
	assert(request(0x21,0x20,0,1,7,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0x21,0x20,0,0,65535,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0xA1,0x20,0,0,7,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0,5,50,0,0,&p,&n) == WACCA_CONTROL_STALL);
	status_request(0x21,0x22,3,0); assert(c.lines == 3);
	status_request(2,3,0,0x83); assert(c.halt_mask == 4);
	assert(request(0x82,0,0,0x83,2,&p,&n) == WACCA_CONTROL_IN && p[0] == 1);
	status_request(2,1,0,0x83); assert(c.halt_mask == 0);
	assert(request(2,3,0,0x8F,0,&p,&n) == WACCA_CONTROL_STALL);
	assert(request(0,3,1,0,0,&p,&n) == WACCA_CONTROL_STALL); // no remote wake
	assert(request(0x21,0x22,1,0,0,&p,&n) == WACCA_CONTROL_STATUS);
	assert(request(0x80,6,0x100,0,18,&p,&n) == WACCA_CONTROL_IN); // abort pending command
	wacca_usb_control_commit(&c); assert(c.lines == 3);
	status_request(0,9,0,0); assert(c.configuration == 0 && c.lines == 0 && c.halt_mask == 0);
	assert(request(0x21,0x22,1,0,0,&p,&n) == WACCA_CONTROL_STALL);
}
static void test_request_space(void)
{
	// Every bmRequestType/bRequest, bounded response regardless of hostile fields.
	const uint16_t fields[]={0,1,7,64,255,0xFFFF};
	for (unsigned type=0; type<256; ++type) {
		for (unsigned req=0; req<256; ++req) {
			for (unsigned i=0; i<sizeof(fields)/sizeof(fields[0]); ++i) {
				const uint8_t *p; uint16_t n;
				const enum wacca_control_result r=request((uint8_t)type,(uint8_t)req,fields[i],fields[i],fields[i],&p,&n);
				assert(n<=67 && n<=fields[i]);
				if (r == WACCA_CONTROL_IN) { assert(p != NULL && n != 0); }
				if (r == WACCA_CONTROL_OUT) { assert(n == 7); }
			}
		}
	}
}
static void test_pma(void)
{
	uint16_t memory[132]; uint8_t input[67],output[67];
	for (unsigned i=0; i<sizeof(input); ++i) { input[i]=(uint8_t)(i*37U); }
	for (size_t len=0; len<=65; ++len) {
		for (unsigned i=0; i<132; ++i) { memory[i]=0xBEEF; }
		memset(output,0xA5,sizeof(output));
		wacca_pma_write(memory+2,input+1,len);
		wacca_pma_read(output+1,memory+2,len);
		assert(memcmp(input+1,output+1,len) == 0);
		assert(output[0] == 0xA5 && output[len+1] == 0xA5);
		assert(memory[0] == 0xBEEF && memory[1] == 0xBEEF);
		for (size_t i=3; i<132; i+=2) { assert(memory[i] == 0xBEEF); }
		assert(memory[2+((len+1)/2)*2] == 0xBEEF);
	}
}
int main(void)
{
	wacca_usb_control_init(&c,WACCA_ROLE_TOUCH); test_descriptors(); test_control();
	wacca_usb_control_init(&c,WACCA_ROLE_LED); test_descriptors(); test_request_space(); test_pma();
	puts("USB control and PMA boundaries passed"); return 0;
}
