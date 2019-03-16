#ifndef DWT_STM32_DELAY_H
#define DWT_STM32_DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f767xx.h"
#include "stm32f7xx_hal.h"

HAL_StatusTypeDef DWT_Delay_Init(void);
uint32_t DWT_us();

__STATIC_INLINE void DWT_Delay_us(volatile uint32_t microseconds) {
    uint32_t clk_cycle_start = DWT->CYCCNT;

    /* Go to number of cycles for system */
    microseconds *= (HAL_RCC_GetHCLKFreq() / 1000000L);

    /* Delay till end */
    while ((DWT->CYCCNT - clk_cycle_start) < microseconds);
}


#ifdef __cplusplus
}
#endif

#endif
