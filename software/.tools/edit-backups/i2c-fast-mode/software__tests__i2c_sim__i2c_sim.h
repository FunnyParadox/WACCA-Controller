#ifndef WACCA_I2C_SIM_H
#define WACCA_I2C_SIM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// Test-only register layout/masks matching the pinned WCH SDK.
typedef struct {
    uint16_t CTLR1, reserved0, CTLR2, reserved1, OADDR1, reserved2;
    uint16_t OADDR2, reserved3, DATAR, reserved4, STAR1, reserved5;
    uint16_t STAR2, reserved6, CKCFGR, reserved7, RTR, reserved8;
} I2C_TypeDef;
typedef struct { uint32_t INDR, OUTDR; } GPIO_TypeDef;
extern GPIO_TypeDef simulated_gpio_b;
#define GPIOB (&simulated_gpio_b)
#define GPIO_Pin_10 0x0400U
#define GPIO_Pin_11 0x0800U
#define GPIO_Speed_2MHz 2
#define GPIO_Mode_Out_OD 0x14
#define GPIO_Mode_AF_OD 0x1C
#define RCC_APB1Periph_I2C2 0x00400000U
#define ENABLE 1
#define DISABLE 0
typedef struct { uint16_t GPIO_Pin; int GPIO_Speed, GPIO_Mode; } GPIO_InitTypeDef;
typedef struct { uint32_t PCLK1_Frequency; } RCC_ClocksTypeDef;
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins);
void GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *pins);
void RCC_APB1PeriphClockCmd(uint32_t peripheral, int enabled);
void RCC_APB1PeriphResetCmd(uint32_t peripheral, int enabled);
void RCC_GetClocksFreq(RCC_ClocksTypeDef *clocks);
uint16_t wacca_i2c_sim_read(size_t offset);
void wacca_i2c_sim_write(size_t offset, uint16_t value);
uint32_t wacca_i2c_sim_irq_save(void);
void wacca_i2c_sim_irq_restore(uint32_t saved);
#define I2C_CTLR1_PE                            ((uint16_t)0x0001) /* Peripheral Enable */
#define I2C_CTLR1_START                         ((uint16_t)0x0100) /* Start Generation */
#define I2C_CTLR1_STOP                          ((uint16_t)0x0200) /* Stop Generation */
#define I2C_CTLR1_ACK                           ((uint16_t)0x0400) /* Acknowledge Enable */
#define I2C_CTLR1_POS                           ((uint16_t)0x0800) /* Acknowledge/PEC Position (for data reception) */
#define I2C_STAR1_SB                            ((uint16_t)0x0001) /* Start Bit (Master mode) */
#define I2C_STAR1_ADDR                          ((uint16_t)0x0002) /* Address sent (master mode)/matched (slave mode) */
#define I2C_STAR1_BTF                           ((uint16_t)0x0004) /* Byte Transfer Finished */
#define I2C_STAR1_RXNE                          ((uint16_t)0x0040) /* Data Register not Empty (receivers) */
#define I2C_STAR1_TXE                           ((uint16_t)0x0080) /* Data Register Empty (transmitters) */
#define I2C_STAR1_BERR                          ((uint16_t)0x0100) /* Bus Error */
#define I2C_STAR1_ARLO                          ((uint16_t)0x0200) /* Arbitration Lost (master mode) */
#define I2C_STAR1_AF                            ((uint16_t)0x0400) /* Acknowledge Failure */
#define I2C_STAR1_OVR                           ((uint16_t)0x0800) /* Overrun/Underrun */
#define I2C_STAR1_PECERR                        ((uint16_t)0x1000) /* PEC Error in reception */
#define I2C_STAR1_TIMEOUT                       ((uint16_t)0x4000) /* Timeout or Tlow Error */
#define I2C_STAR2_MSL                           ((uint16_t)0x0001) /* Master/Slave */
#define I2C_STAR2_BUSY                          ((uint16_t)0x0002) /* Bus Busy */
#define I2C_CKCFGR_CCR                          ((uint16_t)0x0FFF) /* Clock Control Register in Fast/Standard mode (Master mode) */
#endif
