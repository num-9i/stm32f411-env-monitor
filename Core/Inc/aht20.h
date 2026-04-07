#ifndef INC_AHT20_H_
#define INC_AHT20_H_

#include "main.h"

#define AHT20_ADDR (0x38 << 1)

typedef enum
{
    AHT_IDLE = 0,
    AHT_MEASURING
} AHT20_State_t;

typedef struct
{
	uint32_t total;
	uint32_t init_status_read_fail;
	uint32_t init_cmd_fail;
	uint32_t trigger_fail;
	uint32_t busy_timeout;
	uint32_t data_read_fail;
	uint32_t raw_zero;
}AHT20_ErrorStats_t;

void AHT20_Init(void);
void AHT20_Task(void);

void AHT20_Get_Data(float *temp, float *humi);
uint32_t AHT20_Get_ErrorCount(void);
uint32_t AHT20_Get_RawHumi(void);


const AHT20_ErrorStats_t *AHT20_Get_ErrorStats(void);

#endif /* INC_AHT20_H_ */
