#ifndef INC_LED_CONTROL_H_
#define INC_LED_CONTROL_H_

#include "main.h"

typedef enum
{
    LED_MODE_NORMAL = 0,
    LED_MODE_BLINK,
    LED_MODE_BREATH
} LED_mode_t;

void LED_Update_Handler(void);
void LED_Auto_Control(float humidity);

#endif /* INC_LED_CONTROL_H_ */
