//
// Created by Kenneth Yan on 2026/11/20.
//

#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
uint16_t OLED_GetAckFailCount(void);

#endif