/**
 * @file uart_cmd.c
 * @brief UART CLI command handling and system logging utilities.
 *
 * Commands are registered in a function-pointer table and dispatched after
 * line-based input parsing. This module also provides debug / status commands
 * for sensor data, LED control, and runtime system inspection.
 */

#include "uart_cmd.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env_data.h"
#include "led_control.h"
#include "main.h"
#include "bmp280.h"
#include "aht20.h"

extern LED_Config_t g_led;
extern const LED_Config_t default_led_config;

extern UART_HandleTypeDef huart2;
extern UART_Handler g_uart2;
extern uint8_t rx_data;
extern TIM_HandleTypeDef htim2;

extern int auto_report_mode;

static const CommandEntry cmd_table[];



/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

void UART_LOG(const char *fmt, ...)
{
    printf("[%08ldms] [System Log] ", HAL_GetTick());

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\r\n");
}

/* -------------------------------------------------------------------------- */
/* Sensor / report commands                                                   */
/* -------------------------------------------------------------------------- */

static void cmd_REPORT(char *arg)
{
    if (arg == NULL)
    {
        UART_LOG("Usage: REPORT [ON/OFF]");
        return;
    }

    if (strcmp(arg, "ON") == 0)
    {
        auto_report_mode = 1;
        UART_LOG("Auto Reporting Enabled (10s interval)");
    }
    else if (strcmp(arg, "OFF") == 0)
    {
        auto_report_mode = 0;
        UART_LOG("Auto Reporting Disabled");
    }
    else
    {
        UART_LOG("Usage: REPORT [ON/OFF]");
    }
}

static void cmd_SENSSTAT(char *arg)
{
    (void)arg;

    UART_LOG("BMP280 I2C Errors: %lu", BMP280_Get_ErrorCount());
    UART_LOG("AHT20  I2C Errors: %lu", AHT20_Get_ErrorCount());
}

static void cmd_ENV(char *arg)
{
    (void)arg;

    Env_Fixed_t t = Env_Get_Temp_Fixed();
    Env_Fixed_t h = Env_Get_Humi_Fixed();
    int32_t p = Env_Get_Press_Fixed();

    UART_LOG("Environment Data (System Status):");
    UART_LOG(" - Temp: %s%d.%02d C", t.sign_str, t.whole, t.frac);
    UART_LOG(" - Humi: %d.%02d %%", h.whole, h.frac);
    UART_LOG(" - Pres: %ld hPa", (long)p);
    UART_LOG(">> Ready.");
}

static void cmd_DEBUG(char *arg)
{
    (void)arg;

    UART_LOG("--- Sensor RAW Data Debug ---");

    // Decimal output shows magnitude, while hexadecimal output helps verify bit layout.
    UART_LOG("[BMP280] adc_T=%ld (0x%05lX), adc_P=%ld (0x%05lX)",
             (long)g_raw_adc_T, (unsigned long)g_raw_adc_T,
             (long)g_raw_adc_P, (unsigned long)g_raw_adc_P);

    // BMP280 / AHT20 raw humidity are 20-bit values, so 5 hex digits are expected.
    UART_LOG("[AHT20] raw_humi=%lu (0x%05lX)",
             (unsigned long)g_raw_humi, (unsigned long)g_raw_humi);

    Env_Fixed_t t = Env_Get_Temp_Fixed();
    Env_Fixed_t h = Env_Get_Humi_Fixed();
    int32_t p = Env_Get_Press_Fixed();

    UART_LOG("[RESULT] %s%d.%02dC | %d.%02d%% | %ldhPa",
             t.sign_str, t.whole, t.frac,
             h.whole, h.frac,
             (long)p);

    UART_LOG("-----------------------------");
}

/* -------------------------------------------------------------------------- */
/* LED control commands                                                       */
/* -------------------------------------------------------------------------- */

static void cmd_TIMER(char *arg)
{
    if (arg == NULL)
    {
        UART_LOG("Usage: TIMER [seconds]");
        return;
    }

    g_led.timer_count = atoi(arg) * 1000;

    if (g_led.timer_count <= 0)
    {
        UART_LOG("Timer value must be at least 1 second");
        return;
    }

    g_led.timer_signal = 1;
    g_led.enabled = 1;

    UART_LOG("The system will turn off after %d seconds.", g_led.timer_count / 1000);
}

static void cmd_RESET(char *arg)
{
    (void)arg;

    g_led = default_led_config;
    UART_LOG("System Factory Reset Complete!");
}

static void cmd_BRIGHT(char *arg)
{
    int input_val = 0;

    if (arg == NULL)
    {
        UART_LOG("Usage: BRIGHT [0~100]");
        return;
    }

    input_val = atoi(arg);

    if (input_val < 0)
    {
        input_val = 0;
    }
    if (input_val > 100)
    {
        input_val = 100;
    }

    g_led.bright_pulse = input_val * 10;
    UART_LOG("Brightness memory updated to %d%%", input_val);
}

static void cmd_LedStatus(char *arg)
{
    char *mode_str = "UNKNOWN";
    (void)arg;

    if (g_led.mode == LED_MODE_NORMAL)
    {
        mode_str = "NORMAL";
    }
    else if (g_led.mode == LED_MODE_BLINK)
    {
        mode_str = "BLINKING";
    }
    else if (g_led.mode == LED_MODE_BREATH)
    {
        mode_str = "BREATHING";
    }

    UART_LOG("Current Mode: %s", mode_str);
    UART_LOG("Current PWM Value: %d", (int)htim2.Instance->CCR1);
    UART_LOG("Current Enable Value: %d", g_led.enabled);
}

