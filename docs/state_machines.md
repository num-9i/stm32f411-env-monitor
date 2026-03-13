## BMP280

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s period elapsed\ntrigger forced measurement\nstart_tick = now

    MEASURING --> MEASURING: poll throttle
    MEASURING --> DATA_READY: status bit3 == 0\nmeasurement complete
    MEASURING --> IDLE: status read fail\nerror_count++
    MEASURING --> IDLE: timeout > 200 ms\nerror_count++

    DATA_READY --> IDLE: burst read raw data\ncompensate temp / press
    DATA_READY --> IDLE: burst read fail\nerror_count++
