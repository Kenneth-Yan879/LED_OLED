#include "oled.h"
#include "oled_font.h"
#include "main.h"   /* CubeMX 生成的 OLED_SCL_Pin / OLED_SDA_Pin 宏 */

/* ================== 可配置区 ================== */

/* SSD1306 I2C 从机地址（7bit 0x3C 左移一位）。若模块 SA0 接高，改成 0x7A */
#define OLED_I2C_ADDR          0x78

/* [FIX-1] 字库起始 ASCII。通行字库从空格(0x20)开始；若你的表从 0x00 铺满 256 项，改为 0x00 */
#define OLED_FONT_ASCII_OFFSET 0x20
#define OLED_FONT_CHAR_NUM     95      /* 0x20~0x7E 共 95 个 */

/* [FIX-4] 软件 I2C 延时循环次数。
 * SSD1306手册 Table 13-6 给出的 I2C 时序最小值：
 *   tCYCLE  >= 2.5us（即 SCL 上限约 400kHz）
 *   tHSTART >= 0.6us   tSSTOP >= 0.6us   tIDLE >= 1.3us
 *   tSD >= 100ns       tHD(SDA_IN) >= 300ns
 * 72MHz 下取 30 次循环，加上 HAL_GPIO_WritePin 的调用开销，
 * 实测 SCL 周期约 4~5us（≈200~250kHz），留了余量。
 * 换主频或换编译优化等级后请用逻辑分析仪/示波器复测一次。 */
#define OLED_I2C_DELAY_COUNT   30

/* 屏幕几何 */
#define OLED_PAGE_NUM          8
#define OLED_COL_NUM           128
#define OLED_LINE_MAX          4       /* 8x16 字体，64 行 / 16 = 4 行文本 */
#define OLED_COL_CHR_MAX       16      /* 128 列 / 8 = 16 个字符 */

/* ================== 引脚操作（开漏输出 + 外部上拉） ================== */

#define OLED_W_SCL(x)  HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_W_SDA(x)  HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_R_SDA()   HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin)

/* [FIX-6] 通信错误计数。>0 说明总线上没拿到 ACK，可用于上层告警 */
static volatile uint16_t s_oled_ack_fail = 0;

uint16_t OLED_GetAckFailCount(void) { return s_oled_ack_fail; }

static void OLED_I2C_Delay(void)
{
    volatile uint16_t i = OLED_I2C_DELAY_COUNT;
    while (i--);
}

/* ================== 软件 I2C 底层 ================== */

static void OLED_I2C_Start(void)
{
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    OLED_I2C_Delay();          /* 保证 SCL 高电平期间总线稳定 */
    OLED_W_SDA(0);
    OLED_I2C_Delay();          /* [FIX-5] tHSTART >= 0.6us */
    OLED_W_SCL(0);
    OLED_I2C_Delay();          /* [FIX-5] SCL 拉低后再动 SDA，满足 tHD */
}

static void OLED_I2C_Stop(void)
{
    OLED_W_SDA(0);
    OLED_I2C_Delay();
    OLED_W_SCL(1);
    OLED_I2C_Delay();          /* [FIX-5] tSSTOP >= 0.6us */
    OLED_W_SDA(1);
    OLED_I2C_Delay();          /* [FIX-5] tIDLE >= 1.3us，两次延时凑够 */
    OLED_I2C_Delay();
}

static void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        OLED_W_SDA(Byte & 0x80);
        Byte <<= 1;
        OLED_I2C_Delay();      /* [FIX-5] tSD >= 100ns，SDA 建立后再拉高 SCL */
        OLED_W_SCL(1);
        OLED_I2C_Delay();
        OLED_W_SCL(0);
        OLED_I2C_Delay();
    }
    OLED_W_SDA(1);             /* 释放 SDA，准备读 ACK */
}

