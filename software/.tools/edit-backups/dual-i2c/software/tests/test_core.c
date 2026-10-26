#include "../firmware/common/board_pins.h"
#include "../firmware/main_touch/usb2422.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct mock {
    uint8_t regs[256];
    bool seen[255];
    bool reset, prepared, verified, prepare_fail, read_fail, bad_count, bad_swap;
    unsigned writes, reads, assertions, not_ready, fail_write, delay_calls;
};

static void reset(void *ctx, bool asserted)
{
    struct mock *m = ctx;
    m->reset = asserted;
    if (asserted) {
        ++m->assertions;
        memset(m->regs, 0, sizeof(m->regs));
        memset(m->seen, 0, sizeof(m->seen));
        m->verified = false;
    }
}

static bool prepare(void *ctx)
{
    struct mock *m = ctx;
    assert(m->reset);
    m->prepared = true;
    return !m->prepare_fail;
}

static void delay(void *ctx, uint32_t us)
{
    struct mock *m = ctx;
    assert(us == 2 || us == 1000);
    ++m->delay_calls;
}

static bool write_bus(void *ctx, uint8_t address, const uint8_t *tx, size_t n)
{
    struct mock *m = ctx;
    assert(m->prepared && !m->reset);
    assert(address == 0x2C);
    assert(n >= 3 && n <= 34 && (size_t)tx[1] + 2 == n);
    ++m->writes;
    if (m->not_ready != 0) {
        --m->not_ready;
        return false;
    }
    if (m->writes == m->fail_write) {
        return false;
    }
    if (tx[0] == 0xFF) {
        assert(n == 3 && tx[2] == 5 && m->verified);
        for (size_t i = 0; i < 255; ++i) {
            assert(m->seen[i]);
        }
    } else {
        assert((size_t)tx[0] + tx[1] <= 255);
        for (size_t i = 0; i < tx[1]; ++i) {
            const size_t reg = (size_t)tx[0] + i;
            assert(!m->seen[reg]);
            m->seen[reg] = true;
        }
    }
    memcpy(m->regs + tx[0], tx + 2, tx[1]);
    return true;
}

static bool read_bus(void *ctx, uint8_t address, const uint8_t *tx, size_t tx_n, uint8_t *rx, size_t rx_n)
{
    struct mock *m = ctx;
    assert(address == 0x2C && tx_n == 1 && tx[0] == 0xFA && rx_n == 2);
    assert(!m->reset && m->regs[0xFF] == 0);
    ++m->reads;
    for (size_t i = 0; i < 255; ++i) assert(m->seen[i]);
    if (m->read_fail) return (false);
    rx[0] = m->bad_count ? 0 : 1;
    rx[1] = m->bad_swap ? 0 : m->regs[0xFA];
    m->verified = rx[0] == 1 && (rx[1] & 7U) == 6;
    return (true);
}

static struct wacca_hub_io interface(struct mock *m)
{
    const struct wacca_hub_io io = {m, reset, prepare, delay, write_bus, read_bus};
    return (io);
}

static uint16_t pin_mask(const struct wacca_pin *pins, unsigned port)
{
    uint16_t mask = 0;
    for (size_t i = 0; i < WACCA_SECTORS; ++i)
    {
        assert(pins[i].bit < 16 && pins[i].package_pin >= 1 && pins[i].package_pin <= 48);
        if (pins[i].port == port)
        {
            const uint16_t bit = (uint16_t)(1U << pins[i].bit);
            assert((mask & bit) == 0);
            mask = (uint16_t)(mask | bit);
        }
    }
    return (mask);
}

