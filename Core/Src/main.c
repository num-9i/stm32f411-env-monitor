/**
 * @file main.c
 * @brief Application entry point and super-loop scheduler.
 *
 * The main loop periodically runs non-blocking sensor tasks, updates the latest
 * shared sensor values, drives humidity-based LED behavior, refreshes the OLED
 * every 10 seconds, and processes UART CLI commands.
 */

#include "main.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aht20.h"
#include "bmp280.h"
#include "env_data.h"
#include "led_control.h"
#include "ssd1306.h"
#include "uart_cmd.h"

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define OLED_REPORT_INTERVAL_MS 10000U

uint32_t report_tick = 0U;

uint8_t rx_data; // UART RX byte buffer for interrupt-driven input.

const LED_Config_t default_led_config =
{
    .enabled = 1,
    .mode = 0,
    .bright_pulse = 500,
    .blink_interval = 500,
    .breath_speed = 5,
    .timer_signal = 0,
    .timer_count = 1000
};

LED_Config_t g_led =
{
    .enabled = 1,
    .mode = 0,
    .bright_pulse = 500,
    .blink_interval = 500,
    .breath_speed = 5,
    .timer_signal = 0,
    .timer_count = 1000
};

UART_Handler g_uart2;   // RX buffer, index, and state flags for CLI input.
int auto_report_mode = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN 0 */

// Redirect printf() output to USART2 VCP.
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 0xFFFF);
    return len;
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_TIM2_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */
    memset(&g_uart2, 0, sizeof(UART_Handler));
    HAL_UART_Receive_IT(&huart2, &rx_data, 1);

    printf("\r\n[System] System Started! Type Command and Enter.\r\n");

    HAL_TIM_Base_Start_IT(&htim2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    report_tick = HAL_GetTick();

    UART_LOG("Scanning I2C bus...");

    // Scan the I2C bus once at startup to confirm connected devices.
    for (uint8_t i = 1; i < 128; i++)
    {
        HAL_StatusTypeDef result =
            HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1), 3, 5);

        // HAL expects the 8-bit address form, so the 7-bit device address is shifted left by 1.
        if (result == HAL_OK)
        {
            UART_LOG("Found device at: 0x%02X", i);
        }
    }

    UART_LOG("Scan Finished.");

    OLED_Init();
    OLED_Clear();
    OLED_SetCursor(0, 0);
    OLED_Printf("READY");

    // Initialize sensors after OLED setup so startup status is visible during bring-up.
    BMP280_Init();
    AHT20_Init();
    /* USER CODE END 2 */

    while (1)
    {
        uint32_t now = HAL_GetTick();

        // 1) Run sensor state machines every loop without blocking the main loop.
        BMP280_Task();
        AHT20_Task();

        // 2) Copy the latest driver outputs into shared application-level values.
        BMP280_Get_Data(&g_temp, &g_press);
        AHT20_Get_Data(NULL, &g_humi);

        BMP280_Get_RawAdc(&g_raw_adc_T, &g_raw_adc_P);
        g_raw_humi = AHT20_Get_RawHumi();

        // 3) Apply automatic LED behavior using the latest humidity value.
        LED_Auto_Control(g_humi);

        // 4) Refresh OLED and optional auto report every 10 seconds.
        if ((now - report_tick) >= OLED_REPORT_INTERVAL_MS)
        {
            char buf[32];

            report_tick = now;

            OLED_Clear();

            Env_Fixed_t t = Env_Get_Temp_Fixed();
            Env_Fixed_t h = Env_Get_Humi_Fixed();
            int32_t p = Env_Get_Press_Fixed();

            snprintf(buf, sizeof(buf), "TEMP: %s%d.%02d C", t.sign_str, t.whole, t.frac);
            OLED_SetCursor(0, 0);
            OLED_Printf(buf);

            snprintf(buf, sizeof(buf), "HUMI: %d.%02d %%", h.whole, h.frac);
            OLED_SetCursor(2, 0);
            OLED_Printf(buf);

            snprintf(buf, sizeof(buf), "PRES: %ld HPA", (long)p);
            OLED_SetCursor(4, 0);
            OLED_Printf(buf);

            UART_LOG("OLED Updated with Sensor Data!");

            if (auto_report_mode)
            {
                UART_LOG("[Auto Report] T:%s%d.%02dC, H:%d.%02d%%, P:%ldhPa",
                         t.sign_str, t.whole, t.frac,
                         h.whole, h.frac,
                         (long)p);
            }
        }

        // 5) Process UART CLI commands.
        UART_ProcessCommand(&g_uart2);
    }
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 4;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief TIM2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 83;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 999;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_TIM_MspPostInit(&htim2);
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        // 1 ms timer tick used for LED blink / breath state updates.
        LED_Update_Handler();
    }
}

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif /* USE_FULL_ASSERT */