/* 返回 1=收到 ACK，0=无 ACK（同时累加错误计数） */
static uint8_t OLED_I2C_WaitAck(void)
{
    uint8_t ack;
    OLED_W_SDA(1);             /* 释放 SDA，靠上拉电阻拉高 */
    OLED_I2C_Delay();
    OLED_W_SCL(1);
    OLED_I2C_Delay();
    ack = (OLED_R_SDA() == GPIO_PIN_RESET) ? 1 : 0;
    OLED_W_SCL(0);
    OLED_I2C_Delay();
    if (!ack) { s_oled_ack_fail++; if (s_oled_ack_fail) s_oled_ack_fail--; s_oled_ack_fail++; }
    return ack;
}

/* 写一个字节并检查 ACK，返回 0 表示失败 */
static uint8_t OLED_I2C_WriteByteChk(uint8_t b)
{
    OLED_I2C_SendByte(b);
    return OLED_I2C_WaitAck();
}

/* ================== 命令 / 数据 ================== */

/* [FIX-6] 返回 1 成功，0 失败 */
static uint8_t OLED_WriteCommand(uint8_t Command)
{
    uint8_t ok;
    OLED_I2C_Start();
    ok  = OLED_I2C_WriteByteChk(OLED_I2C_ADDR);
    ok &= OLED_I2C_WriteByteChk(0x00);       /* 控制字节：Co=0, D/C#=0 -> 命令 */
    ok &= OLED_I2C_WriteByteChk(Command);
    OLED_I2C_Stop();
    return ok;
}

/* [FIX-9] 开启一次数据流：Start + 地址 + 0x40 控制字节，之后可连发多字节 */
static uint8_t OLED_DataStreamBegin(void)
{
    uint8_t ok;
    OLED_I2C_Start();
    ok  = OLED_I2C_WriteByteChk(OLED_I2C_ADDR);
    ok &= OLED_I2C_WriteByteChk(0x40);       /* 控制字节：Co=0, D/C#=1 -> 数据 */
    return ok;
}

static void OLED_DataStreamEnd(void) { OLED_I2C_Stop(); }

/* 单字节写（保留给零散调用） */
static uint8_t OLED_WriteData(uint8_t Data)
{
    uint8_t ok = OLED_DataStreamBegin();
    if (ok) ok = OLED_I2C_WriteByteChk(Data);
    OLED_DataStreamEnd();
    return ok;
}

/* [FIX-9] 连续写 n 字节，整个流只有一对 Start/Stop */
static uint8_t OLED_WriteDataMulti(const uint8_t *p, uint16_t n)
{
    uint8_t ok = OLED_DataStreamBegin();
    while (n--) { ok &= OLED_I2C_WriteByteChk(*p++); }
    OLED_DataStreamEnd();
    return ok;
}

/* 定位光标。Y: 页 0~7   X: 列 0~127
 * 页寻址模式下：写数据时列地址自动递增，递增到列结束地址后回到列起始地址，
 * 页地址不变，需要用户重新设页 —— 所以每页写满 128 列不会串到下一页。 */
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | (Y & 0x07));          /* 先设页 */
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));   /* 列高 4 位 */
    OLED_WriteCommand(0x00 | (X & 0x0F));          /* 列低 4 位 */
}

/* ================== 初始化 ================== */

