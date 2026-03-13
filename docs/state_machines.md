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
