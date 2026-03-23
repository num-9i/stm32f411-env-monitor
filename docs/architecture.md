# Architecture

## Overview

This project uses a super-loop architecture with non-blocking sensor tasks.

```mermaid
flowchart LR
  subgraph App[Application Layer]
    MAIN[main.c\nsuper-loop scheduler]
    CLI[uart_cmd.c\nCLI command parser]
    MODBUS[modbus_rtu.c\nRTU parse / response build]
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
    UART2[USART2 VCP RX interrupt\nCLI mode]
    UART1[USART1 RX interrupt\nModbus RTU / RS485]
    TIM[TIM2 PWM / 1ms callback]
    TICK[HAL_GetTick]
  end

  MAIN --> BMP
  MAIN --> AHT
  MAIN --> CLI
  MAIN --> MODBUS
  MAIN --> OLED
  MAIN --> LED

  BMP --> ENV
  AHT --> ENV

  ENV --> CLI
  ENV --> OLED
  ENV --> LED
  ENV --> MODBUS

  BMP <--> I2C
  AHT <--> I2C
  OLED <--> I2C

  CLI <--> UART2
  MODBUS <--> UART1
  LED <--> TIM
  MAIN <--> TICK
  BMP <--> TICK
  AHT <--> TICK
