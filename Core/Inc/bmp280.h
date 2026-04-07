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

typedef struct
{
	uint32_t total;
	uint32_t id_read_fail;
	uint32_t wrong_id;
	uint32_t reset_write_fail;
	uint32_t config_write_fail;
	uint32_t calib_read_fail;
	uint32_t trigger_fail;
	uint32_t busy_timeout;
	uint32_t status_read_fail;
	uint32_t data_read_fail;

} BMP280_ErrorStats_t;

void BMP280_Init(void);
void BMP280_Task(void);

void BMP280_Get_Data(float *temp, float *press);
uint32_t BMP280_Get_ErrorCount(void);
void BMP280_Get_RawAdc(int32_t *adc_T, int32_t *adc_P);

const BMP280_ErrorStats_t *BMP280_Get_ErrorStats(void);


#endif /* INC_BMP280_H_ */
