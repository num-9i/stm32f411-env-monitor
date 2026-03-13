## BMP280

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s elapsed
    MEASURING --> MEASURING: poll wait
    MEASURING --> DATA_READY: bit3 == 0
    MEASURING --> IDLE: status fail
    MEASURING --> IDLE: timeout
    DATA_READY --> IDLE: read + compensate
    DATA_READY --> IDLE: burst fail
