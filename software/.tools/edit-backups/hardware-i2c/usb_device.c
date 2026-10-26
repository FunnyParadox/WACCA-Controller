#if defined(WACCA_USB_SIMULATION)
#include "usb_sim.h"
#define WACCA_USB_IRQ
#else
#include "ch32v20x.h"
#define WACCA_USB_IRQ __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
#include "usb_regs.h" // Unmodified WCH register definitions; no vendor control parser
#include "usb_device.h"
#include "platform.h"
#include <string.h>

enum { EP0_RX = 0x40, EP0_TX = 0x80, EP1_TX = 0xC0, EP2_RX = 0x100, EP3_TX = 0x140, RX_CAPACITY = 512, PACKET = 64 };
enum ep0_state { CTRL_IDLE, CTRL_IN, CTRL_STATUS_OUT, CTRL_OUT, CTRL_STATUS_IN, CTRL_STALL };
static struct wacca_usb_control control;
static struct wacca_usb_stats stats;
static enum ep0_state ep0_state;
static const uint8_t *ep0_data;
static uint16_t ep0_remaining;
static bool ep0_zlp;
static volatile bool configured, suspended, tx_busy;
static bool tx_zlp;
static uint8_t rx_ring[RX_CAPACITY];
static uint16_t rx_head, rx_tail;
static volatile uint32_t generation;

static void usb_lock(void)
{
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    __asm__ volatile ("" ::: "memory");
}
static void usb_unlock(void)
{
    #if !defined(WACCA_USB_SIMULATION)
        __asm__ volatile ("fence iorw, iorw" ::: "memory");
    #else
        __asm__ volatile ("" ::: "memory");
    #endif
    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

static volatile uint16_t *pma(uint16_t address)
{
    #if defined(WACCA_USB_SIMULATION)
        return (volatile uint16_t *)(sim_pma + address);
    #else
        return (volatile uint16_t *)(PMAAddr + ((uint32_t)(address) << 1));
    #endif
}
static volatile uint32_t *table(unsigned ep, unsigned field)
{
    #if defined(WACCA_USB_SIMULATION)
        return sim_pma+(ep*8U+field)/2U;
    #else
        return (volatile uint32_t *)(PMAAddr + ((uint32_t)(ep * 8U + field) << 1));
    #endif
}
static uint16_t rx_count(unsigned ep) { return (uint16_t)(*table(ep, 6) & 0x3FFU); }
static void rx_capacity(unsigned ep) { *table(ep, 6) = 0x8400; } // 2 x 32 byte
static void tx_count(unsigned ep, size_t count) { *table(ep, 2) = (uint32_t)(count); }
static void status(unsigned ep, uint16_t mask, uint16_t value)
{
    // STAT bits toggle on writes; CTR bits clear on zero
    // Re-read after each attempt because hardware may complete the other direction concurrently
    // Bound retries instead of the vendor's unbounded register busy-wait
    for (unsigned attempt = 0; attempt < 40; ++attempt)
    {
        const uint16_t reg = (uint16_t)_GetENDPOINT(ep);
        if ((reg & mask) == value) return;
        _SetENDPOINT(ep, (reg & EPREG_MASK) | ((reg & mask)^value) | EP_CTR_RX | EP_CTR_TX);
    }
    stats.errors++;
}
static void tx_status(unsigned ep, uint16_t value) { status(ep, EPTX_STAT, value); }
static void rx_status(unsigned ep, uint16_t value) { status(ep, EPRX_STAT, value); }
static void clear_toggle(unsigned ep, bool receive)
{
    const uint16_t bit = receive ? EP_DTOG_RX : EP_DTOG_TX;
    const uint16_t reg = (uint16_t)_GetENDPOINT(ep);
    if (reg & bit) _SetENDPOINT(ep, (reg & EPREG_MASK) | bit | EP_CTR_RX | EP_CTR_TX);
}
static void rearm_out(void)
{
    if (configured && !(control.halt_mask & 2U) && (uint16_t)(rx_head - rx_tail) <= RX_CAPACITY-PACKET)
    {
        rx_capacity(2);
        rx_status(2, EP_RX_VALID);
    }
}
static void data_reset(void)
{
    rx_head = 0;
    rx_tail = 0;
    tx_busy = false;
    tx_zlp = false;
    for (unsigned ep = 1; ep < 4; ++ep)
    {
        _SetENDPOINT(ep, (ep == 1 ? EP_INTERRUPT : EP_BULK) | ep);
        clear_toggle(ep, true);
        clear_toggle(ep, false);
        tx_status(ep, EP_TX_DIS);
        rx_status(ep, EP_RX_DIS);
    }
    *table(1, 0) = EP1_TX;
    *table(2, 4) = EP2_RX;
    *table(3, 0) = EP3_TX;
    tx_status(1, EP_TX_NAK);
    tx_status(3, EP_TX_NAK);
    rearm_out();
    stats.generation = ++generation;
}
static void bus_reset(void)
{
    const enum wacca_role role = control.role;
    wacca_usb_control_init(&control, role);
    configured = false;
    suspended = false;

    _SetCNTR(_GetCNTR() & ~(CNTR_FSUSP | CNTR_LPMODE));
    ep0_state = CTRL_IDLE;
    ep0_remaining = 0;
    ep0_zlp = false;
    _SetBTABLE(0);

    for (unsigned ep = 0; ep < 8; ++ep) _SetENDPOINT(ep, 0);
    _SetENDPOINT(0, EP_CONTROL);

    *table(0, 0) = EP0_TX;
    *table(0, 4) = EP0_RX;
    rx_capacity(0);
    rx_status(0, EP_RX_VALID);
    tx_status(0, EP_TX_NAK);
    *DADDR = DADDR_EF;

    data_reset();
    stats.resets++;
}
static void stall(void)
{
    ep0_state = CTRL_STALL;
    control.pending = false;
    tx_status(0, EP_TX_STALL);
    rx_status(0, EP_RX_STALL);
}
static void ep0_next(void)
{
    const uint16_t n = ep0_remaining > PACKET ? PACKET : ep0_remaining;
    wacca_pma_write(pma(EP0_TX), ep0_data, n);
    if (n)
    {
        ep0_data += n;
        ep0_remaining = (uint16_t)(ep0_remaining - n);
    }
    else ep0_zlp = false;
    
    tx_count(0, n);
    tx_status(0, EP_TX_VALID);
    rx_capacity(0);
    rx_status(0, EP_RX_VALID);
}
static void setup(void)
{
    const uint8_t *data;
    uint16_t length;
    uint8_t raw[8];

    _ClearEP_CTR_TX(0);
    tx_status(0, EP_TX_NAK);
    rx_status(0, EP_RX_NAK);
    if (rx_count(0) != sizeof(raw)) { stall(); return; }

    wacca_pma_read(raw, pma(EP0_RX), sizeof(raw));
    switch (wacca_usb_setup(&control, raw, &data, &length))
    {
        case WACCA_CONTROL_IN:
        {
            ep0_state = CTRL_IN;
            ep0_data = data;
            ep0_remaining = length;
            ep0_zlp = (length < ((uint16_t)raw[6] | ((uint16_t)raw[7]<<8))) && (length % PACKET == 0);
            ep0_next();
        }
        break;

        case WACCA_CONTROL_OUT:
        {
            ep0_state = CTRL_OUT;
            rx_capacity(0);
            rx_status(0, EP_RX_VALID);
        }
        break;

        case WACCA_CONTROL_STATUS:
        {
            ep0_state = CTRL_STATUS_IN;
            tx_count(0, 0);
            tx_status(0, EP_TX_VALID);
            rx_capacity(0);
            rx_status(0, EP_RX_VALID);
        }
        break;

        default: stall(); break;
    }
}
static void ep0_out(void)
{
    const uint16_t count = rx_count(0);

    if (ep0_state == CTRL_STATUS_OUT && count == 0)
    {
        ep0_state = CTRL_IDLE;
        rx_capacity(0);
        rx_status(0, EP_RX_VALID);
        return;
    }
    // A host may terminate a control read early with a zero-length OUT
    if (ep0_state == CTRL_IN && count == 0)
    {
        ep0_state = CTRL_IDLE;
        tx_status(0, EP_TX_NAK);
        rx_capacity(0);
        rx_status(0, EP_RX_VALID);
        return;
    }
    if (ep0_state == CTRL_OUT && count == 7)
    {
        uint8_t coding[7];
        wacca_pma_read(coding, pma(EP0_RX), 7);
        if (wacca_usb_control_out(&control, coding, 7))
        {
            ep0_state = CTRL_STATUS_IN;
            tx_count(0, 0);
            tx_status(0, EP_TX_VALID);
            return;
        }
    }
    stall();
}
static void apply_control(void)
{
    const uint8_t request = control.pending_request;
    const uint8_t endpoint = control.pending_endpoint;
    wacca_usb_control_commit(&control);

    *DADDR = (DADDR_EF | control.address);
    if (request == 9)
    {
        configured = control.configuration != 0;
        data_reset();
    }
    if (request == 1 || request == 3)
    {
        const unsigned ep = (endpoint == 1) ? 1 : (endpoint == 2 ? 2 : 3);
        if (request == 1) clear_toggle(ep, ep == 2);
        if (ep == 2)
        {
            rx_status(ep, (request == 3) ? EP_RX_STALL : EP_RX_NAK);
            rearm_out();
        }
        else tx_status(ep,(request == 3) ? EP_TX_STALL : EP_TX_NAK);
        if (ep == 3)
        {
            tx_busy = false;
            tx_zlp = false;
        }
    }
    if (request == 0x22)
    {
        // CDC SERIAL_STATE notification: DCD/DSR asserted while opened
        const uint8_t notification[] = { 0xA1, 0x20, 0, 0, 0, 0, 2, 0, control.lines ? 3 : 0, 0 };
        if (!(control.halt_mask & 1U) && _GetEPTxStatus(1) == EP_TX_NAK)
        {
            wacca_pma_write(pma(EP1_TX), notification, sizeof(notification));
            tx_count(1, sizeof(notification));
            tx_status(1, EP_TX_VALID);
        }
    }
}
static void ep0_in(void)
{
    if (ep0_state == CTRL_IN)
    {
        if (ep0_remaining || ep0_zlp) ep0_next(); else
        {
            ep0_state=CTRL_STATUS_OUT;
            tx_status(0, EP_TX_NAK);
            rx_capacity(0);
            rx_status(0, EP_RX_VALID);
        }
    }
    else if (ep0_state == CTRL_STATUS_IN)
    {
        apply_control();
        ep0_state = CTRL_IDLE;
        tx_status(0, EP_TX_NAK);
        rx_capacity(0);
        rx_status(0, EP_RX_VALID);
    }
}
static void bulk_out(void)
{
    uint8_t packet[PACKET];
    const uint16_t n = rx_count(2);
    if (!configured || n > PACKET || (uint16_t)(rx_head-rx_tail) > RX_CAPACITY-n) { stats.errors++; return; }

    wacca_pma_read(packet, pma(EP2_RX), n);
    for (uint16_t i = 0; i < n; ++i)
        rx_ring[rx_head++ & (RX_CAPACITY - 1)] = packet[i];
    stats.received += n;
    rearm_out();
}
void USB_LP_CAN1_RX0_IRQHandler(void) WACCA_USB_IRQ;
void USBWakeUp_IRQHandler(void) WACCA_USB_IRQ;
void USBWakeUp_IRQHandler(void) { EXTI->INTFR = 1U << 18; }
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    uint16_t flags=(uint16_t)_GetISTR();
    if (flags & ISTR_RESET)
    {
        // A bus reset supersedes stale suspend/wake events from the old bus
        _SetISTR((uint16_t)~(ISTR_RESET | ISTR_SUSP | ISTR_WKUP));
        bus_reset();
        flags=(uint16_t)_GetISTR();
    }
    if (flags & ISTR_WKUP)
    {
        _SetCNTR(_GetCNTR() & ~(CNTR_FSUSP | CNTR_LPMODE));
        suspended=false;
        _SetISTR((uint16_t)(CLR_WKUP));
    }
    if (flags & ISTR_SUSP)
    {
        suspended=true;
        _SetCNTR(_GetCNTR() | CNTR_FSUSP | CNTR_LPMODE);
        _SetISTR((uint16_t)(CLR_SUSP));
    }
    if (flags & (ISTR_ERR | ISTR_DOVR))
    {
        stats.errors++;
        _SetISTR((uint16_t)~(ISTR_ERR | ISTR_DOVR));
    }
    // Bound work per entry so a sustained bulk stream cannot monopolize CPU
    for (unsigned work = 0; work < 8; ++work)
    {
        flags = (uint16_t)_GetISTR();
        if (!(flags & ISTR_CTR)) break;

        const unsigned ep = flags & ISTR_EP_ID;
        if (ep >= 4) { stats.errors++; break; }

        const uint16_t reg = (uint16_t)_GetENDPOINT(ep);
        if (reg & EP_CTR_RX)
        {
            _ClearEP_CTR_RX(ep);
            if (ep == 0) (reg & EP_SETUP) ? setup() : ep0_out(); else if (ep == 2) bulk_out();
        }
        // SETUP cancels a previous IN completion; do not replay that event
        if ((reg & EP_CTR_TX) && !(ep == 0 && (reg & EP_SETUP)))
        {
            _ClearEP_CTR_TX(ep);
            if (ep == 0) ep0_in();
            else if (ep == 3)
            {
                if (tx_zlp)
                {
                    tx_zlp = false;
                    tx_count(3, 0);
                    tx_status(3, EP_TX_VALID);
                }
                else tx_busy = false;
            }
        }
    }
}
void wacca_usb_init(enum wacca_role role)
{
    EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    GPIO_InitTypeDef pins = { GPIO_Pin_11 | GPIO_Pin_12, GPIO_Speed_50MHz, GPIO_Mode_IN_FLOATING };
    GPIO_Init(GPIOA, &pins);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);

    _SetCNTR(CNTR_FRES);
    wacca_delay_us(2);

    _SetCNTR(0);
    _SetISTR(0);
    wacca_usb_control_init(&control,role);
    bus_reset();

    EXTI->INTFR = 1U << 18;
    EXTI->RTENR |= 1U << 18;
    EXTI->FTENR |= 1U << 18;
    EXTI->INTENR |= 1U << 18;
    NVIC_EnableIRQ(USBWakeUp_IRQn); NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

    _SetCNTR(CNTR_CTRM | CNTR_RESETM | CNTR_WKUPM | CNTR_SUSPM | CNTR_ERRM | CNTR_DOVRM);
    wacca_delay_us(20000);
    EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
}
bool wacca_usb_ready(void) { return (configured && !suspended); }
uint32_t wacca_usb_generation(void) { return (generation); }
size_t wacca_usb_read(uint8_t *data, size_t capacity)
{
    if (!data || !capacity) return (0);
    usb_lock();

    size_t count= (uint16_t)(rx_head - rx_tail);
    if (count > capacity) count = capacity;
    // Bound each critical section to one full-speed packet
    if (count > PACKET) count = PACKET;
    for (size_t i=0; i<count; ++i) data[i] = rx_ring[rx_tail++ & (RX_CAPACITY-1)];
    
    rearm_out();
    usb_unlock();
    return (count);
}
bool wacca_usb_send(const uint8_t *data, size_t length)
{
    if (!data || !length || length > PACKET) return (false);
    usb_lock();

    const bool ready = configured && !suspended && !tx_busy && !(control.halt_mask & 4U);
    if (ready)
    {
        tx_busy = true;
        tx_zlp = length == PACKET;
        wacca_pma_write(pma(EP3_TX), data, length); tx_count(3,length); stats.transmitted += (uint32_t)length;
        tx_status(3, EP_TX_VALID);
    }
    usb_unlock();
    return (ready);
}
void wacca_usb_get_stats(struct wacca_usb_stats *out)
{
    usb_lock();
    *out = stats;
    usb_unlock();
}
