# STM32F411RE Environmental Monitoring System

STM32F411RE (NUCLEO-F411RE) 기반 환경 모니터링 펌웨어 프로젝트입니다.  
BMP280(온도/기압), AHT20(습도), SSD1306 OLED, UART CLI를 사용해 센서 데이터를 수집하고 표시하며, 현재 동작 상태를 확인할 수 있도록 구성했습니다.

이 프로젝트의 핵심은 센서 측정 경로를 `HAL_Delay()` 기반 순차 처리에서 `HAL_GetTick()` 기반 non-blocking 상태머신으로 재구성한 점입니다.  
각 센서는 독립적인 task로 동작하며, 주기적 trigger, status polling, timeout 처리를 통해 super-loop 구조에서도 다른 기능과 병행 실행될 수 있도록 설계했습니다.

또한 raw sensor value와 사용자 표시용 값을 분리하고, `env_data` 레이어에서 반올림, 음수 처리, clamp, fixed-point 변환을 담당하도록 구성했습니다.  
UART CLI를 통해 측정값, 센서 상태, 디버그 정보, 주기 보고 기능을 확인할 수 있으며, OLED에는 최신 환경 정보를 주기적으로 표시합니다.

추가로, 외부 장치가 표준 산업용 프로토콜로 센서 데이터를 읽을 수 있도록 **Modbus RTU Slave 통신**을 확장했고, **RS485 실통신**까지 검증했습니다.

이 문서는 프로젝트의 구조, 설계 의도, 상태머신 동작, 빌드 환경, 주요 명령어를 정리한 기술 문서입니다.

## Documentation

- [Architecture Diagram](docs/architecture.md)
- [Sensor State Machines](docs/state_machines.md)

## Table of Contents

