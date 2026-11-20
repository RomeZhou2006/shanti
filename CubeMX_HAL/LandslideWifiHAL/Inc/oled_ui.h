#ifndef OLED_UI_H
#define OLED_UI_H

#include "stm32f4xx_hal.h"

void OLED_UI_Init(I2C_HandleTypeDef *i2c);
void OLED_UI_SetWifiState(const char *state);
void OLED_UI_SetBuzzer(uint8_t on);
void OLED_UI_ProcessButtons(void);

#endif
