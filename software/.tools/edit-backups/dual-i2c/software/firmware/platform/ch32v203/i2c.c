#if defined(WACCA_I2C_SIMULATION)
#include "i2c_sim.h"
#define READ(field) wacca_i2c_sim_read(offsetof(I2C_TypeDef, field))
#define WRITE(field, value) wacca_i2c_sim_write(offsetof(I2C_TypeDef, field), (uint16_t)(value))
#else
#include "ch32v20x.h"
#define READ(field) (I2C2->field)
#define WRITE(field, value) (I2C2->field = (uint16_t)(value))
#endif
#include "i2c.h"
#include "platform.h"

enum { TIMEOUT_US = 20000, STOP_TIMEOUT_US = 1000 };
#define BUS_PINS (GPIO_Pin_10 | GPIO_Pin_11)
#define ERROR_FLAGS (I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_AF | I2C_STAR1_OVR | I2C_STAR1_PECERR | I2C_STAR1_TIMEOUT)
static bool initialized;
static uint32_t bus_hz = WACCA_I2C_STANDARD_HZ;

static uint32_t irq_save(void)
{
#if defined(WACCA_I2C_SIMULATION)
    return wacca_i2c_sim_irq_save();
#else
    uint32_t saved;
    // gintenr is accessible in the user mode selected by WCH startup.
    __asm__ volatile ("csrrc %0, 0x800, %1\n\tfence.i" : "=r"(saved) : "r"(0x88U) : "memory");
    return saved & 0x88U;
#endif
}
static void irq_restore(uint32_t saved)
{
#if defined(WACCA_I2C_SIMULATION)
    wacca_i2c_sim_irq_restore(saved);
#else
    __asm__ volatile ("fence iorw, iorw\n\tcsrs 0x800, %0" : : "r"(saved) : "memory");
#endif
}
static bool expired(uint32_t began, uint32_t timeout)
{
    return (uint32_t)(wacca_now_us() - began) >= timeout;
}
static bool wait_status(uint16_t mask, uint32_t began)
{
    for (;;) {
        const uint16_t flags = READ(STAR1);
        if ((flags & ERROR_FLAGS) || expired(began, TIMEOUT_US)) return false;
        if ((flags & mask) == mask) return true;
    }
}
static bool wait_idle(uint32_t began, uint32_t timeout)
{
    for (;;) {
        const uint16_t flags = READ(STAR1);
        const uint16_t state = READ(STAR2);
        if ((flags & ERROR_FLAGS) || expired(began, timeout)) return false;
        if (!(state & I2C_STAR2_BUSY) && !(READ(CTLR1) & I2C_CTLR1_STOP)) return true;
    }
}
static void clear_address(void)
{
    // This read order clears ADDR and releases the hardware's SCL stretching.
    (void)READ(STAR1);
    (void)READ(STAR2);
}
static bool start_address(uint8_t wire_address, uint32_t began)
{
    WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_START);
    if (!wait_status(I2C_STAR1_SB, began)) return false;
    WRITE(DATAR, wire_address);
    return wait_status(I2C_STAR1_ADDR, began);
}
static bool abort_transfer(void)
{
    const uint16_t flags = READ(STAR1);
    // After arbitration loss another master owns the bus: never inject STOP.
    if (!(flags & I2C_STAR1_ARLO) && (READ(STAR2) & I2C_STAR2_MSL)) {
        WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_STOP);
        const uint32_t began = wacca_now_us();
        while ((READ(CTLR1) & I2C_CTLR1_STOP) && !expired(began, STOP_TIMEOUT_US)) { }
    }
    WRITE(CTLR1, 0); // Disable local driving even if STOP cannot complete.
    initialized = false;
    return false;
}
static bool configure(void)
{
    initialized = false;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    WRITE(CTLR1, 0);
    // Release the bus without generating clocks; no software transfer fallback.
    GPIO_SetBits(GPIOB, BUS_PINS);
    GPIO_InitTypeDef pins = { BUS_PINS, GPIO_Speed_2MHz, GPIO_Mode_Out_OD };
    GPIO_Init(GPIOB, &pins);
    const uint32_t began = wacca_now_us();
    wacca_delay_us(5);
    while ((GPIOB->INDR & BUS_PINS) != BUS_PINS) {
        if (expired(began, TIMEOUT_US)) return false;
    }
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    const uint32_t pclk = clocks.PCLK1_Frequency;
    // RM section 19.12.2: FREQ is the actual peripheral input MHz, 4..60.
    if (pclk < 4000000U || pclk > 60000000U || pclk % 1000000U != 0) return false;
    const uint32_t mhz = pclk / 1000000U;
    const bool fast = bus_hz == WACCA_I2C_FAST_HZ;
    const uint32_t factor = fast ? 3U : 2U; // Fast-mode low:high duty is 2:1.
    const uint32_t divisor = factor * bus_hz;
    const uint32_t divider = (pclk + divisor - 1U) / divisor;
    if (divider < (fast ? 1U : 4U) || divider > I2C_CKCFGR_CCR) return false;
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, DISABLE);
    WRITE(CTLR2, mhz); // Polling, no peripheral interrupt or DMA requests yet.
    WRITE(CKCFGR, divider | (fast ? I2C_CKCFGR_FS : 0U));
    // WCH timing formula: 1000 ns in Standard-mode, 300 ns in Fast-mode.
    WRITE(RTR, fast ? (mhz * 3U / 10U + 1U) : (mhz + 1U));
    WRITE(OADDR1, 0x4000); // WCH library's seven-bit own-address format.
    WRITE(OADDR2, 0);
    pins.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &pins);
    WRITE(CTLR1, I2C_CTLR1_PE | I2C_CTLR1_ACK);
    if (!wait_idle(began, TIMEOUT_US)) return abort_transfer();
    initialized = true;
    return true;
}
bool wacca_i2c_init(void)
{
    bus_hz = WACCA_I2C_STANDARD_HZ; // Hub configuration always starts at 100 kHz.
    return configure();
}

