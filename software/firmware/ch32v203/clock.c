#include "ch32v20x.h"
#include "mcu.h"

uint32_t SystemCoreClock = 8000000;
volatile enum wacca_clock_state wacca_clock_state;

static bool wait_register(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
	// A bounded poll works before a calibrated clock is available
	for (uint32_t count = 0; count < 1000000; ++count)
		if ((*reg & mask) == value) return (true);
	return (false);
}

void SystemInit(void)
{
	// WCH D6 reset sequence; keep the hub detached during all clock setup
	{
		RCC->CTLR	|= 1;
		// Clears clock selection, divider, and clock-output configuration fields to a known initial state
		RCC->CFGR0	&= 0xF0FF0000;
		// Clears the external oscillator & clock security & clock divider (PLL) enable bits
		RCC->CTLR	&= 0xFEF6FFFF;
		// Clears external oscillator bypass
		RCC->CTLR	&= 0xFFFBFFFF;
		// Clears the PLL source, prescaler, multiplication, and USB divider configuration fields
		RCC->CFGR0	&= 0xFF00FFFF;
		// Clears selected clock-ready & clock-security flags, and leaves the associated clock-ready interrupt enables disabled
		RCC->INTR	= 0x009F0000;
	}
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);

	#if defined(WACCA_TOUCH_MCU)
		GPIO_ResetBits(GPIOB, GPIO_Pin_9);
		GPIO_InitTypeDef pin = { GPIO_Pin_9, GPIO_Speed_2MHz, GPIO_Mode_Out_OD };
		GPIO_Init(GPIOB, &pin);
	#endif

	// Enable external crystal oscillator
	RCC->CTLR |= RCC_HSEON;
	if (!wait_register(&RCC->CTLR, RCC_HSERDY, RCC_HSERDY)) { wacca_clock_state = WACCA_CLOCK_HSE_FAILED; return; }

	#if defined(WACCA_TOUCH_MCU)
		// I2C requires a 4..60 MHz input clock: APB1 = 144 / 4 = 36 MHz.
		RCC->CFGR0 |= RCC_PPRE1_DIV4;
	#else
		RCC->CFGR0 |= RCC_PPRE1_DIV2;
	#endif
	// Clear old PLL configuration fields before inserting the new settings
	RCC->CFGR0 &= ~(RCC_PLLSRC | RCC_PLLXTPRE | RCC_PLLMULL);
	// Choose the external oscillator as the PLL source, and multiply it by 18 to reach 144 MHz
	RCC->CFGR0 |= (RCC_PLLSRC_HSE | RCC_PLLMULL18);
	RCC->CTLR |= RCC_PLLON;
	if (!wait_register(&RCC->CTLR, RCC_PLLRDY, RCC_PLLRDY)) { wacca_clock_state = WACCA_CLOCK_PLL_FAILED; return; }

	// Clear the SW selection bits on register 0, then insert the selection for PLL
	RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
	if (!wait_register(&RCC->CFGR0, RCC_SWS, 0x08)) { wacca_clock_state = WACCA_CLOCK_SWITCH_FAILED; return; }

	SystemCoreClock = 144 * 1000000; // 144MHz
	RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
	wacca_clock_state = WACCA_CLOCK_READY;
}

// The bootstrap deliberately has only these two clock states
void SystemCoreClockUpdate(void) { SystemCoreClock = (((RCC->CFGR0 & RCC_SWS) == 0x08) ? 144 : 8) * 1000000; } // 144MHz or 8MHz
