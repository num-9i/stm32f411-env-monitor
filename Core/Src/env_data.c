/**
 * @file env_data.c
 * @brief Formatting and fixed-point conversion helpers for shared sensor data.
 *
 * This module keeps the presentation logic separate from sensor drivers.
 * It converts the latest floating-point values into display/CLI-friendly
 * fixed-point representations, including rounding, humidity clamping, and
 * correct handling of negative 0.xx temperature values.
 */

#include "env_data.h"

#include <math.h>
#include <stdlib.h>

float g_temp  = 0.0f;
float g_humi  = 0.0f;
float g_press = 0.0f;

int32_t  g_raw_adc_T = 0;
int32_t  g_raw_adc_P = 0;
uint32_t g_raw_humi  = 0;

/**
 * @brief Convert temperature to fixed-point text-friendly form.
 *
 * Rounds to two decimal places and preserves the sign for values such as -0.xx.
 */
Env_Fixed_t Env_Get_Temp_Fixed(void)
{
    Env_Fixed_t out = {0, 0, ""};

    int32_t t_val = (int32_t)roundf(g_temp * 100.0f);

    out.whole = (int)(t_val / 100);
    out.frac  = (t_val < 0) ? -(int)(t_val % 100) : (int)(t_val % 100);

    // Preserve the minus sign for values like -0.25.
    if ((t_val < 0) && (out.whole == 0))
    {
        out.sign_str[0] = '-';
        out.sign_str[1] = '\0';
    }
    else
    {
        out.sign_str[0] = '\0';
    }

    return out;
}

/**
 * @brief Convert humidity to fixed-point text-friendly form.
 *
 * Humidity is clamped to 0.00 ~ 100.00 as a small safety net against noise
 * or invalid intermediate values.
 */
Env_Fixed_t Env_Get_Humi_Fixed(void)
{
    Env_Fixed_t out = {0, 0, ""};

    int32_t h_val = (int32_t)roundf(g_humi * 100.0f);

    if (h_val < 0)
    {
        h_val = 0;
    }
    if (h_val > 10000)
    {
        h_val = 10000;
    }

    out.whole = (int)(h_val / 100);
    out.frac  = (int)(h_val % 100);

    return out;
}

/**
 * @brief Convert pressure to integer hPa for display and CLI output.
 */
int32_t Env_Get_Press_Fixed(void)
{
    return (int32_t)lroundf(g_press);
}
