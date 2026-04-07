/**
 * @file bmp280.c
 * @brief BMP280 forced-mode driver using a non-blocking state machine.
 *
 * Measurement is triggered once per period, completion is detected by
 * polling the status register, and raw ADC values are converted using
 * calibration coefficients read at initialization.
 */

#include "bmp280.h"
#include "main.h"
#include "uart_cmd.h"

#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

#define BMP280_MEAS_PERIOD_MS   1000U
#define BMP280_POLL_INTERVAL_MS    5U
#define BMP280_MEAS_TIMEOUT_MS   200U

static float BMP280_Compensate_T(int32_t adc_T);
static float BMP280_Compensate_P(int32_t adc_P);
static void  BMP280_Read_Calibration(void);

typedef struct
{
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} BMP280_Calib_t;

static BMP280_Calib_t g_calib;
static int32_t t_fine = 0;

static BMP280_State_t bmp_state = BMP_IDLE;

static uint32_t bmp_measure_start_tick = 0U;
static uint32_t last_bmp_read_time     = 0U;
static uint32_t last_poll_tick         = 0U;
static BMP280_ErrorStats_t bmp_err = {0};

static float   bmp_last_temp  = 0.0f;
static float   bmp_last_press = 0.0f;
static int32_t bmp_last_raw_T = 0;
static int32_t bmp_last_raw_P = 0;

static void BMP280_Read_Calibration(void)
{
    uint8_t data[24];

    // Calibration coefficients are stored in 0x88~0x9F (24 bytes).
    if (HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, 0x88, 1, data, 24, 100) != HAL_OK)
    {
        bmp_err.total++;
        bmp_err.calib_read_fail++;
        return;
    }

    g_calib.dig_T1 = (uint16_t)((data[1]  << 8) | data[0]);
    g_calib.dig_T2 = (int16_t) ((data[3]  << 8) | data[2]);
    g_calib.dig_T3 = (int16_t) ((data[5]  << 8) | data[4]);

    g_calib.dig_P1 = (uint16_t)((data[7]  << 8) | data[6]);
    g_calib.dig_P2 = (int16_t) ((data[9]  << 8) | data[8]);
    g_calib.dig_P3 = (int16_t) ((data[11] << 8) | data[10]);
    g_calib.dig_P4 = (int16_t) ((data[13] << 8) | data[12]);
    g_calib.dig_P5 = (int16_t) ((data[15] << 8) | data[14]);
    g_calib.dig_P6 = (int16_t) ((data[17] << 8) | data[16]);
    g_calib.dig_P7 = (int16_t) ((data[19] << 8) | data[18]);
    g_calib.dig_P8 = (int16_t) ((data[21] << 8) | data[20]);
    g_calib.dig_P9 = (int16_t) ((data[23] << 8) | data[22]);
}

void BMP280_Init(void)
{
    uint8_t id     = 0;
    uint8_t reset  = 0xB6;
    uint8_t config = 0x10;

    if (HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, 0xD0, 1, &id, 1, 100) != HAL_OK)
    {
    	bmp_err.total++;
    	bmp_err.id_read_fail++;
        UART_LOG("BMP280 ID read fail");
        return;
    }

    if (id != 0x58)
    {
    	bmp_err.total++;
    	bmp_err.wrong_id++;
        UART_LOG("BMP280 wrong ID: 0x%02X", id);
        return;
    }

    if (HAL_I2C_Mem_Write(&hi2c1, BMP280_ADDR, 0xE0, 1, &reset, 1, 100) != HAL_OK)
    {
    	bmp_err.total++;
    	bmp_err.reset_write_fail++;
        UART_LOG("BMP280 reset write fail");
        return;
    }

    // Startup-only delay after soft reset. The periodic measurement path remains non-blocking.
    HAL_Delay(5);

    if (HAL_I2C_Mem_Write(&hi2c1, BMP280_ADDR, 0xF5, 1, &config, 1, 100) != HAL_OK)
    {
    	bmp_err.total++;
    	bmp_err.config_write_fail++;
        UART_LOG("BMP280 config write fail");
        return;
    }

    // Calibration data is read once during startup.
    BMP280_Read_Calibration();
}

