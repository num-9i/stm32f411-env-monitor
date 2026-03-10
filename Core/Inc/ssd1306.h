/*
 * ssd1306.h
 *
 *  Created on: Feb 12, 2026
 *      Author: ggggh
 */

#ifndef INC_SSD1306_H_
#define INC_SSD1306_H_

#define OLED_ADDR (0x3C <<1)
#include "main.h"




void OLED_Init(void);
void OLED_SendCommand(uint8_t command);
void OLED_SetCursor(uint8_t row,uint8_t col);
void OLED_Printf(char*str);
void OLED_Clear(void);



#endif /* INC_SSD1306_H_ */
