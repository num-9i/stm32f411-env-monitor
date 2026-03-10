#ifndef INC_UART_CMD_H_
#define INC_UART_CMD_H_

#include "main.h"

typedef enum
{
    SYS_IDLE = 0,
    SYS_COMMAND_PENDING,
    SYS_ERROR
} SystemState;

typedef union
{
    uint8_t all_flags;
    struct
    {
        uint8_t is_ready   : 1;
        uint8_t led_status : 1;
        uint8_t uart_error : 1;
        uint8_t reserved   : 5;
    } bits;
} SystemFlags;

typedef struct
{
    char rx_buffer[64];
    uint8_t rx_idx;
    SystemFlags flags;
    SystemState state;
} UART_Handler;

typedef void (*CmdFunc)(char *arg);

typedef struct
{
    const char *cmdStr;
    CmdFunc func;
} CommandEntry;

void UART_ProcessCommand(UART_Handler *hUart);
void UART_LOG(const char *fmt, ...);

#endif /* INC_UART_CMD_H_ */