static void cmd_BREATHSPEED(char *arg)
{
    if (arg == NULL)
    {
        UART_LOG("Usage: BREATHSPEED [value]");
        return;
    }

    g_led.breath_speed = atoi(arg);

    if (g_led.breath_speed < 1)
    {
        g_led.breath_speed = 1;
    }

    UART_LOG("Breath Speed Changed: %d", g_led.breath_speed);
}

static void cmd_BREATH(char *arg)
{
    (void)arg;
    g_led.mode = LED_MODE_BREATH;
    UART_LOG("Mode Changed: BREATHING");
}

static void cmd_BLINK(char *arg)
{
    (void)arg;
    g_led.mode = LED_MODE_BLINK;
    UART_LOG("Mode Changed: BLINKING");
}

static void cmd_LED(char *arg)
{
    if (arg == NULL)
    {
        UART_LOG("Usage: LED [ON/OFF]");
        return;
    }

    if (strcmp(arg, "ON") == 0)
    {
        g_led.enabled = 1;
        UART_LOG("LED System: Enabled");
    }
    else if (strcmp(arg, "OFF") == 0)
    {
        g_led.enabled = 0;
        UART_LOG("LED System: Disabled");
    }
    else
    {
        UART_LOG("Usage: LED [ON/OFF]");
    }
}

static void cmd_INTERVAL(char *arg)
{
    if (arg == NULL)
    {
        UART_LOG("Usage: INTERVAL [ms]");
        return;
    }

    g_led.blink_interval = atoi(arg);
    UART_LOG("Interval set to %dms", g_led.blink_interval);
}

/* -------------------------------------------------------------------------- */
/* General utility commands                                                   */
/* -------------------------------------------------------------------------- */

static void cmd_UPTIME(char *arg)
{
    uint32_t now = HAL_GetTick();
    uint32_t sec = now / 1000;
    uint32_t ms = now % 1000;
    (void)arg;

    UART_LOG("System Uptime: %lu.%03lu seconds", sec, ms);
}

static void cmd_HELLO(char *arg)
{
    (void)arg;
    UART_LOG("Welcome to My STM32 System!");
}





static void cmd_HELP(char *arg)
{
    (void)arg;

    UART_LOG("--- Available Commands ---");

    for (int i = 0; cmd_table[i].cmdStr != NULL; i++)
    {
        printf("- %s\r\n", cmd_table[i].cmdStr);
    }

    UART_LOG("--------------------------");
}

static void cmd_DUMP(char *arg)
{
    uint8_t *p = (uint8_t *)g_uart2.rx_buffer;
    (void)arg;

    UART_LOG("Memory Hex Dump Started...");

    for (int i = 0; i < 64; i++)
    {
        printf("[%02X]", p[i]);

        if ((i + 1) % 8 == 0)
        {
            printf("\r\n");
        }
    }
}

static const CommandEntry cmd_table[] =
{
    {"LED",         cmd_LED},
    {"INTERVAL",       cmd_INTERVAL},
    {"HELLO",       cmd_HELLO},
    {"DUMP",        cmd_DUMP},
    {"HELP",        cmd_HELP},
    {"UPTIME",      cmd_UPTIME},
    {"BRIGHT",      cmd_BRIGHT},
    {"STATUS",      cmd_LedStatus},
    {"BREATH",      cmd_BREATH},
    {"BLINK",       cmd_BLINK},
    {"BREATHSPEED", cmd_BREATHSPEED},
    {"RESET",       cmd_RESET},
    {"TIMER",       cmd_TIMER},
    {"ENV",         cmd_ENV},
    {"REPORT",      cmd_REPORT},
    {"DEBUG",       cmd_DEBUG},
    {"SENSSTAT",    cmd_SENSSTAT},
    {NULL,          NULL}
};

/* -------------------------------------------------------------------------- */
/* Command table                                                              */
/* -------------------------------------------------------------------------- */



/* -------------------------------------------------------------------------- */
/* Command processing                                                         */
/* -------------------------------------------------------------------------- */

void UART_ProcessCommand(UART_Handler *hUart)
{
    if (hUart->flags.bits.is_ready == 0)
    {
        return;
    }

    char *cmd = strtok(hUart->rx_buffer, " ");
    char *arg = strtok(NULL, " ");

    if (cmd != NULL)
    {
        int found = 0;

        for (int i = 0; cmd_table[i].cmdStr != NULL; i++)
        {
            if (strcmp(cmd, cmd_table[i].cmdStr) == 0)
            {
                cmd_table[i].func(arg);
                found = 1;
                break;
            }
        }

        if (!found)
        {
            UART_LOG("Unknown Command: %s", cmd);
        }
    }

    memset(hUart->rx_buffer, 0, sizeof(hUart->rx_buffer));
    hUart->rx_idx = 0;
    hUart->flags.all_flags = 0;
    hUart->state = SYS_IDLE;

    printf(">> Ready.\r\n");
}

/* -------------------------------------------------------------------------- */
/* UART RX interrupt callback                                                 */
/* -------------------------------------------------------------------------- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if ((rx_data == '\r') || (rx_data == '\n'))
        {
            if (g_uart2.rx_idx > 0)
            {
                g_uart2.rx_buffer[g_uart2.rx_idx] = '\0';
                g_uart2.flags.bits.is_ready = 1;
                g_uart2.state = SYS_COMMAND_PENDING;
            }
        }
        else if ((rx_data == 0x08) || (rx_data == 127))
        {
            if (g_uart2.rx_idx > 0)
            {
                g_uart2.rx_idx--;
                HAL_UART_Transmit(huart, (uint8_t *)"\b \b", 3, 10);
            }
        }
        else
        {
            if (g_uart2.rx_idx < 63)
            {
                g_uart2.rx_buffer[g_uart2.rx_idx++] = rx_data;
            }
        }

        HAL_UART_Receive_IT(huart, &rx_data, 1);
    }
}
