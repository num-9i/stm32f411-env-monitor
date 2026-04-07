/* USER CODE BEGIN Header */

/**
 * @file main.c
 * @brief Application entry point and super-loop scheduler.
 *
 * The main loop periodically runs non-blocking sensor tasks, updates the latest
 * shared sensor values, drives humidity-based LED behavior, refreshes the OLED,
 * and processes Modbus RTU requests over USART1.
 */

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


#include <stdio.h>
#include <string.h>

#include "aht20.h"
#include "bmp280.h"
#include "env_data.h"
#include "led_control.h"
#include "ssd1306.h"
#include "uart_cmd.h"
#include "modbus_rtu.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define OLED_REPORT_INTERVAL_MS 10000U

uint32_t report_tick = 0U;



uint8_t cli_rx_byte = 0U; // UART2 CLI RX buffer
uint8_t modbus_rx_byte = 0U;

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

UART_Handler g_cli_uart;
int auto_report_mode = 0;




uint8_t modbus_rx_buf[MODBUS_MIN_REQUEST_SIZE];
uint8_t modbus_tx_buf[MODBUS_MAX_TX_SIZE];

volatile uint16_t modbus_rx_idx = 0U;
volatile uint8_t modbus_request_ready = 0U;


volatile uint8_t g_temp_valid = 0U;
volatile uint8_t g_humi_valid = 0U;
volatile uint8_t g_press_valid = 0U;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Disable printf() output to avoid contaminating the Modbus line. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 100);
    return len;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
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

    }
    else if (huart->Instance == USART2)
    {
    	if((cli_rx_byte == '\r') || (cli_rx_byte == '\n'))
    	{
    		if(g_cli_uart.rx_idx > 0)
    		{
    			g_cli_uart.rx_buffer[g_cli_uart.rx_idx] = '\0';
    			g_cli_uart.flags.bits.is_ready = 1;
    			g_cli_uart.state = SYS_COMMAND_PENDING;
    		}
    	}
    	else
    	{
    		if(g_cli_uart.rx_idx < (sizeof(g_cli_uart.rx_buffer)-1))
    		{
    			g_cli_uart.rx_buffer[g_cli_uart.rx_idx++] = (char)cli_rx_byte;
    		}
    		else
    		{
    			memset(g_cli_uart.rx_buffer, 0, sizeof(g_cli_uart.rx_buffer));
    			g_cli_uart.rx_idx = 0;
    			g_cli_uart.flags.all_flags = 0;
    			g_cli_uart.state = SYS_ERROR;
    		}
    	}

    	HAL_UART_Receive_IT(&huart2, &cli_rx_byte, 1);


    }

}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  memset(&g_cli_uart, 0, sizeof(UART_Handler));

  memset(modbus_rx_buf, 0, sizeof(modbus_rx_buf));
  memset(modbus_tx_buf, 0, sizeof(modbus_tx_buf));
  modbus_rx_idx = 0U;
  modbus_request_ready = 0U;

  HAL_UART_Receive_IT(&huart1, &modbus_rx_byte, 1);
  HAL_UART_Receive_IT(&huart2, &cli_rx_byte, 1);

  UART_LOG("CLI ready on USART6.");

  HAL_TIM_Base_Start_IT(&htim2);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

  report_tick = HAL_GetTick();

  UART_LOG("Scanning I2C bus...");

  for (uint8_t i = 1; i < 128; i++)
  {
      HAL_StatusTypeDef result =
          HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1), 3, 5);

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

  BMP280_Init();
  AHT20_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

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

            UART_LOG("OLED Updated with Sensor Data!");

            if (auto_report_mode)
            {
                UART_LOG("[Auto Report] T:%s%d.%02dC, H:%d.%02d%%, P:%ldhPa",
                         t.sign_str, t.whole, t.frac,
                         h.whole, h.frac,
                         (long)p);
            }

        }


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

        UART_ProcessCommand(&g_cli_uart);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
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

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
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
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
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
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
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
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
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
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PA11 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
