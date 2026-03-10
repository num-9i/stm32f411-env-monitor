

# Sensor State Machines

## BMP280

```mermaid
stateDiagram-v2
  [*] --> IDLE

  IDLE --> MEASURING: (now - last_bmp_read_time) >= 1000\nwrite 0xF4 = 0x25\nstart_tick = now
  IDLE --> IDLE: trigger write fail\nerror_count++\nlast_bmp_read_time = now

  MEASURING --> MEASURING: poll throttle < 5ms
  MEASURING --> IDLE: timeout > 200ms\nerror_count++
  MEASURING --> IDLE: status read fail\nerror_count++
  MEASURING --> DATA_READY: status bit3 == 0

  DATA_READY --> IDLE: burst read 0xF7..0xFC\nassemble adc_P / adc_T\ncompensate temp / press
  DATA_READY --> IDLE: burst read fail\nerror_count++