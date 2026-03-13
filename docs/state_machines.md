## BMP280

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s elapsed\ntrigger measurement
    MEASURING --> MEASURING: poll throttle
    MEASURING --> DATA_READY: bit3 == 0\nmeasurement complete
    MEASURING --> IDLE: status read fail\ntimeout / error
    DATA_READY --> IDLE: burst read raw data\ncompensate temp / press
    DATA_READY --> IDLE: burst read fail
```
If the status read fails or the measurement does not complete within 200 ms, the task returns to `IDLE` and increments `error_count`.


## AHT20

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s elapsed\ntrigger measurement
    MEASURING --> MEASURING: poll throttle
    MEASURING --> IDLE: bit7 == 0/update humidity
    MEASURING --> IDLE: read fail/timeout