static void check_pin_maps(void)
{
    assert(pin_mask(wacca_touch_irq, WACCA_GPIO_A) == WACCA_TOUCH_GPIOA_MASK);
    assert(pin_mask(wacca_touch_irq, WACCA_GPIO_B) == WACCA_TOUCH_GPIOB_MASK);
    assert(pin_mask(wacca_led_data, WACCA_GPIO_A) == WACCA_LED_GPIOA_MASK);
    assert(pin_mask(wacca_led_data, WACCA_GPIO_B) == WACCA_LED_GPIOB_MASK);
    assert((WACCA_LED_GPIOA_MASK & (uint16_t)(1U << wacca_brightness.bit)) == 0);
    assert((WACCA_LED_GPIOA_MASK & UINT16_C(0x7800)) == 0); // USB/debug
    assert(wacca_brightness.port == WACCA_GPIO_A && wacca_brightness.bit == 3);
    assert(wacca_brightness.package_pin == 13);
    assert(wacca_hub_reset.port == WACCA_GPIO_B && wacca_hub_reset.bit == 9);
}

static void check_config(uint8_t *image)
{
    uint8_t before[WACCA_HUB_CONFIG_SIZE];
    memset(image, 0xA5, WACCA_HUB_CONFIG_SIZE);
    memcpy(before, image, sizeof(before));
    assert(!wacca_usb2422_config(image, 254, 500, 100));
    assert(!wacca_usb2422_config(image, 255, 501, 100));
    assert(!wacca_usb2422_config(image, 255, 500, 101));
    assert(!wacca_usb2422_config(image, 255, 99, 100));
    assert(!wacca_usb2422_config(NULL, 255, 500, 100));
    assert(memcmp(before, image, sizeof(before)) == 0);
    assert(wacca_usb2422_config(image, 255, 500, 100));
    assert(image[0] == 0x24 && image[1] == 4);
    assert(image[2] == 0x22 && image[3] == 0x24);
    assert((image[6] & 0x80U) == 0 && (image[6] & 6U) == 4);
    assert((image[7] & 8U) != 0 && image[9] == 6);
    assert(image[0x0D] == 250 && image[0x0F] == 50);
    assert(image[0xFA] == 6 && image[0xFB] == 0);
    assert(image[0xFE] == 0);
}

int main(void)
{
    uint8_t image[WACCA_HUB_CONFIG_SIZE];
    struct mock m = {0};
    struct wacca_hub_io io = interface(&m);

    check_pin_maps();
    check_config(image);
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_OK);
    assert(!m.reset && m.regs[0xFF] == 5 && m.writes == 9 && m.reads == 1);
    assert(memcmp(image, m.regs, sizeof(image)) == 0);

    memset(&m, 0, sizeof(m)); m.not_ready = 2;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_OK);
    assert(m.writes == 11 && !m.reset);

    memset(&m, 0, sizeof(m)); m.not_ready = 100;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_WRITE_FAILED);
    assert(m.writes == WACCA_HUB_READY_ATTEMPTS && m.reset && m.regs[0xFF] == 0);

    for (unsigned failure = 2; failure <= 9; ++failure)
    {
        memset(&m, 0, sizeof(m)); m.fail_write = failure;
        assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_WRITE_FAILED);
        assert(m.reset && m.regs[0xFF] == 0 && m.assertions == 2);
    }
    memset(&m, 0, sizeof(m)); m.prepare_fail = true;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_BUS_INIT_FAILED);
    assert(m.reset && m.writes == 0);

    memset(&m, 0, sizeof(m)); m.read_fail = true;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_READ_FAILED);
    assert(m.reset && m.regs[0xFF] == 0);

    memset(&m, 0, sizeof(m)); m.bad_count = true;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_VERIFY_FAILED);
    assert(m.reset && m.regs[0xFF] == 0);

    memset(&m, 0, sizeof(m)); m.bad_swap = true;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_VERIFY_FAILED);
    assert(m.reset && m.regs[0xFF] == 0);

    memset(&m, 0, sizeof(m)); image[0xFA] = 7;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_BAD_PORT_SWAP);
    assert(m.assertions == 0 && m.writes == 0);
    assert(wacca_usb2422_start(NULL, image, sizeof(image)) == WACCA_HUB_BAD_ARGUMENT);

    io.write = NULL;
    assert(wacca_usb2422_start(&io, image, sizeof(image)) == WACCA_HUB_BAD_ARGUMENT);
    puts("Hub sequencing, SMBus framing, timeout/failure handling and pin checks passed.");
    return (0);
}
