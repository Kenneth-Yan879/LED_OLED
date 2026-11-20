/* protocol.c */
#include "protocol.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

/* ---------- 接收状态机 ---------- */
typedef enum { ST_HEAD, ST_RUN, ST_DIR, ST_LO, ST_HI, ST_CHK } RxState_t;

static volatile RxState_t  s_state = ST_HEAD;
static LED_Packet_t        s_pkt;                       /* 正在拼装的包 */
static volatile LED_Packet_t s_done;                    /* 校验通过的包 */
static volatile uint8_t    s_new_flag = 0;              /* 有新包标志 */
static volatile uint32_t   s_err_cnt  = 0;              /* 丢包计数 */

/* 范围检查：间隔 100~2000ms，RUN/DIR 只允许 0 或 1 */
static uint8_t Packet_Valid(const LED_Packet_t *p)
{
    uint16_t step = (uint16_t)p->step_l | ((uint16_t)p->step_h << 8);
    if (p->run > 1 || p->dir > 1) return 0;
    if (step < 100 || step > 2000) return 0;
    return 1;
}

/* 每收到一个字节调用一次（在串口中断回调中） */
void Protocol_RxByte(uint8_t byte)
{
    switch (s_state)
    {
    case ST_HEAD:
        if (byte == 0xAA) { s_pkt.head = byte; s_state = ST_RUN; }
        /* 不是帧头就直接丢弃，保持等待 */
        break;
    case ST_RUN:  s_pkt.run    = byte; s_state = ST_DIR; break;
    case ST_DIR:  s_pkt.dir    = byte; s_state = ST_LO;  break;
    case ST_LO:   s_pkt.step_l = byte; s_state = ST_HI;  break;
    case ST_HI:   s_pkt.step_h = byte; s_state = ST_CHK; break;
    case ST_CHK:
        s_pkt.check = byte;
        /* 校验和 = 前5字节累加取低8位 */
        {
            uint8_t calc = (uint8_t)(s_pkt.head + s_pkt.run + s_pkt.dir +
                                     s_pkt.step_l + s_pkt.step_h);
            if (calc == s_pkt.check && Packet_Valid(&s_pkt))
            {
                s_done = s_pkt;
                s_new_flag = 1;
            }
            else
            {
                s_err_cnt++;          /* 校验不对：不处理该命令 */
            }
        }
        s_state = ST_HEAD;            /* 无论对错都回到帧头等待下一包 */
        break;
    default:
        s_state = ST_HEAD;
        break;
    }
}

/* ---------- 中断接收的启动与回调 ---------- */
static uint8_t s_rx_byte;

void Protocol_Init(void)
{
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);   /* 开单字节中断接收 */
}

/* HAL 库弱定义回调，在这里重写 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        Protocol_RxByte(s_rx_byte);               /* 喂给状态机 */
        HAL_UART_Receive_IT(huart, &s_rx_byte, 1);/* 重新挂中断，收下一个字节 */
    }
}

/* ---------- 主循环取指令 ---------- */
uint8_t Protocol_GetCmd(LED_Cmd_t *out)
{
    if (!s_new_flag) return 0;
    __disable_irq();                 /* 临界区：防止取到一半被中断改写 */
    s_new_flag = 0;
    out->run    = s_done.run;
    out->dir    = s_done.dir;
    out->step_ms = (uint16_t)s_done.step_l | ((uint16_t)s_done.step_h << 8);
    __enable_irq();
    return 1;
}

uint32_t Protocol_GetErrCount(void) { return s_err_cnt; }