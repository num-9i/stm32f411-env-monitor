# Architecture

## Overview

This project uses a super-loop architecture with non-blocking sensor tasks.

```mermaid
flowchart LR
  subgraph App[Application Layer]
    MAIN[main.c\nsuper-loop scheduler]
    CLI[uart_cmd.c\nCLI command parser]
    OLED[ssd1306.c\nOLED Update every 10s]
    LED[led_control.c\nLED_Update_Handler + LED_Auto_Control]
    ENV[env_data.c\nformatting + fixed-point conversion]
  end

  subgraph Drivers[Driver Layer]
    BMP[bmp280.c\nBMP280_Init / BMP280_Task]
    AHT[aht20.c\nAHT20_Init / AHT20_Task]
  end

  subgraph HAL[HAL / Peripherals]
    I2C[I2C1]
    UART[USART2 VCP RX interrupt]
    TIM[TIM2 PWM / 1ms callback]
    TICK[HAL_GetTick]
  end

  MAIN --> BMP
  MAIN --> AHT
  MAIN --> CLI
  MAIN --> OLED
  MAIN --> LED

  BMP --> ENV
  AHT --> ENV

  ENV --> CLI
  ENV --> OLED
  ENV --> LED

  BMP <--> I2C
  AHT <--> I2C
  OLED <--> I2C

  CLI <--> UART
  LED <--> TIM
  MAIN <--> TICK
  BMP <--> TICK
  AHT <--> TICK
