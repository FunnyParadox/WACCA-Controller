#include "i2c_sim.h"
#include "i2c.h"
#include "usb2422.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum phase { IDLE, STARTING, ADDRESS, TRANSMIT, RECEIVE };
static struct {
    I2C_TypeDef regs;
    enum phase phase;
    uint32_t time, pclk, irq;
    unsigned starts, stops, resets, critical_depth, addresses, tx_count;
    uint16_t stall, error, data_error;
    unsigned nack_address, nack_data;
    bool read_direction, status_read, stop_pending, stuck_stop, stuck_busy;
    bool clock_enabled, alternate_pins, hub_reset;
    uint8_t wire_addresses[32], sent[1024], input[256];
    size_t sent_length, input_length, input_offset;
} sim;
GPIO_TypeDef simulated_gpio_b;
#define FIELD(name) offsetof(I2C_TypeDef, name)
static void finish_stop(void)
{
    sim.regs.CTLR1 &= (uint16_t)~I2C_CTLR1_STOP;
    sim.regs.STAR1 = 0; sim.regs.STAR2 = 0;
    sim.phase = IDLE; sim.stop_pending = false;
}
uint16_t wacca_i2c_sim_read(size_t offset)
{
    if (offset == FIELD(STAR1)) {
        if (sim.phase == RECEIVE) {
            sim.regs.STAR1 = I2C_STAR1_RXNE;
            if (sim.input_length - sim.input_offset >= 2) sim.regs.STAR1 |= I2C_STAR1_BTF;
        }
        sim.status_read = true;
        return (uint16_t)((sim.regs.STAR1 | sim.error) & (uint16_t)~sim.stall);
    }
    if (offset == FIELD(STAR2)) {
        const uint16_t result = sim.regs.STAR2;
        if (sim.status_read && (sim.regs.STAR1 & I2C_STAR1_ADDR)) {
            sim.regs.STAR1 &= (uint16_t)~I2C_STAR1_ADDR;
            if (sim.read_direction) {
                const uint16_t mode = sim.regs.CTLR1 & (I2C_CTLR1_ACK | I2C_CTLR1_POS);
                if (sim.input_length == 1) assert(mode == 0 && sim.critical_depth == 1);
                else if (sim.input_length == 2) assert(mode == (I2C_CTLR1_ACK | I2C_CTLR1_POS) && sim.critical_depth == 1);
                else assert(mode == I2C_CTLR1_ACK);
                sim.phase = RECEIVE;
            } else {
                sim.phase = TRANSMIT;
                sim.regs.STAR1 = I2C_STAR1_TXE | I2C_STAR1_BTF;
            }
        }
        sim.status_read = false;
        return (uint16_t)(result | (sim.stuck_busy ? I2C_STAR2_BUSY : 0));
    }
    if (offset == FIELD(DATAR)) {
        assert(sim.phase == RECEIVE && sim.input_offset < sim.input_length);
        if (sim.input_length - sim.input_offset <= 2) assert(sim.stop_pending);
        const uint8_t byte = sim.input[sim.input_offset++];
        if (sim.input_offset == sim.input_length && sim.stop_pending && !sim.stuck_stop) finish_stop();
        return byte;
    }
    if (offset == FIELD(CTLR1)) return sim.regs.CTLR1;
    assert(false); return 0;
}
void wacca_i2c_sim_write(size_t offset, uint16_t value)
{
    if (offset == FIELD(CTLR1)) {
        const uint16_t previous = sim.regs.CTLR1;
        sim.regs.CTLR1 = value;
        if (!(value & I2C_CTLR1_PE)) {
            sim.phase = IDLE; sim.regs.STAR1 = 0; sim.regs.STAR2 = 0; return;
        }
        if ((value & I2C_CTLR1_START) && !(previous & I2C_CTLR1_START)) {
            ++sim.starts; sim.phase = STARTING;
            sim.regs.STAR1 = I2C_STAR1_SB;
            sim.regs.STAR2 = I2C_STAR2_MSL | I2C_STAR2_BUSY;
            sim.regs.CTLR1 &= (uint16_t)~I2C_CTLR1_START;
        }
        if ((previous & I2C_CTLR1_ACK) && !(value & I2C_CTLR1_ACK) && sim.phase == RECEIVE) {
            assert(sim.critical_depth == 1);
            if (sim.input_length == 2) assert(value & I2C_CTLR1_POS);
            else assert(sim.input_length - sim.input_offset == 3);
        }
        if ((value & I2C_CTLR1_STOP) && !(previous & I2C_CTLR1_STOP)) {
            assert(!(sim.error & I2C_STAR1_ARLO));
            ++sim.stops; sim.stop_pending = true;
            if (sim.phase == RECEIVE && !sim.error) {
                assert(sim.critical_depth == 1 && !(value & I2C_CTLR1_ACK));
                assert(sim.input_length - sim.input_offset == (sim.input_length == 1 ? 1U : 2U));
            } else if (!sim.stuck_stop) finish_stop();
        }
        return;
    }
    if (offset == FIELD(DATAR)) {
        if (sim.phase == STARTING) {
            assert(sim.addresses < sizeof(sim.wire_addresses));
            sim.wire_addresses[sim.addresses++] = (uint8_t)value;
            sim.read_direction = (value & 1U) != 0; sim.input_offset = 0;
            sim.phase = ADDRESS;
            sim.regs.STAR1 = sim.nack_address == sim.addresses ? I2C_STAR1_AF : I2C_STAR1_ADDR;
        } else {
            assert(sim.phase == TRANSMIT && sim.sent_length < sizeof(sim.sent));
            sim.sent[sim.sent_length++] = (uint8_t)value; ++sim.tx_count;
            sim.regs.STAR1 = sim.nack_data == sim.tx_count ? I2C_STAR1_AF : I2C_STAR1_TXE | I2C_STAR1_BTF;
            if (sim.tx_count == 1) sim.error = sim.data_error;
        }
        return;
    }
    if (offset == FIELD(CTLR2)) sim.regs.CTLR2 = value;
    else if (offset == FIELD(CKCFGR)) sim.regs.CKCFGR = value;
    else if (offset == FIELD(RTR)) sim.regs.RTR = value;
    else if (offset == FIELD(OADDR1)) sim.regs.OADDR1 = value;
    else if (offset == FIELD(OADDR2)) sim.regs.OADDR2 = value;
    else assert(false);
}
uint32_t wacca_i2c_sim_irq_save(void)
{
    assert(sim.critical_depth == 0); ++sim.critical_depth;
    const uint32_t saved = sim.irq; sim.irq = 0; return saved;
}
void wacca_i2c_sim_irq_restore(uint32_t saved)
{ assert(sim.critical_depth == 1); --sim.critical_depth; sim.irq = saved; }
uint32_t wacca_now_us(void) { assert(sim.critical_depth == 0); return sim.time++; }
void wacca_delay_us(uint32_t us) { sim.time += us; }
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins) { assert(port == GPIOB); port->OUTDR |= pins; }
void GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *pins)
{
    assert(port == GPIOB && pins->GPIO_Pin == (GPIO_Pin_10 | GPIO_Pin_11));
    assert(pins->GPIO_Mode == GPIO_Mode_Out_OD || pins->GPIO_Mode == GPIO_Mode_AF_OD);
    sim.alternate_pins = pins->GPIO_Mode == GPIO_Mode_AF_OD;
}
void RCC_APB1PeriphClockCmd(uint32_t peripheral, int enabled)
{ assert(peripheral == RCC_APB1Periph_I2C2 && enabled == ENABLE); sim.clock_enabled = true; }
void RCC_APB1PeriphResetCmd(uint32_t peripheral, int enabled)
{
    assert(peripheral == RCC_APB1Periph_I2C2);
    if (enabled) { ++sim.resets; memset(&sim.regs, 0, sizeof(sim.regs)); sim.phase = IDLE; }
}
void RCC_GetClocksFreq(RCC_ClocksTypeDef *clocks) { clocks->PCLK1_Frequency = sim.pclk; }
static void fresh(void)
{
    memset(&sim, 0, sizeof(sim)); sim.pclk = 36000000; sim.irq = 0x88;
    GPIOB->INDR = GPIO_Pin_10 | GPIO_Pin_11; GPIOB->OUTDR = 0;
    assert(wacca_i2c_init());
    assert(sim.clock_enabled && sim.alternate_pins);
    assert(sim.regs.CTLR2 == 36 && sim.regs.CKCFGR == 180 && sim.regs.RTR == 37);
}
static void check_reads(void)
{
    const uint8_t reg = 0xFA;
    const size_t lengths[] = {1, 2, 3, 4, 17, 256};
    for (size_t test = 0; test < sizeof(lengths) / sizeof(lengths[0]); ++test) {
        fresh(); sim.input_length = lengths[test];
        for (size_t i = 0; i < sim.input_length; ++i) sim.input[i] = (uint8_t)(i ^ 0xA5U);
        uint8_t received[258]; memset(received, 0xCC, sizeof(received));
        assert(wacca_i2c_transfer(0x2C, &reg, 1, received + 1, sim.input_length));
        assert(received[0] == 0xCC && received[sim.input_length + 1] == 0xCC);
        assert(memcmp(received + 1, sim.input, sim.input_length) == 0);
        assert(sim.starts == 2 && sim.stops == 1 && sim.wire_addresses[0] == 0x58 && sim.wire_addresses[1] == 0x59);
        assert(sim.sent_length == 1 && sim.sent[0] == reg && sim.input_offset == sim.input_length);
        assert(sim.irq == 0x88 && sim.critical_depth == 0);
        assert(sim.regs.CTLR1 == (I2C_CTLR1_PE | I2C_CTLR1_ACK));
    }
}
static void set_reset(void *ctx, bool asserted) { (void)ctx; sim.hub_reset = asserted; }
static bool prepare(void *ctx) { (void)ctx; assert(sim.hub_reset); return wacca_i2c_init(); }
static void delay(void *ctx, uint32_t us) { (void)ctx; wacca_delay_us(us); }
static bool write_hub(void *ctx, uint8_t address, const uint8_t *tx, size_t n)
{ (void)ctx; assert(!sim.hub_reset); return wacca_i2c_transfer(address, tx, n, NULL, 0); }
static bool read_hub(void *ctx, uint8_t address, const uint8_t *tx, size_t n, uint8_t *rx, size_t rx_n)
{ (void)ctx; return wacca_i2c_transfer(address, tx, n, rx, rx_n); }
int main(void)
{
    const uint8_t tx[] = {0xFA, 1, 6}; uint8_t rx[2];
    fresh(); assert(wacca_i2c_transfer(0x2C, tx, sizeof(tx), NULL, 0));
    assert(sim.starts == 1 && sim.stops == 1 && sim.wire_addresses[0] == 0x58);
    assert(sim.sent_length == sizeof(tx) && memcmp(sim.sent, tx, sizeof(tx)) == 0);
    check_reads();
    for (unsigned address = 1; address <= 2; ++address) {
        fresh(); sim.nack_address = address; sim.input_length = 2;
        assert(!wacca_i2c_transfer(0x2C, tx, 1, rx, 2));
        assert(sim.regs.CTLR1 == 0 && sim.stops == 1);
        sim.nack_address = 0;
        assert(wacca_i2c_transfer(0x2C, tx, 1, NULL, 0));
    }
    fresh(); sim.nack_data = 3;
    assert(!wacca_i2c_transfer(0x2C, tx, sizeof(tx), NULL, 0) && sim.regs.CTLR1 == 0);
    const uint16_t stalled[] = {I2C_STAR1_SB, I2C_STAR1_ADDR, I2C_STAR1_TXE, I2C_STAR1_BTF, I2C_STAR1_RXNE};
    for (size_t i = 0; i < sizeof(stalled) / sizeof(stalled[0]); ++i) {
        fresh(); sim.stall = stalled[i]; sim.input_length = 1; sim.time = UINT32_MAX - 100U;
        const uint32_t began = sim.time;
        assert(!wacca_i2c_transfer(0x2C, tx, 1, rx, 1));
        assert((uint32_t)(sim.time - began) >= 20000 && (uint32_t)(sim.time - began) < 21100);
        assert(sim.regs.CTLR1 == 0 && sim.critical_depth == 0);
    }
    const uint16_t errors[] = {I2C_STAR1_BERR, I2C_STAR1_ARLO, I2C_STAR1_OVR, I2C_STAR1_TIMEOUT};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        fresh(); sim.data_error = errors[i];
        assert(!wacca_i2c_transfer(0x2C, tx, 1, NULL, 0));
        assert(sim.regs.CTLR1 == 0 && sim.stops == (errors[i] == I2C_STAR1_ARLO ? 0U : 1U));
    }
    fresh(); sim.stuck_stop = true;
    assert(!wacca_i2c_transfer(0x2C, tx, sizeof(tx), NULL, 0));
    assert(sim.time < 21200 && sim.regs.CTLR1 == 0);
    fresh(); sim.stuck_busy = true;
    assert(!wacca_i2c_transfer(0x2C, tx, 1, NULL, 0) && sim.starts == 0);
    fresh(); GPIOB->INDR = 0;
    assert(!wacca_i2c_init() && sim.regs.CTLR1 == 0);
    fresh(); sim.pclk = 72000000; assert(!wacca_i2c_init());
    fresh(); const unsigned starts = sim.starts;
    assert(!wacca_i2c_transfer(0x80, tx, 1, NULL, 0));
    assert(!wacca_i2c_transfer(0x2C, NULL, 1, NULL, 0));
    assert(!wacca_i2c_transfer(0x2C, tx, 257, NULL, 0));
    assert(!wacca_i2c_transfer(0x2C, tx, 1, NULL, 2));
    assert(sim.starts == starts);
    fresh(); sim.irq = 0; sim.input_length = 2;
    assert(wacca_i2c_transfer(0x2C, tx, 1, rx, 2) && sim.irq == 0);
    fresh(); sim.input_length = 2; sim.input[0] = 1; sim.input[1] = 6;
    uint8_t image[WACCA_HUB_CONFIG_SIZE];
    const struct wacca_hub_io io = {NULL, set_reset, prepare, delay, write_hub, read_hub};
    assert(wacca_usb2422_config(image, sizeof(image), 500, 100));
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_OK);
    assert(sim.starts == 11 && sim.stops == 10 && !sim.hub_reset);
    puts("Hardware I2C2: hub bootstrap, repeated START, 1/2/N-byte reads, NACK/error cleanup, clock limits and wraparound deadlines passed.");
    return 0;
}
