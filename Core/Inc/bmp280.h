#ifndef INC_BMP280_H_
#define INC_BMP280_H_

#include "main.h"

#define BMP280_ADDR    (0x77 << 1)
#define BMP280_REG_ID  0xD0

typedef enum
{
    BMP_IDLE = 0,
    BMP_MEASURING,
    BMP_DATA_READY
} BMP280_State_t;

void BMP280_Init(void);
void BMP280_Task(void);

void BMP280_Get_Data(float *temp, float *press);
uint32_t BMP280_Get_ErrorCount(void);
void BMP280_Get_RawAdc(int32_t *adc_T, int32_t *adc_P);

#endif /* INC_BMP280_H_ */
