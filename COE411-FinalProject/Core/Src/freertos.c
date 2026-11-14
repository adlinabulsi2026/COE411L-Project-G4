/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "tim.h"
#include "lcd_i2c.h"   /* lab LCD driver, unchanged */
#include "keypad.h"    /* lab keypad driver, unchanged */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  char line1[17];
  char line2[17];
} LCD_Msg_t;

typedef enum { STATE_DISARMED, STATE_ARMED, STATE_ALARM } SystemState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SECURITY_CODE   "1234"
#define CODE_LEN        4U
#define EXIT_DELAY_MS   10000U
#define EVT_MOTION      0x100U   /* keys are 0x00-0xFF, so this cannot clash */
#define LED_ON          999U     /* full duty (TIM1 period = 999) */

/* PIR on PA6 (D12) with EXTI6, both edges, pull-up.
 * PIR_ACTIVE is the pin level that means "motion":
 *   GPIO_PIN_SET   -> output goes HIGH on motion (HC-SR501 type)
 *   GPIO_PIN_RESET -> output goes LOW on motion (AL / active-low type)
 * If the system beeps right after arming, or stays on "Stay still", this is wrong. */
#define PIR_PORT        GPIOA
#define PIR_PIN         GPIO_PIN_6
#define PIR_ACTIVE      GPIO_PIN_RESET
#define PIR_CONFIRM_MS  200U     /* motion must last this long to count */

/* 1 = LD2 (PA5, set as GPIO output in CubeMX) copies the PIR pin level for testing.
 * Set to 0 when the PIR works. */
#define PIR_DEBUG       1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* Set by stateTask. pirTask only reports motion when this is 1
 * (armed, exit delay finished, alarm not active yet). */
static volatile uint8_t pirEnabled = 0;