bool wacca_i2c_set_speed(uint32_t hz)
{
    if (hz != WACCA_I2C_STANDARD_HZ && hz != WACCA_I2C_FAST_HZ) return false;
    if (initialized && !wait_idle(wacca_now_us(), TIMEOUT_US)) return abort_transfer();
    bus_hz = hz;
    return configure();
}

static bool receive_bytes(uint8_t *rx, size_t length, uint32_t began)
{
    if (length == 1) {
        WRITE(CTLR1, READ(CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
        const uint32_t saved = irq_save();
        clear_address();
        WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_STOP);
        irq_restore(saved);
        if (!wait_status(I2C_STAR1_RXNE, began)) return false;
        rx[0] = (uint8_t)READ(DATAR);
        return true;
    }
    if (length == 2) {
        // POS was set before START. Clear ACK immediately after clearing ADDR.
        const uint32_t saved = irq_save();
        clear_address();
        WRITE(CTLR1, READ(CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
        irq_restore(saved);
        if (!wait_status(I2C_STAR1_BTF, began)) return false;
        const uint32_t final_saved = irq_save();
        WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_STOP);
        rx[0] = (uint8_t)READ(DATAR);
        rx[1] = (uint8_t)READ(DATAR);
        irq_restore(final_saved);
        return true;
    }
    clear_address();
    size_t offset = 0;
    while (length - offset > 3) {
        if (!wait_status(I2C_STAR1_RXNE, began)) return false;
        rx[offset++] = (uint8_t)READ(DATAR);
    }
    // With three bytes left, BTF holds SCL low. Arrange the final NACK before
    // releasing the next byte; USB interrupts cannot split these short windows.
    if (!wait_status(I2C_STAR1_BTF, began)) return false;
    const uint32_t saved = irq_save();
    WRITE(CTLR1, READ(CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
    rx[offset++] = (uint8_t)READ(DATAR);
    irq_restore(saved);
    if (!wait_status(I2C_STAR1_BTF, began)) return false;
    const uint32_t final_saved = irq_save();
    WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_STOP);
    rx[offset++] = (uint8_t)READ(DATAR);
    rx[offset] = (uint8_t)READ(DATAR);
    irq_restore(final_saved);
    return true;
}
bool wacca_i2c_transfer(uint8_t address, const uint8_t *tx, size_t tx_length,
                        uint8_t *rx, size_t rx_length)
{
    if (address < 8 || address > 0x77 || (tx_length && tx == NULL) ||
        tx_length > 256 || rx_length > 256 || (rx_length && rx == NULL) ||
        (tx_length == 0 && rx_length == 0)) return false;
    if (!initialized && !configure()) return false; // Preserve the selected bus speed.
    const uint32_t began = wacca_now_us();
    if (!wait_idle(began, TIMEOUT_US)) return abort_transfer();
    WRITE(CTLR1, I2C_CTLR1_PE | I2C_CTLR1_ACK);
    if (tx_length) {
        if (!start_address((uint8_t)(address << 1), began)) return abort_transfer();
        clear_address();
        for (size_t i = 0; i < tx_length; ++i) {
            if (!wait_status(I2C_STAR1_TXE, began)) return abort_transfer();
            WRITE(DATAR, tx[i]);
        }
        // TXE alone only means the data register is empty. BTF includes final ACK.
        if (!wait_status(I2C_STAR1_BTF, began)) return abort_transfer();
    }
    if (rx_length) {
        if (rx_length == 2) WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_POS);
        if (!start_address((uint8_t)((address << 1) | 1U), began)) return abort_transfer();
        if (!receive_bytes(rx, rx_length, began)) return abort_transfer();
    } else {
        WRITE(CTLR1, READ(CTLR1) | I2C_CTLR1_STOP);
    }
    if (!wait_idle(began, TIMEOUT_US)) return abort_transfer();
    WRITE(CTLR1, I2C_CTLR1_PE | I2C_CTLR1_ACK);
    return true;
}
