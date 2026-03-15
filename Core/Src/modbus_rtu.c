#include "modbus_rtu.h"

#include "env_data.h"
#include "bmp280.h"
#include "aht20.h"

extern int auto_report_mode;

static uint16_t Modbus_BuildStatusFlags(void);

uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001U)
            {
                crc = (crc >> 1) ^ 0xA001U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

Modbus_Status_t Modbus_ParseRequest(const uint8_t *rx_buf,
                                    uint16_t rx_len,
                                    Modbus_Request_t *request)
{
    if ((rx_buf == NULL) || (request == NULL))
    {
        return MODBUS_ERR_FRAME;
    }

    if (rx_len < MODBUS_MIN_REQUEST_SIZE)
    {
        return MODBUS_ERR_FRAME;
    }

    uint16_t rx_crc = (uint16_t)rx_buf[rx_len - 2]
                    | ((uint16_t)rx_buf[rx_len - 1] << 8);

    uint16_t calc_crc = Modbus_CRC16(rx_buf, (uint16_t)(rx_len - 2));

    if (rx_crc != calc_crc)
    {
        return MODBUS_ERR_CRC;
    }

    request->slave_addr    = rx_buf[0];
    request->function_code = rx_buf[1];
    request->start_addr    = ((uint16_t)rx_buf[2] << 8) | rx_buf[3];
    request->quantity      = ((uint16_t)rx_buf[4] << 8) | rx_buf[5];

    if (request->slave_addr != MODBUS_SLAVE_ADDR)
    {
        return MODBUS_ERR_SLAVE_ADDR;
    }

    if (request->function_code != MODBUS_FUNC_READ_HOLDING_REGS)
    {
        return MODBUS_ERR_FUNC;
    }

    if ((request->quantity == 0U) || (request->quantity > MODBUS_MAX_REG_COUNT))
    {
        return MODBUS_ERR_VALUE;
    }

    if ((request->start_addr + request->quantity) > (MB_REG_RESERVED1 + 1U))
    {
        return MODBUS_ERR_ADDR;
    }

    return MODBUS_OK;
}

bool Modbus_GetHoldingRegister(uint16_t addr, uint16_t *value)
{
    if (value == NULL)
    {
        return false;
    }

    switch (addr)
    {
    case MB_REG_TEMP_X100:
        *value = (uint16_t)Env_Get_Temp_X100();
        return true;

    case MB_REG_HUMI_X100:
        *value = Env_Get_Humi_X100();
        return true;

    case MB_REG_PRESS_HPA_X10:
        *value = Env_Get_Press_X10();
        return true;

    case MB_REG_STATUS_FLAGS:
        *value = Modbus_BuildStatusFlags();
        return true;

    case MB_REG_BMP_ERR_COUNT:
        *value = (uint16_t)BMP280_Get_ErrorCount();
        return true;

    case MB_REG_AHT_ERR_COUNT:
        *value = (uint16_t)AHT20_Get_ErrorCount();
        return true;

    case MB_REG_RESERVED0:
    case MB_REG_RESERVED1:
        *value = 0U;
        return true;

    default:
        return false;
    }
}

uint16_t Modbus_BuildReadHoldingResponse(const Modbus_Request_t *request,
                                         uint8_t *tx_buf,
                                         uint16_t tx_buf_size)
{
    if ((request == NULL) || (tx_buf == NULL))
    {
        return 0U;
    }

    uint16_t needed = (uint16_t)(3U + (request->quantity * 2U) + 2U);

    if (tx_buf_size < needed)
    {
        return 0U;
    }

    uint16_t idx = 0U;
    tx_buf[idx++] = request->slave_addr;
    tx_buf[idx++] = request->function_code;
    tx_buf[idx++] = (uint8_t)(request->quantity * 2U);

    for (uint16_t i = 0; i < request->quantity; i++)
    {
        uint16_t reg = 0U;

        if (!Modbus_GetHoldingRegister((uint16_t)(request->start_addr + i), &reg))
        {
            return 0U;
        }

        tx_buf[idx++] = (uint8_t)((reg >> 8) & 0xFFU);
        tx_buf[idx++] = (uint8_t)(reg & 0xFFU);
    }

    uint16_t crc = Modbus_CRC16(tx_buf, idx);
    tx_buf[idx++] = (uint8_t)(crc & 0xFFU);
    tx_buf[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    return idx;
}

uint16_t Modbus_BuildExceptionResponse(uint8_t slave_addr,
                                       uint8_t function_code,
                                       uint8_t exception_code,
                                       uint8_t *tx_buf,
                                       uint16_t tx_buf_size)
{
    if ((tx_buf == NULL) || (tx_buf_size < 5U))
    {
        return 0U;
    }

    uint16_t idx = 0U;
    tx_buf[idx++] = slave_addr;
    tx_buf[idx++] = function_code | 0x80U;
    tx_buf[idx++] = exception_code;

    uint16_t crc = Modbus_CRC16(tx_buf, idx);
    tx_buf[idx++] = (uint8_t)(crc & 0xFFU);
    tx_buf[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    return idx;
}

static uint16_t Modbus_BuildStatusFlags(void)
{
    uint16_t flags = 0U;

    flags |= MB_STATUS_TEMP_VALID;
    flags |= MB_STATUS_HUMI_VALID;
    flags |= MB_STATUS_PRESS_VALID;

    if (auto_report_mode)
    {
        flags |= MB_STATUS_AUTO_REPORT;
    }

    if (BMP280_Get_ErrorCount() > 0U)
    {
        flags |= MB_STATUS_BMP_ERROR;
    }

    if (AHT20_Get_ErrorCount() > 0U)
    {
        flags |= MB_STATUS_AHT_ERROR;
    }

    return flags;
}