- [Project Highlights](#project-highlights)
- [Design Goals](#design-goals)
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
- [Modbus RTU / RS485](#modbus-rtu--rs485)
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
- **Modbus RTU (0x03 Read Holding Registers)** 를 **RS485** 위에 구현했고, **USART1 (PA9/PA10)** 을 사용해 Modbus Poll과의 실통신을 검증

## Design Goals

본 프로젝트는 다음과 같은 설계 목표를 기준으로 구성했습니다.

- Delay 기반 센서 측정 흐름을 **non-blocking 상태머신(State Machine)** 구조로 전환
- 다중 페리퍼럴 동작 상황에서도 super-loop의 응답성 유지
- 센서 raw 데이터 수집부와 사용자 표시용 데이터 가공부(formatting)의 분리
- UART CLI를 통한 런타임 상태 확인 및 디버깅 가능성 확보
- 센서 대기 상태와 I2C 통신 오류에 대한 명시적 timeout / fault handling 적용

## Hardware

- MCU: STM32F411RE (NUCLEO-F411RE)
- Sensor 1: BMP280 (Temperature / Pressure)
- Sensor 2: AHT20 (Humidity)
- Display: SSD1306 OLED
- Timing / PWM: TIM2
- Bus: I2C1 (100 kHz)
- Communication:
  - USART1 (PA9/PA10) for Modbus RTU / RS485
  - UART CLI / debug path used during bring-up
- External interface:
  - TTL-to-RS485 transceiver
  - USB-to-RS485 adapter
  - Modbus Poll for validation

## System Overview

부팅 후 시스템은 UART 수신 인터럽트, TIM2 PWM, I2C1을 초기화하고 I2C bus scan을 수행한 뒤 OLED와 센서를 초기화합니다. 이후 main loop에서는 두 센서 Task를 매 루프 호출하고, 드라이버 내부 최신 측정값을 `g_temp`, `g_humi`, `g_press` 및 raw 데이터 전역값으로 반영합니다. LED 자동 제어는 최신 습도값을 사용하고, OLED 표시 및 UART auto report는 10초 주기로만 수행됩니다. UART 명령 처리는 루프 말단에서 수행되며, Modbus RTU 모드에서는 holding register 요청을 파싱하고 응답 프레임을 생성합니다.

## Main Loop

~~~c
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

    // UART CLI or Modbus RTU processing
}
~~~

## Software Architecture

프로젝트는 기능별 모듈 분리를 기준으로 구성했습니다.

- `main.c`
  - 시스템 초기화
  - super-loop orchestration
  - 센서 task 호출
  - 데이터 반영
  - OLED / LED / UART / Modbus 연동
- `bmp280.c`
  - BMP280 초기화
  - 보정계수 처리
  - forced mode trigger
  - status polling / timeout / compensation
- `aht20.c`
  - AHT20 초기화
  - busy bit polling
  - humidity raw 데이터 조립
  - timeout 처리
- `env_data.c`
  - 사용자 표시용 값 가공
  - 반올림 / clamp / 부호 처리
  - fixed-point 변환
- `uart_cmd.c`
  - CLI 명령 파싱
  - 상태 조회 / 디버깅 / 제어 명령
- `led_control.c`
  - normal / blink / breath 출력
  - 자동 LED 동작
- `modbus_rtu.c`
  - Modbus RTU frame parsing
  - CRC16 계산
  - holding register map 조회
  - exception / normal response 생성

## Architecture Diagram

상세 구조는 아래 문서를 참고하세요.

- [Architecture Diagram](docs/architecture.md)

## Sensor Design

센서 측정은 `HAL_Delay()` 기반 blocking 구조 대신, `HAL_GetTick()` 기반 non-blocking 상태머신으로 설계했습니다.  
각 센서는 독립적인 task 함수로 주기적으로 호출되며, 내부 상태에 따라 trigger, polling, timeout, data-ready 판정을 수행합니다.

## BMP280

BMP280은 다음 흐름으로 동작합니다.

- `IDLE`
- `MEASURING`
- `DATA_READY`

forced mode를 trigger한 뒤, status register의 measuring bit를 polling하여 측정 완료를 판정합니다.  
고정 delay를 사용하지 않고 polling throttle과 timeout을 함께 두어, super-loop 응답성을 유지하면서도 비정상 대기 상태를 방지했습니다.

## AHT20

AHT20은 다음 흐름으로 동작합니다.

- `IDLE`
- `MEASURING`

측정 명령 전송 후 busy bit를 polling하여 완료를 판정합니다.  
이 과정에서도 polling throttle과 timeout을 적용해 센서 task가 루프를 과도하게 점유하지 않도록 했습니다.

## Data Layer (`env_data`)

`env_data` 레이어는 내부 raw sensor value와 사용자 표시용 값을 분리하는 역할을 담당합니다.

주요 책임은 다음과 같습니다.

- 반올림
- 음수 부호 처리
- clamp
- fixed-point 변환
- 표시용 format 정리

이를 통해 센서 드라이버는 “측정과 보정”에 집중하고, 표시/UI/통신 계층은 가공된 값을 일관되게 사용할 수 있도록 했습니다.

## UART CLI

CLI는 사람이 읽기 쉬운 형태로 현재 시스템 상태를 확인하고, 센서/LED 동작을 제어하기 위한 디버깅 인터페이스입니다.

예시 명령:

- `ENV`
- `DEBUG`
- `REPORT`
- `SENSSTAT`
- `LED`
- `INTERVAL`
- `BRIGHT`
- `BREATH`
- `BLINK`
- `BREATHSPEED`
- `RESET`
- `TIMER`

## LED Control

LED 제어는 TIM2 PWM을 기반으로 동작하며, 다음 모드를 지원합니다.

- normal
- blink
- breath

또한 최신 습도값을 기준으로 자동 제어 로직을 적용해, 센서값 변화에 따라 LED 동작이 바뀌도록 구성했습니다.

## Boot Sequence

부팅 시 시스템은 다음 순서로 초기화됩니다.

1. HAL / Clock 초기화
2. GPIO / UART / TIM / I2C 초기화
3. I2C bus scan
4. OLED 초기화
5. BMP280 / AHT20 초기화
6. super-loop 진입

## Design Decisions

이 프로젝트에서 중요하게 본 설계 선택은 다음과 같습니다.

- `HAL_Delay()` 제거를 통한 non-blocking 구조 전환
- 센서별 독립 상태머신 적용
- polling throttle + timeout 조합
- raw data / display data 분리
- CLI 기반 런타임 진단 가능성 확보
- error count를 통해 통신 문제를 외부에서 확인 가능하도록 구성
- Modbus register는 float 대신 fixed-point 정수 형식으로 제공

## Example UART Output

CLI를 통해 환경값, 디버그 정보, 센서 상태를 텍스트 기반으로 확인할 수 있습니다.

~~~text
> ENV
Temperature : 27.11 C
Humidity    : 56.42 %
Pressure    : 1013 hPa

> SENSSTAT
BMP280 state : DATA_READY
AHT20 state  : MEASURING
BMP err cnt  : 0
AHT err cnt  : 0
~~~

## Modbus RTU / RS485

이 프로젝트는 외부 장치가 표준 산업용 프로토콜로 환경 데이터를 읽을 수 있도록 **Modbus RTU Slave 통신**을 추가했습니다.  
현재 구현 범위는 **Function Code `0x03` (Read Holding Registers)** 이며, 주요 센서값과 상태 정보를 holding register에 매핑해 PC master에서 읽을 수 있도록 구성했습니다.

### Why USART1 was used

NUCLEO-F411RE 보드에서는 기본 **USART2(PA2/PA3)** 가 **ST-LINK VCP** 경로와 연결되어 있어, 외부 TTL-to-RS485 모듈과 직접 연동할 때 제약이 있었습니다.
초기에는 USART2 기반으로 Modbus RTU를 붙이려 했지만, 실제 RS485 통신 경로를 검증하는 과정에서 보드 라우팅 특성 때문에 실통신 구성이 불편하다는 점을 확인했습니다.

이를 해결하기 위해 Modbus RTU 통신 포트를 **USART1(PA9/PA10)** 으로 재배치하였으며 
이 과정에서 `USART1_IRQHandler()`와 `USART1_IRQn` NVIC enable 설정이 필요했고, 인터럽트 기반 UART 수신 경로를 완성한 뒤 
최종적으로 **RS485 기반 Modbus RTU 실통신**을 확인했습니다.

### Current Modbus Scope

현재 Modbus 구현 범위는 다음과 같습니다.

- Protocol: **Modbus RTU Slave**
- Physical layer: **RS485**
- Supported function:
  - `0x03` Read Holding Registers

초기 구현에서는 읽기 경로를 안정적으로 검증하는 데 집중하기 위해, holding register 읽기와 register map 검증을 우선 범위로 잡았습니다.

### Holding Register Map

| Address | Name | Description | Format |
|---|---|---|---|
| `0x0000` | Temperature | 온도 값 | `°C x100` |
| `0x0001` | Humidity | 상대습도 값 | `%RH x100` |
| `0x0002` | Pressure | 기압 값 | `hPa x10` |
| `0x0003` | Status Flags | 센서 / 애플리케이션 상태 비트 | bit field |
| `0x0004` | BMP280 Error Count | BMP280 통신 / 상태 에러 카운터 | unsigned |
| `0x0005` | AHT20 Error Count | AHT20 통신 / 상태 에러 카운터 | unsigned |
| `0x0006` | Reserved | 추후 확장용 예약 영역 | - |
| `0x0007` | Reserved | 추후 확장용 예약 영역 | - |

### Notes on Data Representation

Modbus register 인터페이스를 단순하고 예측 가능하게 유지하기 위해, 내부 float 센서값은 register에 올릴 때 **fixed-point 정수값**으로 변환했습니다.

- Temperature → `x100`
- Humidity → `x100`
- Pressure → `x10`

이 방식은 wire format을 단순하게 유지하면서도, 외부 모니터링 툴에서 충분한 해상도로 값을 읽을 수 있게 해줍니다.

### Verification

구현된 Modbus RTU Slave는 **Modbus Poll**과 **RS485 연결**을 이용해 검증했습니다.

- Slave ID: `1`
- Mode: `RTU`
- Function: `03 Read Holding Registers`
- UART: `USART1`
- Physical layer: `TTL-to-RS485 transceiver + USB-RS485 adapter`

![Modbus Poll real-time chart](docs/imgs/modbus_poll_realtime_chart.png)

*Modbus Poll을 이용해 holding register를 주기적으로 읽고, 온도(x100), 습도(x100), 상태값을 실시간으로 시각화한 화면.*

### Design Takeaway

이번 확장은 단순히 프로토콜을 추가한 작업이 아니라, 실제 하드웨어 환경에서 다음 요소들을 함께 검증하는 과정이었습니다.

- STM32 USART peripheral 설정
- interrupt handler / NVIC 설정
- TTL-to-RS485 transceiver 동작 확인
- PC master(Modbus Poll)와의 상호운용성 검증
- 보드 라우팅 제약에 따른 UART 경로 재배치

그 결과, 센서 데이터를 사람이 읽는 CLI 출력뿐 아니라 **산업용 표준 프로토콜 형태로 외부 장치가 직접 읽을 수 있는 구조**로 확장할 수 있었습니다.

## Current Limitations

- RTOS가 적용되지 않은 super-loop 기반 bare-metal 펌웨어
- UART CLI는 machine-to-machine 통신보다는 human-readable 진단 및 개발 편의성에 초점이 맞춰져 있음
- 현재 Modbus 구현은 `0x03` Read Holding Registers 중심의 1차 버전이며, write 계열 function code는 아직 지원하지 않음
- 현재 버전에는 전원 차단 이후에도 데이터를 유지하는 persistent logging 기능이 없음

## Future Work

- **Modbus Write Support**: `0x06`, `0x10` 등 write 계열 function code 추가
- **External Flash Logging**: W25Qxx SPI 플래시 메모리를 이용한 비휘발성 데이터 로깅 기능 추가
- **RTC Integration**: 로깅 데이터에 정확한 timestamp를 기록하기 위한 RTC 연동
- **Protocol / Interface Refinement**: CLI와 machine-to-machine 통신 경로를 더 명확히 분리하고, Modbus frame handling을 확장
