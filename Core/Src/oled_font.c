//
// Created by Kenneth Yan on 2026/11/20.
//
#include "oled_font.h"

const uint8_t F6x8[][6] = {
    0x00,0x00,0x00,0x00,0x00,0x00, // 空格
    0x00,0x00,0x00,0x2F,0x00,0x00, // !
    /* ……完整表用取模工具生成或从现有字库拷贝…… */
};

const uint8_t F8X16[] = {
    /* 每字符 16 字节，共 95 个可打印字符 */
};

const uint8_t Hzk[][32] = {
    /* 每个 16x16 汉字 32 字节 */
};