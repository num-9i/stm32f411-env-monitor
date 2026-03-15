

#ifndef INC_MODBUS_RTU_H_
#define INC_MODBUS_RTU_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define MODBUS_SLAVE_ADDR              0x01U
#define MODBUS_FUNC_READ_HOLDING_REGS  0x03U

#define MODBUS_EXCEPTION_ILLEGAL_FUNC  0x01U
#define MODBUS_EXCEPTION_ILLEGAL_ADDR  0x02U
#define MODBUS_EXCEPTION_ILLEGAL_VALUE 0x03U

#define MODBUS_MIN_REQUEST_SIZE        8U
#define MODBUS_MAX_REG_COUNT           8U
#define MODBUS_MAX_TX_SIZE             64U

#define MB_STATUS_TEMP_VALID           ((uint16_t)(1U << 0))
#define MB_STATUS_HUMI_VALID           ((uint16_t)(1U << 1))
#define MB_STATUS_PRESS_VALID          ((uint16_t)(1U << 2))
#define MB_STATUS_AUTO_REPORT          ((uint16_t)(1U << 3))
#define MB_STATUS_BMP_ERROR            ((uint16_t)(1U << 4))
#define MB_STATUS_AHT_ERROR            ((uint16_t)(1U << 5))

typedef enum
{
    MODBUS_OK = 0,
    MODBUS_ERR_FRAME,
    MODBUS_ERR_CRC,
    MODBUS_ERR_SLAVE_ADDR,
    MODBUS_ERR_FUNC,
    MODBUS_ERR_ADDR,
    MODBUS_ERR_VALUE
} Modbus_Status_t;

typedef enum
{
    MB_REG_TEMP_X100     = 0x0000,
    MB_REG_HUMI_X100     = 0x0001,
    MB_REG_PRESS_HPA_X10 = 0x0002,
    MB_REG_STATUS_FLAGS  = 0x0003,
    MB_REG_BMP_ERR_COUNT = 0x0004,
    MB_REG_AHT_ERR_COUNT = 0x0005,
    MB_REG_RESERVED0     = 0x0006,
    MB_REG_RESERVED1     = 0x0007
} Modbus_RegAddr_t;

typedef struct
{
    uint8_t slave_addr;
    uint8_t function_code;
    uint16_t start_addr;
    uint16_t quantity;
} Modbus_Request_t;

uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length);

Modbus_Status_t Modbus_ParseRequest(const uint8_t *rx_buf,
                                    uint16_t rx_len,
                                    Modbus_Request_t *request);

bool Modbus_GetHoldingRegister(uint16_t addr, uint16_t *value);

uint16_t Modbus_BuildReadHoldingResponse(const Modbus_Request_t *request,
                                         uint8_t *tx_buf,
                                         uint16_t tx_buf_size);

uint16_t Modbus_BuildExceptionResponse(uint8_t slave_addr,
                                       uint8_t function_code,
                                       uint8_t exception_code,
                                       uint8_t *tx_buf,
                                       uint16_t tx_buf_size);

#endif /* INC_MODBUS_RTU_H_ */