void OLED_Init(void)
{
    /* [FIX-7] 前提：CubeMX 中 SCL/SDA 必须是 GPIO_MODE_OUTPUT_OD（开漏）+ 上拉。
     * 若为推挽输出，WaitAck 期间主机会与从机在 SDA 上冲突，永远读不到 ACK。 */
    OLED_W_SCL(1);
    OLED_W_SDA(1);
    HAL_Delay(100);                /* 等 OLED 上电稳定 */

    OLED_WriteCommand(0xAE);       /* 关显示 */
    OLED_WriteCommand(0xD5); OLED_WriteCommand(0x80);  /* 时钟分频 */
    OLED_WriteCommand(0xA8); OLED_WriteCommand(0x3F);  /* MUX = 64 行 */
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00);  /* 显示偏移 */
    OLED_WriteCommand(0x40);       /* 起始行 */
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14);  /* 开内部电荷泵，缺这步必黑屏 */
    HAL_Delay(20);                 /* 等电荷泵升压。注：有中文博客建议 100ms，
                                    * 但该说法非手册原文，此处取 20ms 折中；
                                    * 若出现首次上电偶发不亮，把它加大到 100ms 再试 */
    OLED_WriteCommand(0x20); OLED_WriteCommand(0x02);  /* 页寻址模式 */
    OLED_WriteCommand(0xA1);       /* 段重映射 */
    OLED_WriteCommand(0xC8);       /* COM 扫描方向 */
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12);  /* COM 引脚配置 */
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1);  /* 预充电 */
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x40);  /* VCOMH */
    OLED_WriteCommand(0x81); OLED_WriteCommand(0xCF);  /* 对比度 */
    OLED_WriteCommand(0xA4);       /* 按显存显示 */
    OLED_WriteCommand(0xA6);       /* 正显 */
    OLED_Clear();                  /* 先清显存，避免上电残留 */
    OLED_WriteCommand(0xAF);       /* 最后开显示 */
}

/* ================== 绘图接口 ================== */

void OLED_Clear(void)
{
    uint8_t page, buf[OLED_COL_NUM];
    for (page = 0; page < OLED_COL_NUM; page++) buf[page] = 0x00;   /* 复用上电初始化后的清零 */
    for (page = 0; page < OLED_PAGE_NUM; page++)
    {
        OLED_SetCursor(page, 0);
        OLED_WriteDataMulti(buf, OLED_COL_NUM);   /* [FIX-9] 每页一次 Start/Stop */
    }
}

/* 只显示一行（Line 1~4）时可用这个轻量版本，省 128 字节栈 */
void OLED_ClearLine(uint8_t Line)
{
    uint8_t i, page;
    if (Line == 0 || Line > OLED_LINE_MAX) return;
    for (page = (Line - 1) * 2; page <= (Line - 1) * 2 + 1; page++)
    {
        OLED_SetCursor(page, 0);
        if (OLED_DataStreamBegin())
            for (i = 0; i < OLED_COL_NUM; i++) OLED_I2C_SendByte(0x00), OLED_I2C_WaitAck();
        OLED_DataStreamEnd();
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint16_t idx;
    uint8_t  i;

    /* [FIX-2] 参数防护，避免 uint8_t 下溢 */
    if (Line == 0 || Line > OLED_LINE_MAX) return;
    if (Column == 0 || Column > OLED_COL_CHR_MAX) return;

    /* [FIX-1] 字库从 OLED_FONT_ASCII_OFFSET 开始，越界字符显示为空格 */
    {
        uint8_t c = (uint8_t)Char;
        if (c < OLED_FONT_ASCII_OFFSET ||
            c >= OLED_FONT_ASCII_OFFSET + OLED_FONT_CHAR_NUM) c = OLED_FONT_ASCII_OFFSET;
        idx = (uint16_t)(c - OLED_FONT_ASCII_OFFSET) * 16;
    }

    /* 上半页：8 列 */
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    OLED_WriteDataMulti(&OLED_F8x16[idx], 8);
    /* 下半页：8 列 */
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    OLED_WriteDataMulti(&OLED_F8x16[idx + 8], 8);

    (void)i;
}

void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
    if (String == 0) return;
    while (*String && Column <= OLED_COL_CHR_MAX)
    {
        OLED_ShowChar(Line, Column, *String++);
        Column++;
    }
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint32_t pow10 = 1;
    uint8_t  i;

    if (Length == 0 || Length > 10) return;              /* [FIX-11] 防 pow10 溢出 */
    if (Column == 0 || Column + Length - 1 > OLED_COL_CHR_MAX) return;

    for (i = 1; i < Length; i++) pow10 *= 10;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, (char)('0' + (Number / pow10) % 10));
        pow10 /= 10;
    }
}