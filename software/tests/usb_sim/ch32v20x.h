#ifndef WACCA_SIM_CH32_H
#define WACCA_SIM_CH32_H
#include <stdint.h>
#define __IO volatile
#define GPIO_Pin_11 (1U<<11)
#define GPIO_Pin_12 (1U<<12)
#define GPIO_Speed_50MHz 3
#define GPIO_Mode_IN_FLOATING 4
#define RCC_APB1Periph_USB 1
#define ENABLE 1
#define EXTEN_USBD_PU_EN (1U<<7)
typedef struct { uint32_t GPIO_Pin, GPIO_Speed, GPIO_Mode; } GPIO_InitTypeDef;
struct sim_exten { uint32_t EXTEN_CTR; };
struct sim_exti { uint32_t INTFR, RTENR, FTENR, INTENR; };
extern struct sim_exten sim_exten;
extern struct sim_exti sim_exti;
#define EXTEN (&sim_exten)
#define EXTI (&sim_exti)
#define GPIOA ((void *)0)
enum { USB_LP_CAN1_RX0_IRQn=36, USBWakeUp_IRQn=58 };
void GPIO_Init(void *gpio,const GPIO_InitTypeDef *pins);
void RCC_APB1PeriphClockCmd(uint32_t peripheral,int enabled);
void NVIC_EnableIRQ(int irq);
void NVIC_DisableIRQ(int irq);
#endif
