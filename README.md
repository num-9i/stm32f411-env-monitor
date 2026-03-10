# STM32F411RE Environmental Monitoring System

STM32F411RE (NUCLEO-F411RE) 기반 환경 모니터링 펌웨어 프로젝트입니다.  
BMP280(온도/기압), AHT20(습도), SSD1306 OLED, UART CLI를 사용해 센서 데이터를 수집하고 표시하며, 현재 동작 상태를 확인할 수 있도록 구성했습니다.

이 프로젝트의 핵심은 센서 측정 경로를 `HAL_Delay()` 기반 순차 처리에서 `HAL_GetTick()` 기반 non-blocking 상태머신으로 재구성한 점입니다.  
각 센서는 독립적인 task로 동작하며, 주기적 trigger, status polling, timeout 처리를 통해 super-loop 구조에서도 다른 기능과 병행 실행될 수 있도록 설계했습니다.

또한 raw sensor value와 사용자 표시용 값을 분리하고, `env_data` 레이어에서 반올림, 음수 처리, clamp, fixed-point 변환을 담당하도록 구성했습니다.  
UART CLI를 통해 측정값, 센서 상태, 디버그 정보, 주기 보고 기능을 확인할 수 있으며, OLED에는 최신 환경 정보를 주기적으로 표시합니다.

이 문서는 프로젝트의 구조, 설계 의도, 상태머신 동작, 빌드 환경, 주요 명령어를 정리한 기술 문서입니다.

## Documentation

- [Architecture Diagram](docs/architecture.md)
- [Sensor State Machines](docs/state_machines.md)

## Table of Contents

- [Project Highlights](#project-highlights)
- [Hardware](#hardware)
- [System Overview](#system-overview)
- [Main Loop](#main-loop)
- [Software Architecture](#software-architecture)
- [Architecture Diagram](#architecture-diagram)
- [Sensor Design](#sensor-design)
- [BMP280](#bmp280)
- [AHT20](#aht20)
- [Data Layer (`env_data`)](#data-layer-env_data)
- [UART CLI](#uart-cli)
- [LED Control](#led-control)
- [Boot Sequence](#boot-sequence)
- [Design Decisions](#design-decisions)
- [Example UART Output](#example-uart-output)
- [Current Limitations](#current-limitations)
- [Future Work](#future-work)
## Project Highlights

- STM32F411RE 기반 환경 모니터링 시스템(BMP280 / AHT20 / SSD1306)
- `HAL_Delay()`를 메인 측정 경로에서 제거하고, `HAL_GetTick()` 기반 non-blocking state machine으로 센서 측정을 비동기화
- UART CLI(`cmd_table`)로 `ENV`, `DEBUG`, `REPORT`, `SENSSTAT` 등 운영/디버깅 기능 제공
- 센서 Task는 1초 주기로 측정을 시작하고, OLED 갱신 및 auto report는 10초 주기로 수행
- 측정 완료 판정은 고정 delay 대신 센서 status bit polling으로 처리하고, polling throttle 및 software timeout을 함께 적용
- 환경 값과 raw ADC 데이터는 `env_data` 레이어로 통합하고, 반올림 / 음수 부호 / 클램핑 등 표현 로직을 별도 분리
- I2C error count를 노출해 센서 통신 이상 여부를 런타임에 확인 가능

## Hardware

- MCU: STM32F411RE
- Sensor 1: BMP280 (Temperature / Pressure)
- Sensor 2: AHT20 (Humidity)
- Display: SSD1306 OLED
- Communication: USART2 VCP, 115200 baud
- Timing / PWM: TIM2
- Bus: I2C1 (100 kHz)

## System Overview

부팅 후 시스템은 UART 수신 인터럽트, TIM2 PWM, I2C1을 초기화하고 I2C bus scan을 수행한 뒤 OLED와 센서를 초기화합니다. 이후 main loop에서는 두 센서 Task를 매 루프 호출하고, 드라이버 내부 최신 측정값을 `g_temp`, `g_humi`, `g_press` 및 raw 데이터 전역값으로 반영합니다. LED 자동 제어는 최신 습도값을 사용하고, OLED 표시 및 UART auto report는 10초 주기로만 수행됩니다. UART 명령 처리는 루프 말단에서 수행됩니다.

## Main Loop

```c
while (1)
{
    BMP280_Task();
    AHT20_Task();

    BMP280_Get_Data(&g_temp, &g_press);
    AHT20_Get_Data(NULL, &g_humi);

    BMP280_Get_RawAdc(&g_raw_adc_T, &g_raw_adc_P);
    g_raw_humi = AHT20_Get_RawHumi();

    LED_Auto_Control(g_humi);

    if (HAL_GetTick() - report_tick >= 10000)
    {
        report_tick = HAL_GetTick();
        // OLED update + optional auto report
    }

    UART_ProcessCommand(&g_uart2);
}
