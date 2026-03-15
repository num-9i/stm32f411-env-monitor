/**
 * @file led_control.c
 * @brief LED mode handling for PWM-based normal / blink / breath behavior.
 *
 * The update handler is intended to be called from the TIM2 periodic callback.
 * LED mode can also be changed automatically based on the latest humidity value.
 */

#include "led_control.h"

#define HUMI_BLINK_THRESHOLD 60.0f


extern TIM_HandleTypeDef htim2;
extern LED_Config_t g_led;
extern const LED_Config_t default_led_config;



void LED_ResetToDefault(void)
{
	g_led=default_led_config;
}






static void LED_Handle_Blink(void)
{
    static uint32_t count = 0;
    static int led_on = 1;

    if (++count >= g_led.blink_interval)
    {
        count = 0;
        led_on = !led_on;

        if (led_on)
        {
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, g_led.bright_pulse);
        }
        else
        {
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        }
    }
}

static void LED_Handle_Breath(void)
{
    static int breath_val = 0;
    static int direction = 1;
    static int slow_down = 0;

    // Slow the apparent breathing rate by updating PWM every few timer ticks.
    if (++slow_down >= 3)
    {
        slow_down = 0;

        if (direction == 1)
        {
            breath_val += g_led.breath_speed;
            if (breath_val >= 999)
            {
                direction = 0;
            }
        }
        else
        {
            breath_val -= g_led.breath_speed;
            if (breath_val <= 0)
            {
                direction = 1;
            }
        }

        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, breath_val);
    }
}

void LED_Auto_Control(float current_humi)
{
    // Example policy: high humidity switches the LED into blink mode.
    if (current_humi >HUMI_BLINK_THRESHOLD)
    {
        g_led.mode = LED_MODE_BLINK;
    }
    else
    {
        g_led.mode = LED_MODE_NORMAL;
    }
}

void LED_Update_Handler(void)
{
    if (g_led.enabled == 0)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        return;
    }

    if ((g_led.timer_signal == 1) && (g_led.timer_count > 0))
    {
        g_led.timer_count--;

        if (g_led.timer_count == 0)
        {
            g_led.enabled = 0;
            g_led.timer_signal = 0;
        }
    }

    switch (g_led.mode)
    {
    case LED_MODE_NORMAL:
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, g_led.bright_pulse);
        break;

    case LED_MODE_BLINK:
        LED_Handle_Blink();
        break;

    case LED_MODE_BREATH:
        LED_Handle_Breath();
        break;

    default:
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, g_led.bright_pulse);
        break;
    }
}
