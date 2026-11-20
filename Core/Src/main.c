/* USER CODE BEGIN Header */

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include "protocol.h"


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {                 /* 作业2：自定义结构体接收数据包 */
    uint8_t  run;                /* 0=暂停 1=运行 */
    uint8_t  dir;                /* 0=正向 1=反向 */
    uint16_t step_ms;            /* 流水间隔 ms */
} LedPacket_t;

typedef enum {                   /* 接收状态机 */
    RX_WAIT_HEAD = 0, RX_RUN, RX_DIR, RX_STEP_L, RX_STEP_H, RX_CHECK
} RxState_t;

typedef struct {                 
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint32_t      debounce_ms;
    uint8_t       last_raw;
    uint8_t       stable;
    uint32_t      change_tick;
    uint8_t       event;
} Key_t;

typedef struct {
    uint8_t  run;                /* 1=运行 0=暂停 */
    uint8_t  dir;                /* 0=正向 1=反向 */
    uint16_t step_ms;            /* 间隔 ms */
    uint8_t  led_idx;            /* 0..2 对应 LED_R/G/B */
    uint8_t  dirty;              /* OLED 需刷新 */
} AppState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PKT_HEAD      0xAA
#define PKT_LEN       6
#define STEP_MIN_MS   100
#define STEP_MAX_MS   2000
#define LED_NUM 3
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static const uint16_t    LED_PIN[3]     = { LED_R_Pin,  LED_G_Pin,  LED_B_Pin  };
static GPIO_TypeDef *const LED_PORT[3]  = { LED_R_GPIO_Port, LED_G_GPIO_Port, LED_B_GPIO_Port };

static AppState_t app = { 1, 0, 300, 0, 1 };   /* 上电: 运行 正向 300ms LED#1 需刷新 */

static Key_t key1 = { .port = KEY1_GPIO_Port, .pin = KEY1_Pin, .debounce_ms = 20 };
static Key_t key2 = { .port = KEY2_GPIO_Port, .pin = KEY2_Pin, .debounce_ms = 20 };

static char tx_buf[128];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void LEDs_Only(uint8_t idx);
static void OLED_Refresh(void);
static void UART_SendStr(const char *s);
static void Key_Scan(Key_t *k);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 只点亮第 idx 号 LED（低电平点亮） */
static void LEDs_Only(uint8_t idx)
{
    int i;
    for (i = 0; i < 3; i++)
        HAL_GPIO_WritePin(LED_PORT[i], LED_PIN[i],
                          (i == (int)idx) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}


static void OLED_Refresh(void)
{
    OLED_ShowString(1, 1, app.run ? "STATE: RUN " : "STATE: STOP");
    OLED_ShowString(2, 1, app.dir ? "DIR : REV " : "DIR : FWD ");
    OLED_ShowString(3, 1, "STEP:");
    OLED_ShowNum (3, 7, (uint32_t)app.step_ms, 4);
    OLED_ShowString(3, 11, "ms ");
    OLED_ShowString(4, 1, "LED :");
    OLED_ShowNum (4, 7, (uint32_t)(app.led_idx + 1), 1);
    OLED_ShowString(4, 9, "    ");
    app.dirty = 0;
}

static void UART_SendStr(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}


static void Key_Scan(Key_t *k)
{
    uint8_t raw = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_RESET) ? 0 : 1;
    if (raw != k->last_raw) {
        k->last_raw    = raw;
        k->change_tick = HAL_GetTick();
    }
    else if ((HAL_GetTick() - k->change_tick) >= k->debounce_ms)
    {
        if (raw != k->stable) {
            k->stable = raw;
            if (raw == 0) k->event = 1;
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  LEDs_Only(app.led_idx);          /* 上电点亮 LED#1 */
  OLED_Init();                     /* 须在 MX_GPIO_Init 之后 */
  OLED_Refresh();
  Protocol_Init();                 /* 内部已调用 HAL_UART_Receive_IT，不要再重复开 */
  UART_SendStr("System Ready\r\n");
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  LED_Cmd_t cmd;
  uint32_t  last_shift = HAL_GetTick();

  while (1)
  {
    /* 1. 按键（作业1） */
    Key_Scan(&key1);
    Key_Scan(&key2);
    if (key1.event) {                             /* KEY1：换向 */
        key1.event = 0;
        app.dir ^= 1;
        app.dirty = 1;
        snprintf(tx_buf, sizeof(tx_buf), "Direction: %s\r\n",
                 app.dir ? "Reverse" : "Forward");
        UART_SendStr(tx_buf);
    }
    if (key2.event) {                             /* KEY2：暂停/继续 */
        key2.event = 0;
        app.run ^= 1;
        if (app.run) last_shift = HAL_GetTick();  /* 恢复时重置计时，避免瞬间跳 */
        app.dirty = 1;
        snprintf(tx_buf, sizeof(tx_buf), "State: %s\r\n",
                 app.run ? "Running" : "Paused");
        UART_SendStr(tx_buf);
    }

    /* 2. 串口协议包（作业2，protocol.c 已完成帧头/校验判定） */
    if (Protocol_GetCmd(&cmd))
    {
        app.run     = cmd.run;
        app.dir     = cmd.dir;
        app.step_ms = cmd.step_ms;
        app.dirty   = 1;
        snprintf(tx_buf, sizeof(tx_buf), "ACK: RUN=%u DIR=%u STEP=%u\r\n",
                 app.run, app.dir, app.step_ms);
        UART_SendStr(tx_buf);
    }

    /* 3. 流水移位（作业1，间隔/方向受作业1+2 控制） */
    if (app.run && (HAL_GetTick() - last_shift >= (uint32_t)app.step_ms))
    {
        last_shift  = HAL_GetTick();
        app.led_idx = (uint8_t)((app.led_idx + (app.dir ? 2u : 1u)) % 3u); /* 正向+1 反向-1 */
        LEDs_Only(app.led_idx);
        app.dirty   = 1;                            /* 暂停时不进这里，满足要求2 */
    }

    /* 4. OLED 刷新（作业3） */
    if (app.dirty) OLED_Refresh();
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}


/* ===== 以下均为 CubeMX 生成区，已按你的 .ioc 配置重建，确保编译/链接完整 ===== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /*** Initialize the RCC Oscillators ***/
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;   /* F1 必须有这行 */
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;          /* 8MHz HSE ×9 = 72MHz */
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /*** Initialize the CPU, AHB and APB buses clocks ***/
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* 输出脚初始态：LED 全灭(高)，OLED 两线空闲(高) */
  HAL_GPIO_WritePin(GPIOA, LED_R_Pin | LED_G_Pin | LED_B_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, OLED_SCL_Pin | OLED_SDA_Pin, GPIO_PIN_SET);

  /* 按键：PA0 / PC13 输入，无上下拉 */
  GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Pin   = KEY1_Pin;
  HAL_GPIO_Init(KEY1_GPIO_Port, &GPIO_InitStruct);
  GPIO_InitStruct.Pin   = KEY2_Pin;
  HAL_GPIO_Init(KEY2_GPIO_Port, &GPIO_InitStruct);

  /* LED：PA1/PA2/PA3 推挽输出，低速 */
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Pin   = LED_R_Pin | LED_G_Pin | LED_B_Pin;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* OLED：PB8/PB9 开漏输出，高速（软件 I2C，靠外部上拉） */
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pin   = OLED_SCL_Pin | OLED_SDA_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif