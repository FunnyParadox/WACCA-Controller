#if defined(WACCA_I2C_SIMULATION)
#include "i2c_sim.h"
#define READ(bus, field) wacca_i2c_sim_read((unsigned)((bus) - buses), offsetof(I2C_TypeDef, field))
#define WRITE(bus, field, value) wacca_i2c_sim_write((unsigned)((bus) - buses), offsetof(I2C_TypeDef, field), (uint16_t)(value))
#else
#include "ch32v20x.h"
#define READ(bus, field) ((bus)->regs->field)
#define WRITE(bus, field, value) ((bus)->regs->field = (uint16_t)(value))
#endif
#include "i2c.h"
#include "mcu.h"

enum { TIMEOUT_US = 20000, STOP_TIMEOUT_US = 1000 };
#define ERROR_FLAGS (I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_AF | I2C_STAR1_OVR | I2C_STAR1_PECERR | I2C_STAR1_TIMEOUT)
enum phase { CONFIG_LINES, WAIT_IDLE, START_WRITE, ADDRESS_WRITE, TRANSMIT, TRANSMIT_FINISH, START_READ, ADDRESS_READ, RECEIVE_ONE, RECEIVE_MORE, RECEIVE_LAST_TWO, WAIT_STOP, ABORT_STOP };
struct bus_state
{
	I2C_TypeDef *regs;
	uint32_t peripheral, hz, began, stop_began;
	uint16_t pins, mhz, divider;
	bool initialized, config_only;
	enum phase phase;
	enum wacca_i2c_status status;
	uint8_t address;
	const uint8_t *tx;
	uint8_t *rx;
	size_t tx_length, rx_length, tx_offset, rx_offset;
};
static struct bus_state buses[WACCA_I2C_BUS_COUNT] =
{
	{ .regs = I2C1, .peripheral = RCC_APB1Periph_I2C1, .pins = GPIO_Pin_6 | GPIO_Pin_7, .hz = WACCA_I2C_STANDARD_HZ },
	{ .regs = I2C2, .peripheral = RCC_APB1Periph_I2C2, .pins = GPIO_Pin_10 | GPIO_Pin_11, .hz = WACCA_I2C_STANDARD_HZ }
};
static bool valid_bus(enum wacca_i2c_bus bus) { return ((unsigned)(bus) < WACCA_I2C_BUS_COUNT); }
static bool valid_speed(uint32_t hz) { return (hz == WACCA_I2C_STANDARD_HZ || hz == WACCA_I2C_FAST_HZ); }
static uint32_t irq_save(void)
{
	#if defined(WACCA_I2C_SIMULATION)
		return wacca_i2c_sim_irq_save();
	#else
		uint32_t saved;
		// gintenr is accessible in the user mode selected by WCH startup
		__asm__ volatile ("csrrc %0, 0x800, %1\n\tfence.i" : "=r"(saved) : "r"(0x88U) : "memory");
		return (saved & 0x88U);
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
static void clear_address(struct bus_state *bus)
{
	// Reading STAR1 then STAR2 clears ADDR and releases hardware SCL stretching
	(void)READ(bus, STAR1);
	(void)READ(bus, STAR2);
}
static void finish_error(struct bus_state *bus)
{
	WRITE(bus, CTLR1, 0);
	bus->initialized = false;
	bus->status = WACCA_I2C_ERROR;
}
static void abort_transfer(struct bus_state *bus, uint32_t now)
{
	const uint16_t flags = READ(bus, STAR1);
	// After arbitration loss another master owns the bus: never inject STOP
	if (!(flags & I2C_STAR1_ARLO) && (READ(bus, STAR2) & I2C_STAR2_MSL))
	{
		WRITE(bus, CTLR1, READ(bus, CTLR1) | I2C_CTLR1_STOP);
		bus->stop_began = now;
		bus->phase = ABORT_STOP;
	}
	else finish_error(bus);
}
static bool prepare_configuration(struct bus_state *bus)
{
	bus->initialized = false;
	RCC_APB1PeriphClockCmd(bus->peripheral, ENABLE);

	WRITE(bus, CTLR1, 0);
	// Explicitly select default I2C1 PB6/PB7; PB9 must remain the hub reset
	if (bus == &buses[WACCA_I2C_BUS1])
	{
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
		GPIO_PinRemapConfig(GPIO_Remap_I2C1, DISABLE);
	}
	GPIO_SetBits(GPIOB, bus->pins);

	GPIO_InitTypeDef pins = { bus->pins, GPIO_Speed_2MHz, GPIO_Mode_Out_OD };
	GPIO_Init(GPIOB, &pins);

	RCC_ClocksTypeDef clocks;
	RCC_GetClocksFreq(&clocks);

	const uint32_t pclk = clocks.PCLK1_Frequency;
	// RM 19.12.2: FREQ is the actual peripheral input MHz, 4..60.
	if (pclk < 4000000U || pclk > 60000000U || pclk % 1000000U != 0)
	{
		finish_error(bus);
		return (false);
	}
	const bool fast = bus->hz == WACCA_I2C_FAST_HZ;

	const uint32_t factor = fast ? 3U : 2U; // Fast-mode low:high duty is 2:1
	const uint32_t divisor = factor * bus->hz;
	const uint32_t divider = (pclk + divisor - 1U) / divisor;
	if (divider < (fast ? 1U : 4U) || divider > I2C_CKCFGR_CCR)
	{
		finish_error(bus);
		return (false);
	}
	bus->mhz = (uint16_t)(pclk / 1000000U);
	bus->divider = (uint16_t)divider;
	bus->phase = CONFIG_LINES;
	return (true);
}
static void finish_configuration(struct bus_state *bus)
{
	RCC_APB1PeriphResetCmd(bus->peripheral, ENABLE);
	RCC_APB1PeriphResetCmd(bus->peripheral, DISABLE);

	WRITE(bus, CTLR2, bus->mhz); // Polling, no peripheral interrupt/DMA requests
	const bool fast = (bus->hz == WACCA_I2C_FAST_HZ);
	WRITE(bus, CKCFGR, bus->divider | (fast ? I2C_CKCFGR_FS : 0U));
	// WCH timing formula: 1000 ns in Standard-mode, 300 ns in Fast-mode
	WRITE(bus, RTR, fast ? ((uint32_t)(bus->mhz) * 3U / 10U + 1U) : (bus->mhz + 1U));
	WRITE(bus, OADDR1, 0x4000); // WCH library's seven-bit own-address format
	WRITE(bus, OADDR2, 0);

	GPIO_InitTypeDef pins = { bus->pins, GPIO_Speed_2MHz, GPIO_Mode_AF_OD };
	GPIO_Init(GPIOB, &pins);

	WRITE(bus, CTLR1, I2C_CTLR1_PE | I2C_CTLR1_ACK);
	bus->initialized = true;
	bus->phase = WAIT_IDLE;
}
static void start(struct bus_state *bus, bool read)
{
	uint16_t control = I2C_CTLR1_PE | I2C_CTLR1_ACK;
	// POS must be selected before START for a two-byte receive
	if (read && bus->rx_length == 2) control |= I2C_CTLR1_POS;

	WRITE(bus, CTLR1, control | I2C_CTLR1_START);
	bus->phase = read ? START_READ : START_WRITE;
}
enum wacca_i2c_status wacca_i2c_poll(enum wacca_i2c_bus id)
{
	if (!valid_bus(id)) return WACCA_I2C_ERROR;
	struct bus_state *bus = &buses[id];
	if (bus->status != WACCA_I2C_PENDING) return (bus->status);

	const uint32_t now = wacca_now_us();
	if (bus->phase == ABORT_STOP)
	{
		if (!(READ(bus, CTLR1) & I2C_CTLR1_STOP) || (uint32_t)(now - bus->stop_began) >= STOP_TIMEOUT_US) finish_error(bus);
		return (bus->status);
	}
	if ((uint32_t)(now - bus->began) >= TIMEOUT_US)
	{
		abort_transfer(bus, now);
		return (bus->status);
	}
	if (bus->phase == CONFIG_LINES)
	{
		// Release without synthesizing clocks
		// A stuck cable on this bus never blocks progress on the other peripheral while the deadline expires
		if ((uint32_t)(now - bus->began) >= 5U && (GPIOB->INDR & bus->pins) == bus->pins) finish_configuration(bus);
		return (bus->status);
	}
	const uint16_t flags = READ(bus, STAR1);
	if (flags & ERROR_FLAGS)
	{
		abort_transfer(bus, now);
		return (bus->status);
	}
	switch (bus->phase)
	{
		case WAIT_IDLE: case WAIT_STOP:
		if (!(READ(bus, STAR2) & I2C_STAR2_BUSY) && !(READ(bus, CTLR1) & I2C_CTLR1_STOP))
		{
			if (bus->config_only || bus->phase == WAIT_STOP)
			{
				WRITE(bus, CTLR1, I2C_CTLR1_PE | I2C_CTLR1_ACK);
				bus->status = WACCA_I2C_COMPLETE;
			}
			else start(bus, !bus->tx_length);
		}
		break;

		case START_WRITE:
		case START_READ:
		if (flags & I2C_STAR1_SB)
		{
			const bool read = (bus->phase == START_READ);
			WRITE(bus, DATAR, ((uint16_t)bus->address << 1) | (read ? 1U : 0U));
			bus->phase = read ? ADDRESS_READ : ADDRESS_WRITE;
		}
		break;

		case ADDRESS_WRITE:
		if (flags & I2C_STAR1_ADDR)
		{
			clear_address(bus);
			bus->phase = TRANSMIT;
		}
		break;

		case TRANSMIT:
		if (flags & I2C_STAR1_TXE)
		{
			WRITE(bus, DATAR, bus->tx[bus->tx_offset++]);
			if (bus->tx_offset == bus->tx_length) bus->phase = TRANSMIT_FINISH;
		}
		break;

		case TRANSMIT_FINISH:
		// TXE alone only means DATAR is empty, BTF includes the final ACK
		if (flags & I2C_STAR1_BTF)
		{
			if (!bus->rx_length) 
			{
				WRITE(bus, CTLR1, READ(bus, CTLR1) | I2C_CTLR1_STOP);
				bus->phase = WAIT_STOP;
			}
			else start(bus, true);
		}
		break;

		case ADDRESS_READ:
		if (flags & I2C_STAR1_ADDR)
		{
			if (bus->rx_length == 1)
			{
				WRITE(bus, CTLR1, READ(bus, CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
				const uint32_t saved = irq_save();
				clear_address(bus);
				WRITE(bus, CTLR1, READ(bus, CTLR1) | I2C_CTLR1_STOP);
				irq_restore(saved);
				bus->phase = RECEIVE_ONE;
			}
			else if (bus->rx_length == 2)
			{
				const uint32_t saved = irq_save();
				clear_address(bus);
				WRITE(bus, CTLR1, READ(bus, CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
				irq_restore(saved);
				bus->phase = RECEIVE_LAST_TWO;
			}
			else
			{
				clear_address(bus);
				bus->phase = RECEIVE_MORE;
			}
		}
		break;

		case RECEIVE_ONE:
		if (flags & I2C_STAR1_RXNE)
		{
			bus->rx[0] = (uint8_t)READ(bus, DATAR);
			bus->phase = WAIT_STOP;
		}
		break;

		case RECEIVE_MORE:
		if (bus->rx_length - bus->rx_offset > 3)
		{
			if (flags & I2C_STAR1_RXNE)
				bus->rx[bus->rx_offset++] = (uint8_t)READ(bus, DATAR);
		}
		else if (flags & I2C_STAR1_BTF)
		{
			// BTF holds SCL low
			// Arrange the final NACK before releasing the next byte
			// USB interrupts cannot split this short window
			const uint32_t saved = irq_save();
			WRITE(bus, CTLR1, READ(bus, CTLR1) & (uint16_t)~I2C_CTLR1_ACK);
			bus->rx[bus->rx_offset++] = (uint8_t)READ(bus, DATAR);
			irq_restore(saved);
			bus->phase = RECEIVE_LAST_TWO;
		}
		break;

		case RECEIVE_LAST_TWO:
		if (flags & I2C_STAR1_BTF)
		{
			const uint32_t saved = irq_save();
			WRITE(bus, CTLR1, READ(bus, CTLR1) | I2C_CTLR1_STOP);
			bus->rx[bus->rx_offset++] = (uint8_t)READ(bus, DATAR);
			bus->rx[bus->rx_offset++] = (uint8_t)READ(bus, DATAR);
			irq_restore(saved);
			bus->phase = WAIT_STOP;
		}
		break;

		case CONFIG_LINES: case ABORT_STOP: break; // Handled before reading peripheral event flags
	}
	return (bus->status);
}
static bool wait_complete(enum wacca_i2c_bus id)
{
	do { for (unsigned i = 0; i < WACCA_I2C_BUS_COUNT; ++i)
		(void)wacca_i2c_poll((enum wacca_i2c_bus)i);
	} while (buses[id].status == WACCA_I2C_PENDING);
	return buses[id].status == WACCA_I2C_COMPLETE;
}
bool wacca_i2c_init(enum wacca_i2c_bus id, uint32_t hz)
{
	if (!valid_bus(id) || !valid_speed(hz)) return (false);
	struct bus_state *bus = &buses[id];

	if (bus->status == WACCA_I2C_PENDING) return (false);
	bus->hz = hz;
	bus->config_only = true;
	bus->status = WACCA_I2C_PENDING;
	bus->began = wacca_now_us();

	if (!prepare_configuration(bus)) return (false);
	return wait_complete(id);
}
bool wacca_i2c_set_speed(enum wacca_i2c_bus id, uint32_t hz)
{
	if (!valid_bus(id) || !valid_speed(hz)) return (false);
	struct bus_state *bus = &buses[id];

	if (bus->status == WACCA_I2C_PENDING) return (false);
	if (bus->initialized)
	{
		const uint32_t began = wacca_now_us();
		while (1)
		{
			const uint16_t flags = READ(bus, STAR1);
			const uint16_t state = READ(bus, STAR2);
			const uint32_t now = wacca_now_us();

			if ((flags & ERROR_FLAGS) || (uint32_t)(now - began) >= TIMEOUT_US)
			{
				bus->status = WACCA_I2C_PENDING;
				abort_transfer(bus, now);
				(void)wait_complete(id);
				return (false);
			}
			if (!(state & I2C_STAR2_BUSY) && !(READ(bus, CTLR1) & I2C_CTLR1_STOP)) break;
			for (unsigned i = 0; i < WACCA_I2C_BUS_COUNT; ++i) if (i != (unsigned)(id)) (void)wacca_i2c_poll((enum wacca_i2c_bus)(i));
		}
	}
	return (wacca_i2c_init(id, hz));
}
bool wacca_i2c_begin(enum wacca_i2c_bus id, uint8_t address, const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length)
{
	if (!valid_bus(id) || address < 8 || address > 0x77 || (tx_length && tx == NULL) || (rx_length && rx == NULL) || tx_length > 256 || rx_length > 256 || (!tx_length && !rx_length)) return (false);
	struct bus_state *bus = &buses[id];

	if (bus->status == WACCA_I2C_PENDING) return (false);
	bus->address = address;
	bus->tx = tx; bus->tx_length = tx_length; bus->tx_offset = 0;
	bus->rx = rx; bus->rx_length = rx_length; bus->rx_offset = 0;
	bus->config_only = false;
	bus->began = wacca_now_us();
	bus->status = WACCA_I2C_PENDING;

	if (!bus->initialized) return (prepare_configuration(bus));
	bus->phase = WAIT_IDLE;
	return (true);
}
bool wacca_i2c_transfer(enum wacca_i2c_bus id, uint8_t address, const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length)
{
	return (wacca_i2c_begin(id, address, tx, tx_length, rx, rx_length) ? wait_complete(id) : false);
}
