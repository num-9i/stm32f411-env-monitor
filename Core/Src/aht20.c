/**
 * @file aht20.c
 * @brief AHT20 driver using a non-blocking state machine.
 *
 * Humidity measurement is triggered once per period, completion is detected
 * by polling the busy bit in the status byte, and the latest converted values
 * are cached for CLI / OLED / debug output.
 */

#include "aht20.h"
#include "uart_cmd.h"

#include <stdlib.h>
#include <string.h>

static AHT20_ErrorStats_t aht_err = {0};

extern I2C_HandleTypeDef hi2c1;

#define AHT20_MEAS_PERIOD_MS    1000U
#define AHT20_POLL_INTERVAL_MS     5U
#define AHT20_MEAS_TIMEOUT_MS    300U

static AHT20_State_t aht_state = AHT_IDLE;

static uint32_t last_aht_read_time     = 0U;
static uint32_t aht_measure_start_tick = 0U;
static uint32_t last_poll_tick         = 0U;

static float    aht_last_humi     = 0.0f;
static float    aht_last_temp     = 0.0f;
static uint32_t aht_last_raw_humi = 0U;




uint32_t AHT20_Get_ErrorCount(void)
{
    return aht_err.total;
}

const AHT20_ErrorStats_t *AHT20_Get_ErrorStats(void)
{
    return &aht_err;
}





void AHT20_Init(void)
{
    uint8_t status = 0U;
    static const uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};

    // Startup-only delay after power-on.
    HAL_Delay(40);

    if (HAL_I2C_Master_Receive(&hi2c1, AHT20_ADDR, &status, 1, 100) != HAL_OK)
    {
        aht_err.init_status_read_fail++;
        aht_err.total++;
        return;
    }

    // If calibration enable bit(bit3) is not set, send the initialization command.
    if ((status & 0x08U) == 0U)
    {
        if (HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR, (uint8_t *)init_cmd, 3, 100) != HAL_OK)
        {
            aht_err.init_cmd_fail++;
            aht_err.total++;
            return;
        }

        // Startup-only delay after init command.
        HAL_Delay(10);
    }
}

void AHT20_Task(void)
{
    uint8_t data[6];
    uint32_t now = HAL_GetTick();

    static const uint8_t trigger[3] = {0xAC, 0x33, 0x00};

    // Guards against invalid transition into MEASURING.
    static uint8_t aht_triggered = 0U;

    switch (aht_state)
    {
    case AHT_IDLE:
    {
        // Trigger one measurement every 1 second.
        if ((now - last_aht_read_time) >= AHT20_MEAS_PERIOD_MS)
        {
            if (HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR, (uint8_t *)trigger, 3, 10) == HAL_OK)
            {
                aht_measure_start_tick = now;
                last_poll_tick = now;
                aht_triggered = 1U;
                aht_state = AHT_MEASURING;
            }
            else
            {
                aht_err.trigger_fail++;
                aht_err.total++;
                aht_triggered = 0U;

                // Keep the 1-second schedule even on trigger failure to avoid retry spamming.
                last_aht_read_time = now;
                aht_state = AHT_IDLE;
            }
        }
        break;
    }

    case AHT_MEASURING:
    {
        if (!aht_triggered)
        {
            aht_state = AHT_IDLE;
            break;
        }

        if ((now - last_poll_tick) < AHT20_POLL_INTERVAL_MS)
        {
            break;
        }
        last_poll_tick = now;

        if ((now - aht_measure_start_tick) >= AHT20_MEAS_TIMEOUT_MS)
        {
            aht_err.total++;
            aht_err.busy_timeout++;

            aht_state = AHT_IDLE;
            aht_triggered = 0U;
            last_aht_read_time = now;
            aht_measure_start_tick = 0U;
            break;
        }

        if (HAL_I2C_Master_Receive(&hi2c1, AHT20_ADDR, data, 6, 50) != HAL_OK)
        {
            aht_err.total++;
            aht_err.data_read_fail++;

            aht_state = AHT_IDLE;
            aht_triggered = 0U;
            last_aht_read_time = now;
            aht_measure_start_tick = 0U;
            break;
        }

        /* data[0] = status byte, bit7 = busy */
        if ((data[0] & 0x80U) != 0U)
        {
            break;
        }

        uint32_t raw_humi =
            ((uint32_t)data[1] << 12) |
            ((uint32_t)data[2] << 4)  |
            (((uint32_t)data[3] >> 4) & 0x0FU);

        uint32_t raw_temp =
            (((uint32_t)data[3] & 0x0FU) << 16) |
            ((uint32_t)data[4] << 8) |
            (uint32_t)data[5];

        aht_last_temp = ((float)raw_temp * 200.0f / 1048576.0f) - 50.0f;

        if (raw_humi != 0U)
        {
            aht_last_raw_humi = raw_humi;
            aht_last_humi = (float)raw_humi * 100.0f / 1048576.0f;
            g_humi_valid = 1U;
        }
        else
        {
            aht_err.total++;
            aht_err.raw_zero++;
        }

        last_aht_read_time = now;
        aht_state = AHT_IDLE;
        aht_measure_start_tick = 0U;
        aht_triggered = 0U;
        break;
    }

    default:
        {
            aht_state = AHT_IDLE;
            aht_triggered = 0U;
            break;
        }
  }

}

void AHT20_Get_Data(float *temp, float *humi)
{
    if (temp != NULL)
    {
        *temp = aht_last_temp;
    }

    if (humi != NULL)
    {
        *humi = aht_last_humi;
    }
}

uint32_t AHT20_Get_RawHumi(void)
{
    return aht_last_raw_humi;
}


