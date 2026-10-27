#include "i2c_sim.h"
#include "i2c.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum phase { IDLE, STARTING, ADDRESS, TRANSMIT, RECEIVE };
static struct simulation {
	enum phase phase;
	unsigned starts, stops, resets, addresses, tx_count;
	uint16_t stall, error, data_error;
	unsigned nack_address, nack_data;
	bool read_direction, status_read, stop_pending, stuck_stop, stuck_busy;
	bool clock_enabled, alternate_pins;
	uint8_t wire_addresses[32], sent[1024], input[256];
	size_t sent_length, input_length, input_offset;
} simulation[WACCA_I2C_BUS_COUNT];
static unsigned selected, critical_depth;
static uint32_t time_us, pclk, irq;
static bool afio_enabled, remap_disabled;
I2C_TypeDef simulated_i2c_regs[WACCA_I2C_BUS_COUNT];
#define sim simulation[selected]
#define regs simulated_i2c_regs[selected]
#define TEST_BUS ((enum wacca_i2c_bus)selected)
GPIO_TypeDef simulated_gpio_b;
#define FIELD(name) offsetof(I2C_TypeDef, name)
static void finish_stop(unsigned bus)
{
	simulated_i2c_regs[bus].CTLR1 &= (uint16_t)~I2C_CTLR1_STOP;
	simulated_i2c_regs[bus].STAR1 = 0; simulated_i2c_regs[bus].STAR2 = 0;
	simulation[bus].phase = IDLE; simulation[bus].stop_pending = false;
}
uint16_t wacca_i2c_sim_read(unsigned bus, size_t offset)
{
	if (offset == FIELD(STAR1)) {
		if (simulation[bus].phase == RECEIVE) {
			simulated_i2c_regs[bus].STAR1 = I2C_STAR1_RXNE;
			if (simulation[bus].input_length - simulation[bus].input_offset >= 2) simulated_i2c_regs[bus].STAR1 |= I2C_STAR1_BTF;
		}
		simulation[bus].status_read = true;
		return (uint16_t)((simulated_i2c_regs[bus].STAR1 | simulation[bus].error) & (uint16_t)~simulation[bus].stall);
	}
	if (offset == FIELD(STAR2)) {
		const uint16_t result = simulated_i2c_regs[bus].STAR2;
		if (simulation[bus].status_read && (simulated_i2c_regs[bus].STAR1 & I2C_STAR1_ADDR)) {
			simulated_i2c_regs[bus].STAR1 &= (uint16_t)~I2C_STAR1_ADDR;
			if (simulation[bus].read_direction) {
				const uint16_t mode = simulated_i2c_regs[bus].CTLR1 & (I2C_CTLR1_ACK | I2C_CTLR1_POS);
				if (simulation[bus].input_length == 1) assert(mode == 0 && critical_depth == 1);
				else if (simulation[bus].input_length == 2) assert(mode == (I2C_CTLR1_ACK | I2C_CTLR1_POS) && critical_depth == 1);
				else assert(mode == I2C_CTLR1_ACK);
				simulation[bus].phase = RECEIVE;
			} else {
				simulation[bus].phase = TRANSMIT;
				simulated_i2c_regs[bus].STAR1 = I2C_STAR1_TXE | I2C_STAR1_BTF;
			}
		}
		simulation[bus].status_read = false;
		return (uint16_t)(result | (simulation[bus].stuck_busy ? I2C_STAR2_BUSY : 0));
	}
	if (offset == FIELD(DATAR)) {
		assert(simulation[bus].phase == RECEIVE && simulation[bus].input_offset < simulation[bus].input_length);
		if (simulation[bus].input_length - simulation[bus].input_offset <= 2) assert(simulation[bus].stop_pending);
		const uint8_t byte = simulation[bus].input[simulation[bus].input_offset++];
		if (simulation[bus].input_offset == simulation[bus].input_length && simulation[bus].stop_pending && !simulation[bus].stuck_stop) finish_stop(bus);
		return byte;
	}
	if (offset == FIELD(CTLR1)) return simulated_i2c_regs[bus].CTLR1;
	assert(false); return 0;
}
void wacca_i2c_sim_write(unsigned bus, size_t offset, uint16_t value)
{
	if (offset == FIELD(CTLR1)) {
		const uint16_t previous = simulated_i2c_regs[bus].CTLR1;
		simulated_i2c_regs[bus].CTLR1 = value;
		if (!(value & I2C_CTLR1_PE)) {
			simulation[bus].phase = IDLE; simulated_i2c_regs[bus].STAR1 = 0; simulated_i2c_regs[bus].STAR2 = 0; return;
		}
		if ((value & I2C_CTLR1_START) && !(previous & I2C_CTLR1_START)) {
			++simulation[bus].starts; simulation[bus].phase = STARTING;
			simulated_i2c_regs[bus].STAR1 = I2C_STAR1_SB;
			simulated_i2c_regs[bus].STAR2 = I2C_STAR2_MSL | I2C_STAR2_BUSY;
			simulated_i2c_regs[bus].CTLR1 &= (uint16_t)~I2C_CTLR1_START;
		}
		if ((previous & I2C_CTLR1_ACK) && !(value & I2C_CTLR1_ACK) && simulation[bus].phase == RECEIVE) {
			assert(critical_depth == 1);
			if (simulation[bus].input_length == 2) assert(value & I2C_CTLR1_POS);
			else assert(simulation[bus].input_length - simulation[bus].input_offset == 3);
		}
		if ((value & I2C_CTLR1_STOP) && !(previous & I2C_CTLR1_STOP)) {
			assert(!(simulation[bus].error & I2C_STAR1_ARLO));
			++simulation[bus].stops; simulation[bus].stop_pending = true;
			if (simulation[bus].phase == RECEIVE && !simulation[bus].error) {
				assert(critical_depth == 1 && !(value & I2C_CTLR1_ACK));
				assert(simulation[bus].input_length - simulation[bus].input_offset == (simulation[bus].input_length == 1 ? 1U : 2U));
			} else if (!simulation[bus].stuck_stop) finish_stop(bus);
		}
		return;
	}
	if (offset == FIELD(DATAR)) {
		if (simulation[bus].phase == STARTING) {
			assert(simulation[bus].addresses < sizeof(simulation[bus].wire_addresses));
			simulation[bus].wire_addresses[simulation[bus].addresses++] = (uint8_t)value;
			simulation[bus].read_direction = (value & 1U) != 0; simulation[bus].input_offset = 0;
			simulation[bus].phase = ADDRESS;
			simulated_i2c_regs[bus].STAR1 = simulation[bus].nack_address == simulation[bus].addresses ? I2C_STAR1_AF : I2C_STAR1_ADDR;
		} else {
			assert(simulation[bus].phase == TRANSMIT && simulation[bus].sent_length < sizeof(simulation[bus].sent));
			simulation[bus].sent[simulation[bus].sent_length++] = (uint8_t)value; ++simulation[bus].tx_count;
			simulated_i2c_regs[bus].STAR1 = simulation[bus].nack_data == simulation[bus].tx_count ? I2C_STAR1_AF : I2C_STAR1_TXE | I2C_STAR1_BTF;
			if (simulation[bus].tx_count == 1) simulation[bus].error = simulation[bus].data_error;
		}
		return;
	}
	if (offset == FIELD(CTLR2)) simulated_i2c_regs[bus].CTLR2 = value;
	else if (offset == FIELD(CKCFGR)) simulated_i2c_regs[bus].CKCFGR = value;
	else if (offset == FIELD(RTR)) simulated_i2c_regs[bus].RTR = value;
	else if (offset == FIELD(OADDR1)) simulated_i2c_regs[bus].OADDR1 = value;
	else if (offset == FIELD(OADDR2)) simulated_i2c_regs[bus].OADDR2 = value;
	else assert(false);
}
uint32_t wacca_i2c_sim_irq_save(void)
{
	assert(critical_depth == 0); ++critical_depth;
	const uint32_t saved = irq; irq = 0; return saved;
}
void wacca_i2c_sim_irq_restore(uint32_t saved)
{ assert(critical_depth == 1); --critical_depth; irq = saved; }
uint32_t wacca_now_us(void) { assert(critical_depth == 0); return time_us++; }
void wacca_delay_us(uint32_t us) { time_us += us; }
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins) { assert(port == GPIOB); port->OUTDR |= pins; }
void GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *pins)
{
	assert(port == GPIOB);
	const unsigned bus = pins->GPIO_Pin == (GPIO_Pin_6 | GPIO_Pin_7) ? 0U : 1U;
	assert(pins->GPIO_Pin == (bus == 0 ? (GPIO_Pin_6 | GPIO_Pin_7) : (GPIO_Pin_10 | GPIO_Pin_11)));
	assert(pins->GPIO_Mode == GPIO_Mode_Out_OD || pins->GPIO_Mode == GPIO_Mode_AF_OD);
	simulation[bus].alternate_pins = pins->GPIO_Mode == GPIO_Mode_AF_OD;
}
static unsigned peripheral_bus(uint32_t peripheral)
{
	assert(peripheral == RCC_APB1Periph_I2C1 || peripheral == RCC_APB1Periph_I2C2);
	return peripheral == RCC_APB1Periph_I2C1 ? 0U : 1U;
}
void RCC_APB1PeriphClockCmd(uint32_t peripheral, int enabled)
{ assert(enabled == ENABLE); simulation[peripheral_bus(peripheral)].clock_enabled = true; }
void RCC_APB1PeriphResetCmd(uint32_t peripheral, int enabled)
{
	const unsigned bus = peripheral_bus(peripheral);
	if (enabled) {
		++simulation[bus].resets;
		memset(&simulated_i2c_regs[bus], 0, sizeof(simulated_i2c_regs[bus]));
		simulation[bus].phase = IDLE;
	}
}
void RCC_APB2PeriphClockCmd(uint32_t peripheral, int enabled)
{ assert(peripheral == RCC_APB2Periph_AFIO && enabled == ENABLE); afio_enabled = true; }
void GPIO_PinRemapConfig(uint32_t remap, int enabled)
{ assert(afio_enabled && remap == GPIO_Remap_I2C1 && enabled == DISABLE); remap_disabled = true; }
void RCC_GetClocksFreq(RCC_ClocksTypeDef *clocks) { clocks->PCLK1_Frequency = pclk; }
static void fresh(void)
{
	memset(simulation, 0, sizeof(simulation));
	memset(simulated_i2c_regs, 0, sizeof(simulated_i2c_regs));
	pclk = 36000000; irq = 0x88; time_us = 0; critical_depth = 0;
	afio_enabled = remap_disabled = false;
	GPIOB->INDR = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIOB->OUTDR = 0;
	assert(wacca_i2c_init(TEST_BUS, WACCA_I2C_STANDARD_HZ));
	assert(sim.clock_enabled && sim.alternate_pins);
	if (selected == WACCA_I2C_BUS1) assert(afio_enabled && remap_disabled);
	assert(regs.CTLR2 == 36 && regs.CKCFGR == 180 && regs.RTR == 37);
}
static void check_reads(void)
{
	const uint8_t reg = 0xFA;
	const size_t lengths[] = {1, 2, 3, 4, 17, 256};
	for (unsigned mode = 0; mode < 4; ++mode) {
	  const bool fast = (mode & 1U) != 0, direct = (mode & 2U) != 0;
	  for (size_t test = 0; test < sizeof(lengths) / sizeof(lengths[0]); ++test) {
		fresh();
		if (fast) {
			assert(wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_FAST_HZ));
			assert(regs.CKCFGR == (I2C_CKCFGR_FS | 30U) && regs.RTR == 11);
		}
		sim.input_length = lengths[test];
		for (size_t i = 0; i < sim.input_length; ++i) sim.input[i] = (uint8_t)(i ^ 0xA5U);
		uint8_t received[258]; memset(received, 0xCC, sizeof(received));
		assert(wacca_i2c_transfer(TEST_BUS, 0x30, direct ? NULL : &reg, direct ? 0U : 1U, received + 1, sim.input_length));
		assert(received[0] == 0xCC && received[sim.input_length + 1] == 0xCC);
		assert(memcmp(received + 1, sim.input, sim.input_length) == 0);
		assert(sim.starts == (direct ? 1U : 2U) && sim.stops == 1);
		assert(sim.wire_addresses[direct ? 0 : 1] == 0x61);
		assert(sim.sent_length == (direct ? 0U : 1U) && sim.input_offset == sim.input_length);
		if (!direct) assert(sim.wire_addresses[0] == 0x60 && sim.sent[0] == reg);
		assert(irq == 0x88 && critical_depth == 0);
		assert(regs.CTLR1 == (I2C_CTLR1_PE | I2C_CTLR1_ACK));
	  }
	}
}
static void check_speed(void)
{
	const uint32_t clocks[] = {4000000, 8000000, 36000000, 60000000};
	for (size_t i = 0; i < sizeof(clocks) / sizeof(clocks[0]); ++i) {
		fresh(); pclk = clocks[i];
		assert(wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_FAST_HZ));
		const uint32_t divider = regs.CKCFGR & I2C_CKCFGR_CCR;
		assert((regs.CKCFGR & I2C_CKCFGR_FS) != 0);
		assert(divider * 3U * WACCA_I2C_FAST_HZ >= pclk);
		assert(regs.CTLR2 == pclk / 1000000U);
		assert(regs.RTR == (pclk / 1000000U * 3U / 10U + 1U));
	}
	fresh();
	assert(wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_FAST_HZ));
	const unsigned resets = sim.resets;
	assert(!wacca_i2c_set_speed(TEST_BUS, 1000000) && !wacca_i2c_set_speed(TEST_BUS, 0));
	assert(sim.resets == resets && regs.CKCFGR == (I2C_CKCFGR_FS | 30U));
	// Error recovery must not silently slow the next touch read to 100 kHz.
	uint8_t byte;
	sim.nack_address = 1; sim.input_length = 1; sim.input[0] = 0xA5;
	assert(!wacca_i2c_transfer(TEST_BUS, 0x30, NULL, 0, &byte, 1));
	assert(regs.CTLR1 == 0);
	sim.nack_address = 0;
	assert(wacca_i2c_transfer(TEST_BUS, 0x30, NULL, 0, &byte, 1) && byte == 0xA5);
	assert(regs.CKCFGR == (I2C_CKCFGR_FS | 30U) && regs.RTR == 11);
	assert(wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_STANDARD_HZ));
	assert(regs.CKCFGR == 180 && regs.RTR == 37);
	assert(wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_FAST_HZ) && wacca_i2c_init(TEST_BUS, WACCA_I2C_STANDARD_HZ));
	assert(regs.CKCFGR == 180); // Explicit reinitialization selects the requested speed.
	fresh(); sim.stuck_busy = true;
	assert(!wacca_i2c_set_speed(TEST_BUS, WACCA_I2C_FAST_HZ));
	assert(regs.CTLR1 == 0 && sim.starts == 0);
}
static void check_bus(void)
{
	const uint8_t tx[] = {0xFA, 1, 0}; uint8_t rx[2];
	fresh(); assert(wacca_i2c_transfer(TEST_BUS, 0x2C, tx, sizeof(tx), NULL, 0));
	assert(sim.starts == 1 && sim.stops == 1 && sim.wire_addresses[0] == 0x58);
	assert(sim.sent_length == sizeof(tx) && memcmp(sim.sent, tx, sizeof(tx)) == 0);
	check_reads();
	check_speed();
	for (unsigned address = 1; address <= 2; ++address) {
		fresh(); sim.nack_address = address; sim.input_length = 2;
		assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, rx, 2));
		assert(regs.CTLR1 == 0 && sim.stops == 1);
		sim.nack_address = 0;
		assert(wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, NULL, 0));
	}
	fresh(); sim.nack_data = 3;
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, sizeof(tx), NULL, 0) && regs.CTLR1 == 0);
	const uint16_t stalled[] = {I2C_STAR1_SB, I2C_STAR1_ADDR, I2C_STAR1_TXE, I2C_STAR1_BTF, I2C_STAR1_RXNE};
	for (size_t i = 0; i < sizeof(stalled) / sizeof(stalled[0]); ++i) {
		fresh(); sim.stall = stalled[i]; sim.input_length = 1; time_us = UINT32_MAX - 100U;
		const uint32_t began = time_us;
		assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, rx, 1));
		assert((uint32_t)(time_us - began) >= 20000 && (uint32_t)(time_us - began) < 21100);
		assert(regs.CTLR1 == 0 && critical_depth == 0);
	}
	const uint16_t errors[] = {I2C_STAR1_BERR, I2C_STAR1_ARLO, I2C_STAR1_OVR, I2C_STAR1_TIMEOUT};
	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
		fresh(); sim.data_error = errors[i];
		assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, NULL, 0));
		assert(regs.CTLR1 == 0 && sim.stops == (errors[i] == I2C_STAR1_ARLO ? 0U : 1U));
	}
	fresh(); sim.stuck_stop = true;
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, sizeof(tx), NULL, 0));
	assert(time_us < 21200 && regs.CTLR1 == 0);
	fresh(); sim.stuck_busy = true;
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, NULL, 0) && sim.starts == 0);
	fresh(); GPIOB->INDR = 0;
	assert(!wacca_i2c_init(TEST_BUS, WACCA_I2C_STANDARD_HZ) && regs.CTLR1 == 0);
	fresh(); pclk = 72000000; assert(!wacca_i2c_init(TEST_BUS, WACCA_I2C_STANDARD_HZ));
	fresh(); const unsigned starts = sim.starts;
	assert(!wacca_i2c_transfer(TEST_BUS, 0x80, tx, 1, NULL, 0));
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, NULL, 1, NULL, 0));
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 257, NULL, 0));
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, NULL, 2));
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, NULL, 0, NULL, 0));
	assert(!wacca_i2c_transfer(TEST_BUS, 0x2C, NULL, 0, rx, 257));
	assert(sim.starts == starts);
	fresh(); irq = 0; sim.input_length = 2;
	assert(wacca_i2c_transfer(TEST_BUS, 0x2C, tx, 1, rx, 2) && irq == 0);
}
static void fresh_both(void)
{
	selected = WACCA_I2C_BUS1;
	fresh();
	assert(wacca_i2c_init(WACCA_I2C_BUS2, WACCA_I2C_STANDARD_HZ));
	assert(wacca_i2c_set_speed(WACCA_I2C_BUS1, WACCA_I2C_FAST_HZ));
	// I2C1 changes must not change the independent second bus.
	assert(simulated_i2c_regs[1].CKCFGR == 180);
	assert(wacca_i2c_set_speed(WACCA_I2C_BUS2, WACCA_I2C_FAST_HZ));
	for (unsigned bus = 0; bus < WACCA_I2C_BUS_COUNT; ++bus) {
		simulation[bus].input_length = 3;
		for (size_t i = 0; i < 3; ++i) simulation[bus].input[i] = (uint8_t)(bus * 80U + i);
	}
}
static void poll_both(void)
{
	(void)wacca_i2c_poll(WACCA_I2C_BUS1);
	(void)wacca_i2c_poll(WACCA_I2C_BUS2);
}
static void drain_both(void)
{
	for (unsigned i = 0; i < 22000; ++i) {
		const enum wacca_i2c_status a = wacca_i2c_poll(WACCA_I2C_BUS1);
		const enum wacca_i2c_status b = wacca_i2c_poll(WACCA_I2C_BUS2);
		if (a != WACCA_I2C_PENDING && b != WACCA_I2C_PENDING) return;
	}
	assert(false);
}
static void check_parallel(void)
{
	uint8_t a[258], b[258];
	// Exercise independent 1/2/N receive endings, using the same slave address
	// on separate buses. Both STARTs must occur before either receive completes.
	const size_t lengths[] = {1, 2, 3, 4, 17, 256};
	for (size_t first = 0; first < sizeof(lengths) / sizeof(lengths[0]); ++first) {
		for (size_t second = 0; second < sizeof(lengths) / sizeof(lengths[0]); ++second) {
			fresh_both();
			simulation[0].input_length = lengths[first];
			simulation[1].input_length = lengths[second];
			for (unsigned bus = 0; bus < WACCA_I2C_BUS_COUNT; ++bus) {
				for (size_t i = 0; i < simulation[bus].input_length; ++i) {
					simulation[bus].input[i] = (uint8_t)(i ^ (bus ? 0x5A : 0xA5));
				}
			}
			memset(a, 0xCC, sizeof(a)); memset(b, 0xDD, sizeof(b));
			assert(wacca_i2c_begin(WACCA_I2C_BUS1, 0x30, NULL, 0, a + 1, lengths[first]));
			assert(wacca_i2c_begin(WACCA_I2C_BUS2, 0x30, NULL, 0, b + 1, lengths[second]));
			poll_both();
			assert(simulation[0].starts == 1 && simulation[1].starts == 1);
			assert(simulation[0].input_offset == 0 && simulation[1].input_offset == 0);
			const unsigned reset_a = simulation[0].resets, reset_b = simulation[1].resets;
			assert(!wacca_i2c_begin(WACCA_I2C_BUS1, 0x31, NULL, 0, b, 1));
			assert(!wacca_i2c_init(WACCA_I2C_BUS1, WACCA_I2C_STANDARD_HZ));
			assert(!wacca_i2c_set_speed(WACCA_I2C_BUS2, WACCA_I2C_STANDARD_HZ));
			assert(!wacca_i2c_transfer(WACCA_I2C_BUS2, 0x31, NULL, 0, a, 1));
			assert(simulation[0].resets == reset_a && simulation[1].resets == reset_b);
			drain_both();
			assert(wacca_i2c_poll(WACCA_I2C_BUS1) == WACCA_I2C_COMPLETE);
			assert(wacca_i2c_poll(WACCA_I2C_BUS2) == WACCA_I2C_COMPLETE);
			assert(memcmp(a + 1, simulation[0].input, lengths[first]) == 0);
			assert(memcmp(b + 1, simulation[1].input, lengths[second]) == 0);
			assert(a[0] == 0xCC && a[lengths[first] + 1] == 0xCC);
			assert(b[0] == 0xDD && b[lengths[second] + 1] == 0xDD);
			assert(critical_depth == 0 && irq == 0x88);
		}
	}
	// A healthy transfer completes while the other bus waits on a stalled
	// hardware event. Timeout/recovery must not reset or slow the healthy bus.
	for (unsigned failed = 0; failed < WACCA_I2C_BUS_COUNT; ++failed) {
		const unsigned healthy = 1U - failed;
		fresh_both();
		simulation[failed].stall = I2C_STAR1_SB;
		assert(wacca_i2c_begin(WACCA_I2C_BUS1, 0x30, NULL, 0, a, 3));
		assert(wacca_i2c_begin(WACCA_I2C_BUS2, 0x30, NULL, 0, b, 3));
		for (unsigned i = 0; i < 100; ++i) poll_both();
		assert(wacca_i2c_poll((enum wacca_i2c_bus)healthy) == WACCA_I2C_COMPLETE);
		assert(wacca_i2c_poll((enum wacca_i2c_bus)failed) == WACCA_I2C_PENDING);
		drain_both();
		assert(wacca_i2c_poll((enum wacca_i2c_bus)failed) == WACCA_I2C_ERROR);
		assert(simulated_i2c_regs[failed].CTLR1 == 0);
		const unsigned healthy_resets = simulation[healthy].resets;
		const unsigned failed_resets = simulation[failed].resets;
		simulation[failed].stall = 0;
		// Recovery also progresses asynchronously when an external line is low.
		const uint16_t failed_pins = failed == 0 ? GPIO_Pin_6 | GPIO_Pin_7 : GPIO_Pin_10 | GPIO_Pin_11;
		GPIOB->INDR &= (uint32_t)~failed_pins;
		const uint32_t began = time_us;
		assert(wacca_i2c_begin((enum wacca_i2c_bus)failed, 0x30, NULL, 0, a, 3));
		assert((uint32_t)(time_us - began) < 5);
		assert(wacca_i2c_begin((enum wacca_i2c_bus)healthy, 0x30, NULL, 0, b, 3));
		for (unsigned i = 0; i < 100; ++i) poll_both();
		assert(wacca_i2c_poll((enum wacca_i2c_bus)healthy) == WACCA_I2C_COMPLETE);
		assert(simulation[healthy].resets == healthy_resets);
		drain_both();
		assert(wacca_i2c_poll((enum wacca_i2c_bus)failed) == WACCA_I2C_ERROR);
		GPIOB->INDR |= failed_pins;
		assert(wacca_i2c_transfer((enum wacca_i2c_bus)failed, 0x30, NULL, 0, a, 3));
		assert(simulated_i2c_regs[failed].CKCFGR == (I2C_CKCFGR_FS | 30U));
		assert(simulation[failed].resets == failed_resets + 1);
		assert(simulation[healthy].resets == healthy_resets);
	}
	fresh_both();
	simulation[0].nack_address = 1;
	assert(wacca_i2c_begin(WACCA_I2C_BUS1, 0x30, NULL, 0, a, 3));
	assert(wacca_i2c_transfer(WACCA_I2C_BUS2, 0x30, NULL, 0, b, 3));
	// Even the blocking convenience wrapper advances the other bus's failure.
	assert(wacca_i2c_poll(WACCA_I2C_BUS1) == WACCA_I2C_ERROR);
	assert(memcmp(b, simulation[1].input, 3) == 0);
	const unsigned starts = simulation[0].starts + simulation[1].starts;
	const enum wacca_i2c_bus invalid[] = {(enum wacca_i2c_bus)-1, WACCA_I2C_BUS_COUNT};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		assert(!wacca_i2c_begin(invalid[i], 0x30, NULL, 0, a, 3));
		assert(!wacca_i2c_init(invalid[i], WACCA_I2C_FAST_HZ));
		assert(!wacca_i2c_set_speed(invalid[i], WACCA_I2C_FAST_HZ));
		assert(!wacca_i2c_transfer(invalid[i], 0x30, NULL, 0, a, 3));
		assert(wacca_i2c_poll(invalid[i]) == WACCA_I2C_ERROR);
	}
	assert(simulation[0].starts + simulation[1].starts == starts);
}
int main(void)
{
	for (selected = 0; selected < WACCA_I2C_BUS_COUNT; ++selected) check_bus();
	check_parallel();
	puts("Hardware I2C1/I2C2: independent 100/400 kHz timing, simultaneous transfers, receive endings, bus-local fault recovery passed.");
	return 0;
}
