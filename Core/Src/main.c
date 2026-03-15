/**
 * @file main.c
 * @brief Application entry point and super-loop scheduler.
 *
 * The main loop periodically runs non-blocking sensor tasks, updates the latest
 * shared sensor values, drives humidity-based LED behavior, refreshes the OLED,
 * and processes Modbus RTU requests over USART1.
 */

#include "main.h"

#include <stdio.h>
#include <string.h>

#include "aht20.h"
#include "bmp280.h"
#include "env_data.h"
#include "led_control.h"
#include "ssd1306.h"
#include "uart_cmd.h"
#include "modbus_rtu.h"

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
#define OLED_REPORT_INTERVAL_MS 10000U

uint32_t report_tick = 0U;

uint8_t rx_data; /* CLI RX byte buffer (unused in Modbus mode). */

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

UART_Handler g_uart2;
int auto_report_mode = 0;

#define UART_MODE_CLI     0
#define UART_MODE_MODBUS  1

#define APP_UART_MODE     UART_MODE_MODBUS

uint8_t modbus_rx_byte = 0U;
uint8_t modbus_rx_buf[MODBUS_MIN_REQUEST_SIZE];
uint8_t modbus_tx_buf[MODBUS_MAX_TX_SIZE];

volatile uint16_t modbus_rx_idx = 0U;
volatile uint8_t modbus_request_ready = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN 0 */

/* Disable printf() output to avoid contaminating the Modbus line. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

#if (APP_UART_MODE == UART_MODE_MODBUS)
    if (modbus_request_ready == 0U)
    {
        if (modbus_rx_idx < MODBUS_MIN_REQUEST_SIZE)
        {
            modbus_rx_buf[modbus_rx_idx++] = modbus_rx_byte;
        }

        if (modbus_rx_idx >= MODBUS_MIN_REQUEST_SIZE)
        {
            modbus_request_ready = 1U;
            modbus_rx_idx = 0U;
        }
    }

    HAL_UART_Receive_IT(huart, &modbus_rx_byte, 1);
#else
    HAL_UART_Receive_IT(huart, &rx_data, 1);
#endif
}

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM2_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();

    memset(&g_uart2, 0, sizeof(UART_Handler));

#if (APP_UART_MODE == UART_MODE_MODBUS)
    memset(modbus_rx_buf, 0, sizeof(modbus_rx_buf));
    memset(modbus_tx_buf, 0, sizeof(modbus_tx_buf));
    modbus_rx_idx = 0U;
    modbus_request_ready = 0U;

    HAL_UART_Receive_IT(&huart1, &modbus_rx_byte, 1);
#else
    HAL_UART_Receive_IT(&huart1, &rx_data, 1);
#endif

    HAL_TIM_Base_Start_IT(&htim2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    report_tick = HAL_GetTick();

#if (APP_UART_MODE == UART_MODE_CLI)
    UART_LOG("Scanning I2C bus...");
#endif

    for (uint8_t i = 1; i < 128; i++)
    {
        HAL_StatusTypeDef result =
            HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1), 3, 5);

#if (APP_UART_MODE == UART_MODE_CLI)
        if (result == HAL_OK)
        {
            UART_LOG("Found device at: 0x%02X", i);
        }
#else
        (void)result;
#endif
    }

#if (APP_UART_MODE == UART_MODE_CLI)
    UART_LOG("Scan Finished.");
#endif

    OLED_Init();
    OLED_Clear();
    OLED_SetCursor(0, 0);
    OLED_Printf("READY");

    BMP280_Init();
    AHT20_Init();

    while (1)
    {
        uint32_t now = HAL_GetTick();

        BMP280_Task();
        AHT20_Task();

        BMP280_Get_Data(&g_temp, &g_press);
        AHT20_Get_Data(NULL, &g_humi);

        BMP280_Get_RawAdc(&g_raw_adc_T, &g_raw_adc_P);
        g_raw_humi = AHT20_Get_RawHumi();

        LED_Auto_Control(g_humi);

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

#if (APP_UART_MODE == UART_MODE_CLI)
            UART_LOG("OLED Updated with Sensor Data!");

            if (auto_report_mode)
            {
                UART_LOG("[Auto Report] T:%s%d.%02dC, H:%d.%02d%%, P:%ldhPa",
                         t.sign_str, t.whole, t.frac,
                         h.whole, h.frac,
                         (long)p);
            }
#endif
        }


#if (APP_UART_MODE == UART_MODE_MODBUS)
        if (modbus_request_ready)
        {
            Modbus_Request_t request;
            Modbus_Status_t status;
            uint16_t tx_len = 0U;

            status = Modbus_ParseRequest(modbus_rx_buf,
                                         MODBUS_MIN_REQUEST_SIZE,
                                         &request);

            if (status == MODBUS_OK)
            {
                tx_len = Modbus_BuildReadHoldingResponse(&request,
                                                         modbus_tx_buf,
                                                         sizeof(modbus_tx_buf));
            }
            else if (status == MODBUS_ERR_FUNC)
            {
                tx_len = Modbus_BuildExceptionResponse(MODBUS_SLAVE_ADDR,
                                                       MODBUS_FUNC_READ_HOLDING_REGS,
                                                       MODBUS_EXCEPTION_ILLEGAL_FUNC,
                                                       modbus_tx_buf,
                                                       sizeof(modbus_tx_buf));
            }
            else if (status == MODBUS_ERR_ADDR)
            {
                tx_len = Modbus_BuildExceptionResponse(MODBUS_SLAVE_ADDR,
                                                       MODBUS_FUNC_READ_HOLDING_REGS,
                                                       MODBUS_EXCEPTION_ILLEGAL_ADDR,
                                                       modbus_tx_buf,
                                                       sizeof(modbus_tx_buf));
            }
            else if (status == MODBUS_ERR_VALUE)
            {
                tx_len = Modbus_BuildExceptionResponse(MODBUS_SLAVE_ADDR,
                                                       MODBUS_FUNC_READ_HOLDING_REGS,
                                                       MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                                       modbus_tx_buf,
                                                       sizeof(modbus_tx_buf));
            }
            else
            {
                tx_len = 0U;
            }

            if (tx_len > 0U)
            {
                HAL_UART_Transmit(&huart1, modbus_tx_buf, tx_len, 100);
            }

            memset(modbus_rx_buf, 0, sizeof(modbus_rx_buf));
            modbus_request_ready = 0U;
        }
#else
        UART_ProcessCommand(&g_uart2);
#endif
    }
}
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

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 9600;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

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

    /* Configure GPIO pins : PA9 (USART1_TX), PA10 (USART1_RX) */
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        LED_Update_Handler();
    }
}
/* USER CODE END 4 */

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
#endif
