#ifndef WACCA_CH32_MCU_H
#define WACCA_CH32_MCU_H
#include <stdbool.h>
#include <stdint.h>
extern uint32_t SystemCoreClock;

enum wacca_clock_state { WACCA_CLOCK_STARTING, WACCA_CLOCK_READY, WACCA_CLOCK_HSE_FAILED, WACCA_CLOCK_PLL_FAILED, WACCA_CLOCK_SWITCH_FAILED };
extern volatile enum wacca_clock_state wacca_clock_state;
void wacca_time_init(void);
void wacca_delay_us(uint32_t us);
uint32_t wacca_now_us(void);
#endif