void BMP280_Task(void)
{
    uint8_t status = 0;
    uint8_t data[6];
    int32_t adc_P = 0;
    int32_t adc_T = 0;
    uint8_t ctrl_meas = 0;
    uint32_t now = HAL_GetTick();

    // Guards against invalid transition into MEASURING / DATA_READY.
    static uint8_t bmp_triggered = 0U;

    switch (bmp_state)
    {
    case BMP_IDLE:
    {
        // Trigger one forced measurement every 1 second.
        if ((now - last_bmp_read_time) >= BMP280_MEAS_PERIOD_MS)
        {
            // osrs_t x1 | osrs_p x1 | forced mode
            ctrl_meas = 0x25;

            if (HAL_I2C_Mem_Write(&hi2c1, BMP280_ADDR, 0xF4, 1, &ctrl_meas, 1, 10) == HAL_OK)
            {
                bmp_triggered = 1U;
                last_poll_tick = now;
                bmp_measure_start_tick = now;
                bmp_state = BMP_MEASURING;
            }
            else
            {
                bmp_err.total++;
                bmp_err.trigger_fail++;

                bmp_triggered = 0U;

                // Keep the 1-second schedule even on trigger failure to avoid retry spamming.
                last_bmp_read_time = now;
                bmp_measure_start_tick = 0U;
                bmp_state = BMP_IDLE;
            }
        }

        break;
    }

    case BMP_MEASURING:
    {
        // Reject invalid state entry if no trigger was issued.
        if (!bmp_triggered)
        {
            bmp_state = BMP_IDLE;
            break;
        }

        // Polling is throttled to reduce unnecessary I2C traffic.
        if ((now - last_poll_tick) < BMP280_POLL_INTERVAL_MS)
        {
            break;
        }
        last_poll_tick = now;

        if ((now - bmp_measure_start_tick) > BMP280_MEAS_TIMEOUT_MS)
        {
            bmp_err.total++;
            bmp_err.busy_timeout++;
            bmp_triggered = 0U;
            bmp_state = BMP_IDLE;
            last_bmp_read_time = now;
            bmp_measure_start_tick = 0U;
            break;
        }

        if (HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, 0xF3, 1, &status, 1, 10) == HAL_OK)
        {
            // Measurement is complete when status.bit3 (measuring) becomes 0.
            if ((status & (1U << 3)) == 0U)
            {
                bmp_state = BMP_DATA_READY;
            }
        }
        else
        {
            bmp_err.total++;
            bmp_err.status_read_fail++;
            bmp_state = BMP_IDLE;
            bmp_triggered = 0U;
            last_bmp_read_time = now;
            bmp_measure_start_tick = 0U;
        }

        break;
    }

    case BMP_DATA_READY:
    {
        if (!bmp_triggered)
        {
            bmp_state = BMP_IDLE;
            break;
        }

        if (HAL_I2C_Mem_Read(&hi2c1, BMP280_ADDR, 0xF7, 1, data, 6, 100) == HAL_OK)
        {
            adc_P = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
            adc_T = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

            bmp_last_raw_P = adc_P;
            bmp_last_raw_T = adc_T;

            bmp_last_temp  = BMP280_Compensate_T(adc_T);
            bmp_last_press = BMP280_Compensate_P(adc_P);

            g_temp_valid = 1U;
            g_press_valid = 1U;
        }
        else
        {
            bmp_err.total++;
            bmp_err.data_read_fail++;
        }

        last_bmp_read_time = now;
        bmp_state = BMP_IDLE;
        bmp_measure_start_tick = 0U;
        bmp_triggered = 0U;
        break;
    }

    default:
    {
        bmp_state = BMP_IDLE;
        break;
    }
    }
}

// Temperature compensation based on BMP280 datasheet formula.
static float BMP280_Compensate_T(int32_t adc_T)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)g_calib.dig_T1 << 1))) *
            ((int32_t)g_calib.dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - ((int32_t)g_calib.dig_T1)) *
             ((adc_T >> 4) - ((int32_t)g_calib.dig_T1))) >> 12) *
             ((int32_t)g_calib.dig_T3)) >> 14;

    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;

    return (float)T / 100.0f;
}

// Pressure compensation based on BMP280 datasheet formula.
static float BMP280_Compensate_P(int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)g_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)g_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)g_calib.dig_P4) << 35);

    var1 = ((var1 * var1 * (int64_t)g_calib.dig_P3) >> 8) +
           ((var1 * (int64_t)g_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)g_calib.dig_P1)) >> 33;

    if (var1 == 0)
    {
        return 0.0f; // Prevent division by zero.
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)g_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)g_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)g_calib.dig_P7) << 4);

    return (float)p / 256.0f / 100.0f; // Convert Pa to hPa.
}

void BMP280_Get_Data(float *temp, float *press)
{
    if (temp != NULL)
    {
        *temp = bmp_last_temp;
    }

    if (press != NULL)
    {
        *press = bmp_last_press;
    }
}

void BMP280_Get_RawAdc(int32_t *adc_T, int32_t *adc_P)
{
    if (adc_T != NULL)
    {
        *adc_T = bmp_last_raw_T;
    }

    if (adc_P != NULL)
    {
        *adc_P = bmp_last_raw_P;
    }
}

uint32_t BMP280_Get_ErrorCount(void)
{
    return bmp_err.total;
}

const BMP280_ErrorStats_t *BMP280_Get_ErrorStats(void)
{
	return &bmp_err;
}