/* State machine data (owned by stateTask) */
static SystemState_t state;
static char          entry[CODE_LEN + 1];
static uint8_t       entryLen;
static uint8_t       exitDelayActive;
static uint32_t      armTick;
static uint32_t      exitTicks;
static uint32_t      tickFreq;
static uint32_t      lastShownSecs;
/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for stateTask */
osThreadId_t stateTaskHandle;
const osThreadAttr_t stateTask_attributes = {
  .name = "stateTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for keypadTask */
osThreadId_t keypadTaskHandle;
const osThreadAttr_t keypadTask_attributes = {
  .name = "keypadTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for lcdTask */
osThreadId_t lcdTaskHandle;
const osThreadAttr_t lcdTask_attributes = {
  .name = "lcdTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for pirTask */
osThreadId_t pirTaskHandle;
const osThreadAttr_t pirTask_attributes = {
  .name = "pirTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for eventQueue */
osMessageQueueId_t eventQueueHandle;
const osMessageQueueAttr_t eventQueue_attributes = {
  .name = "eventQueue"
};
/* Definitions for lcdQueue */
osMessageQueueId_t lcdQueueHandle;
const osMessageQueueAttr_t lcdQueue_attributes = {
  .name = "lcdQueue"
};
/* Definitions for pirSem */
osSemaphoreId_t pirSemHandle;
const osSemaphoreAttr_t pirSem_attributes = {
  .name = "pirSem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void SetLed(uint16_t r, uint16_t g, uint16_t b);
static void SetBuzzer(uint8_t on);
static void ShowLcd(const char *l1, const char *l2);
static void LcdWriteRow(uint8_t row, const char *text);
static void ShowCodeEntry(const char *title);
static void ShowExitCountdown(uint32_t secs);
static void ClearEntry(void);
static void EnterDisarmed(void);
static void EnterArmed(void);
static void EnterAlarm(void);
static void HandleKey(char key);
static void HandleMotion(void);
static void CheckExitDelay(void);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void stateTaskFunction(void *argument);
void keypadTaskFunction(void *argument);
void lcdTaskFunction(void *argument);
void pirTaskFunction(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of pirSem */
  pirSemHandle = osSemaphoreNew(1, 1, &pirSem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of eventQueue */
  eventQueueHandle = osMessageQueueNew (10, sizeof(uint32_t), &eventQueue_attributes);

  /* creation of lcdQueue */
  lcdQueueHandle = osMessageQueueNew (5, 34, &lcdQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of stateTask */
  stateTaskHandle = osThreadNew(stateTaskFunction, NULL, &stateTask_attributes);

  /* creation of keypadTask */
  keypadTaskHandle = osThreadNew(keypadTaskFunction, NULL, &keypadTask_attributes);

  /* creation of lcdTask */
  lcdTaskHandle = osThreadNew(lcdTaskFunction, NULL, &lcdTask_attributes);

  /* creation of pirTask */
  pirTaskHandle = osThreadNew(pirTaskFunction, NULL, &pirTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* If any object is NULL the FreeRTOS heap is too small:
     increase TOTAL_HEAP_SIZE in CubeMX (FreeRTOS > Config parameters). */
  if (eventQueueHandle == NULL || lcdQueueHandle == NULL || pirSemHandle == NULL ||
      stateTaskHandle == NULL || keypadTaskHandle == NULL ||
      lcdTaskHandle == NULL || pirTaskHandle == NULL)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;
  osThreadExit();   /* not used */
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_stateTaskFunction */
/**
* @brief Function implementing the stateTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_stateTaskFunction */
void stateTaskFunction(void *argument)
{
  /* USER CODE BEGIN stateTaskFunction */
  (void)argument;
  uint32_t ev;

  tickFreq  = osKernelGetTickFreq();
  exitTicks = (EXIT_DELAY_MS * tickFreq) / 1000U;

  EnterDisarmed();

  /* Infinite loop */
  for(;;)
  {
#if PIR_DEBUG
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN));
#endif
    /* Wake on an event, or every 200 ms to update the exit countdown */
    if (osMessageQueueGet(eventQueueHandle, &ev, NULL, 200U) == osOK)
    {
      if (ev == EVT_MOTION) HandleMotion();
      else                  HandleKey((char)ev);
    }
    CheckExitDelay();
  }
  /* USER CODE END stateTaskFunction */
}

/* USER CODE BEGIN Header_keypadTaskFunction */
/**
* @brief Function implementing the keypadTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_keypadTaskFunction */
void keypadTaskFunction(void *argument)
{
  /* USER CODE BEGIN keypadTaskFunction */
  (void)argument;

  /* Infinite loop */
  for(;;)
  {
    /* Lab Keypad_Scan debounces and waits for release, so it returns
     * one key per press (or 0 if nothing is pressed). */
    char k = Keypad_Scan();
    if (k != 0)
    {
      uint32_t ev = (uint32_t)(uint8_t)k;
      osMessageQueuePut(eventQueueHandle, &ev, 0U, 0U);
    }
    osDelay(20);
  }
  /* USER CODE END keypadTaskFunction */
}

/* USER CODE BEGIN Header_lcdTaskFunction */
/**
* @brief Function implementing the lcdTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_lcdTaskFunction */
void lcdTaskFunction(void *argument)
{
  /* USER CODE BEGIN lcdTaskFunction */
  (void)argument;
  LCD_Msg_t m;

  LCD_Init();   /* lcdTask is the only task that uses I2C */

  /* Infinite loop */
  for(;;)
  {
    if (osMessageQueueGet(lcdQueueHandle, &m, NULL, osWaitForever) == osOK)
    {
      LcdWriteRow(0, m.line1);
      LcdWriteRow(1, m.line2);
    }
  }
  /* USER CODE END lcdTaskFunction */
}

/* USER CODE BEGIN Header_pirTaskFunction */
/**
* @brief Function implementing the pirTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_pirTaskFunction */
void pirTaskFunction(void *argument)
{
  /* USER CODE BEGIN pirTaskFunction */
  (void)argument;
  uint32_t ev = EVT_MOTION;

  /* The semaphore starts with one token. Take it so we wait for a real interrupt. */
  osSemaphoreAcquire(pirSemHandle, 0U);

  /* Infinite loop */
  for(;;)
  {
    /* Sleep until the EXTI6 interrupt (HAL_GPIO_EXTI_Callback in main.c) gives pirSem */
    if (osSemaphoreAcquire(pirSemHandle, osWaitForever) != osOK) continue;

    /* Ignore edges while disarmed, during the exit delay, or when the pin
     * is back at its idle level (that edge is just the motion ending). */
    if (!pirEnabled) continue;
    if (HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) != PIR_ACTIVE) continue;

    /* Confirm: real motion keeps the output active for a while,
     * a glitch is gone almost at once. */
    osDelay(PIR_CONFIRM_MS);
    if (pirEnabled && HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) == PIR_ACTIVE)
    {
      osMessageQueuePut(eventQueueHandle, &ev, 0U, 0U);
    }
  }
  /* USER CODE END pirTaskFunction */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ======================= Output helpers ========================== */
static void SetLed(uint16_t r, uint16_t g, uint16_t b)
{
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, r);   /* PA8  red   */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, g);   /* PA9  green */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, b);   /* PA10 blue  */
}

static void SetBuzzer(uint8_t on)
{
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Queue two lines for lcdTask. stateTask never touches I2C itself. */
static void ShowLcd(const char *l1, const char *l2)
{
  LCD_Msg_t m;
  strncpy(m.line1, l1, 16); m.line1[16] = '\0';
  strncpy(m.line2, l2, 16); m.line2[16] = '\0';
  osMessageQueuePut(lcdQueueHandle, &m, 0U, 20U);
}

/* Used by lcdTask only. Writes 16 characters (padded with spaces) with the
 * lab LCD functions, so old text is overwritten without a clear. */
static void LcdWriteRow(uint8_t row, const char *text)
{
  char buf[17];
  uint8_t i = 0;
  while (i < 16 && text[i] != '\0') { buf[i] = text[i]; i++; }
  while (i < 16) buf[i++] = ' ';
  buf[16] = '\0';
  LCD_SetCursor(row, 0);
  LCD_Print(buf);
}

/* Line 2 shows one '*' per digit typed, e.g. "Code: ***" */
static void ShowCodeEntry(const char *title)
{
  char l2[17] = "Code: ";
  for (uint8_t i = 0; i < entryLen; i++) l2[6 + i] = '*';
  l2[6 + entryLen] = '\0';
  ShowLcd(title, l2);
}

static void ShowExitCountdown(uint32_t secs)
{
  char l2[17] = "Leave in ";
  uint8_t p = 9;
  if (secs >= 10U) l2[p++] = (char)('0' + secs / 10U);
  l2[p++] = (char)('0' + secs % 10U);
  l2[p++] = 's';
  l2[p]   = '\0';
  ShowLcd("ARMING...", l2);
}

static void ClearEntry(void)
{
  entryLen = 0;
  entry[0] = '\0';
}

/* ======================= State transitions ======================= */
static void EnterDisarmed(void)
{
  state = STATE_DISARMED;
  pirEnabled = 0;
  exitDelayActive = 0;
  ClearEntry();
  SetBuzzer(0);
  SetLed(0, LED_ON, 0);                    /* green */
  ShowLcd("SYSTEM DISARMED", "Press A to arm");
}

static void EnterArmed(void)
{
  state = STATE_ARMED;
  pirEnabled = 0;                          /* ignore motion during exit delay */
  exitDelayActive = 1;
  armTick = osKernelGetTickCount();
  lastShownSecs = EXIT_DELAY_MS / 1000U;
  ClearEntry();
  SetBuzzer(0);
  SetLed(0, 0, LED_ON);                    /* blue */
  ShowExitCountdown(lastShownSecs);
}

static void EnterAlarm(void)
{
  state = STATE_ALARM;
  pirEnabled = 0;                          /* stop further motion events */
  ClearEntry();
  SetBuzzer(1);                            /* buzzer first: time-critical */
  SetLed(LED_ON, 0, 0);                    /* red */
  ShowLcd("!! INTRUDER !!", "Enter code");
}

/* ========================= Event handling ======================== */
static void HandleKey(char key)
{
  if (state == STATE_DISARMED)
  {
    if (key == 'A') EnterArmed();
    return;                                /* other keys ignored */
  }

  /* Armed or Alarm: digits build the code, # clears it */
  const char *title = (state == STATE_ALARM) ? "!! INTRUDER !!"
                    : (exitDelayActive ? "ARMING..." : "SYSTEM ARMED");

  if (key == '#')
  {
    ClearEntry();
    ShowCodeEntry(title);
    return;
  }
  if (key < '0' || key > '9') return;      /* ignore A-D and * */

  entry[entryLen++] = key;
  entry[entryLen] = '\0';

  if (entryLen < CODE_LEN)
  {
    ShowCodeEntry(title);
    return;
  }

  /* 4 digits entered: check the code */
  if (strcmp(entry, SECURITY_CODE) == 0)
  {
    EnterDisarmed();
  }
  else
  {
    ClearEntry();
    ShowLcd(title, "Wrong code");
  }
}

static void HandleMotion(void)
{
  if (state == STATE_ARMED && !exitDelayActive)
  {
    EnterAlarm();
  }
}

/* Called regularly by stateTask: runs the countdown and ends the exit delay. */
static void CheckExitDelay(void)
{
  if (state != STATE_ARMED || !exitDelayActive) return;

  uint32_t elapsed = osKernelGetTickCount() - armTick;   /* wrap-safe */

  if (elapsed >= exitTicks)
  {
    osSemaphoreAcquire(pirSemHandle, 0U);  /* drop any old interrupt */
    exitDelayActive = 0;
    pirEnabled = 1;                        /* now motion can trigger the alarm */

    /* Motion is already there when the system gets armed: alarm right away */
    if (HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) == PIR_ACTIVE)
    {
      EnterAlarm();
      return;
    }
    if (entryLen == 0) ShowLcd("SYSTEM ARMED", "Code to disarm");
  }
  else if (entryLen == 0)                  /* do not overwrite the stars */
  {
    uint32_t left = (exitTicks - elapsed + tickFreq - 1U) / tickFreq;
    if (left != lastShownSecs)
    {
      lastShownSecs = left;
      ShowExitCountdown(left);
    }
  }
}

/* USER CODE END Application */
