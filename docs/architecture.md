# Architecture

## Overview

이 프로젝트는 **super-loop 기반 구조** 위에서 동작하며,  
센서 측정은 `HAL_GetTick()` 기반의 **non-blocking 상태머신 task**로 구성했습니다.

메인 루프는 센서 task, CLI 처리, OLED 갱신, LED 제어, Modbus request 처리 및 response 생성을 순차적으로 실행합니다.  
UART 입력은 인터럽트 기반으로 수신하고, 실제 parsing / handling 로직은 main loop에서 수행하도록 분리했습니다.

```mermaid
flowchart LR
  subgraph App[Application Layer]
    MAIN[main.c\nsuper-loop scheduler]
    CLI[uart_cmd.c\nCLI command parser]
    MODBUS[modbus_rtu.c\nRTU parse / response build]
    OLED[ssd1306.c\nOLED update every 10s]
    LED[led_control.c\nLED_Update_Handler + LED_Auto_Control]
    ENV[env_data.c\nformatting + fixed-point conversion]
  end

  subgraph Drivers[Driver Layer]
    BMP[bmp280.c\nBMP280_Init / BMP280_Task]
    AHT[aht20.c\nAHT20_Init / AHT20_Task]
  end

  subgraph HAL[HAL / Peripherals]
    I2C[I2C1]
    UART1[USART1 VCP RX interrupt\nCLI mode]
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

  CLI <--> UART1
  MODBUS <--> UART1
  LED <--> TIM
  MAIN <--> TICK
  BMP <--> TICK
  AHT <--> TICK
