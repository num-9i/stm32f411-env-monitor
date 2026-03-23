## BMP280

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s elapsed\ntrigger forced measurement
    MEASURING --> MEASURING: poll throttle
    MEASURING --> DATA_READY: status bit3 == 0\nmeasurement complete
    MEASURING --> IDLE: status read fail\ntimeout / error
    DATA_READY --> IDLE: burst read raw data\ncompensate temp / press
    DATA_READY --> IDLE: burst read fail
```
If the status read fails or the measurement does not complete within 200 ms, the task returns to `IDLE` and increments `error_count`.
When the burst read succeeds, the task updates the latest temperature and pressure data and sets the corresponding valid flags.


## AHT20

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> MEASURING: 1 s elapsed\ntrigger measurement
    MEASURING --> MEASURING: poll throttle
    MEASURING --> IDLE: bit7 == 0\nread 6 bytes\nupdate temp / humidity
    MEASURING --> IDLE: read fail\ntimeout / error

```
When the busy bit is cleared, the task reads 6 bytes from the sensor, assembles the raw humidity and temperature values, and updates the latest data.
If the read fails or the measurement does not complete within 200 ms, the task returns to `IDLE` and increments `error_count`.
When humidity data is updated successfully, the corresponding valid flag is also set.