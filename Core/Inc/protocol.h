//
// Created by Kenneth Yan on 2026/11/20.
//

#ifndef LED_OLED_PROTOCOL_H
#define LED_OLED_PROTOCOL_H

#endif //LED_OLED_PROTOCOL_H
/* protocol.h */
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "main.h"

/* 数据包结构体：AA RUN DIR STEP_L STEP_H CHECK，共6字节 */
typedef struct
{
    uint8_t  head;      /* 帧头 0xAA */
    uint8_t  run;       /* 00暂停 01运行 */
    uint8_t  dir;       /* 00正向 01反向 */
    uint8_t  step_l;    /* 间隔低字节 */
    uint8_t  step_h;    /* 间隔高字节 */
    uint8_t  check;     /* 校验和 */
} LED_Packet_t;

/* 解析出的有效指令（校验通过后填充） */
typedef struct
{
    uint8_t  run;       /* 0/1 */
    uint8_t  dir;       /* 0/1 */
    uint16_t step_ms;   /* 100~2000 */
} LED_Cmd_t;

void Protocol_Init(void);                       /* 启动中断接收，要在 MX_USART1_UART_Init 之后调用 */
void Protocol_RxByte(uint8_t byte);             /* 在中断回调里喂字节 */
uint8_t Protocol_GetCmd(LED_Cmd_t *out);        /* 主循环轮询：有新指令返回1并取出，否则0 */
uint32_t Protocol_GetErrCount(void);            /* 帧头/校验错误计数，调试用 */

#endif
