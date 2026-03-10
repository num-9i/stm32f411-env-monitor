#ifndef INC_ENV_DATA_H_
#define INC_ENV_DATA_H_

#include <stdint.h>

typedef struct
{
    int whole;
    int frac;
    char sign_str[2];
} Env_Fixed_t;

extern float g_temp;
extern float g_humi;
extern float g_press;

extern int32_t  g_raw_adc_T;
extern int32_t  g_raw_adc_P;
extern uint32_t g_raw_humi;

Env_Fixed_t Env_Get_Temp_Fixed(void);
Env_Fixed_t Env_Get_Humi_Fixed(void);
int32_t Env_Get_Press_Fixed(void);

#endif /* INC_ENV_DATA_H_ */
