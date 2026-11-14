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
#include <stdio.h>
#include "tim.h"
#include "lcd_i2c.h"
#include "keypad.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  char line1[17];
  char line2[17];
} LCD_Msg_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DISARMED 0
#define ARMED 1
#define ALARM 2
#define MOTION 0x100
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile uint8_t pirEnabled = 0;
uint8_t state = DISARMED;
uint8_t arming = 0;
uint32_t startTime = 0;
char code[5];
char stars[5];
uint8_t count = 0;
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
void SetColor(uint8_t red, uint8_t green, uint8_t blue);
void ShowLcd(const char *a, const char *b);
void ShowCode(void);
void StartAlarm(void);
void Disarm(void);
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
  for(;;)
  {
    osDelay(1);
  }
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
  uint32_t ev;
  uint32_t timeout;
  SetColor(0, 255, 0);
  ShowLcd("SYSTEM DISARMED", "Press A to arm");
  for(;;)
  {
    if (arming)
    {
      timeout = 100;
    }
    else
    {
      timeout = osWaitForever;
    }
    if (osMessageQueueGet(eventQueueHandle, &ev, NULL, timeout) == osOK)
    {
      if (ev == MOTION)
      {
        if (state == ARMED)
        {
          StartAlarm();
        }
      }
      else if (state == DISARMED)
      {
        if (ev == 'A')
        {
          state = ARMED;
          arming = 1;
          startTime = HAL_GetTick();
          count = 0;
          stars[0] = '\0';
          SetColor(0, 0, 255);
          ShowLcd("ARMING...", "Wait 10 seconds");
        }
      }
      else
      {
        if (ev == '#')
        {
          count = 0;
          stars[0] = '\0';
          ShowCode();
        }
        else if (ev >= '0' && ev <= '9')
        {
          code[count] = (char)ev;
          stars[count] = '*';
          count++;
          code[count] = '\0';
          stars[count] = '\0';
          if (count < 4)
          {
            ShowCode();
          }
          else if (strcmp(code, "1234") == 0)
          {
            Disarm();
          }
          else
          {
            count = 0;
            stars[0] = '\0';
            if (state == ALARM)
            {
              ShowLcd("!! INTRUDER !!", "Wrong code");
            }
            else if (arming)
            {
              ShowLcd("ARMING...", "Wrong code");
            }
            else
            {
              ShowLcd("SYSTEM ARMED", "Wrong code");
            }
          }
        }
      }
    }
    if (arming && HAL_GetTick() - startTime >= 10000)
    {
      arming = 0;
      osSemaphoreAcquire(pirSemHandle, 0);
      pirEnabled = 1;
      if (count == 0)
      {
        ShowLcd("SYSTEM ARMED", "Code to disarm");
      }
      else
      {
        ShowCode();
      }
      if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET)
      {
        StartAlarm();
      }
    }
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
  for(;;)
  {
    char key = Keypad_Scan();
    if (key)
    {
      uint32_t ev = key;
      osMessageQueuePut(eventQueueHandle, &ev, 0, osWaitForever);
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
  LCD_Msg_t m;
  LCD_Init();
  for(;;)
  {
    if (osMessageQueueGet(lcdQueueHandle, &m, NULL, osWaitForever) == osOK)
    {
      LCD_Clear();
      LCD_SetCursor(0, 0);
      LCD_Print(m.line1);
      LCD_SetCursor(1, 0);
      LCD_Print(m.line2);
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
  uint32_t ev = MOTION;
  for(;;)
  {
    if (osSemaphoreAcquire(pirSemHandle, osWaitForever) == osOK)
    {
      if (pirEnabled && HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET)
      {
        osMessageQueuePut(eventQueueHandle, &ev, 0, osWaitForever);
      }
    }
  }
/* USER CODE END pirTaskFunction */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void SetColor(uint8_t red, uint8_t green, uint8_t blue)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (red * arr) / 255);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (green * arr) / 255);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (blue * arr) / 255);
}

void ShowLcd(const char *a, const char *b)
{
  LCD_Msg_t m;
  strcpy(m.line1, a);
  strcpy(m.line2, b);
  osMessageQueuePut(lcdQueueHandle, &m, 0, 20);
}

void ShowCode(void)
{
  char line[17];
  sprintf(line, "Code: %s", stars);
  if (state == ALARM)
  {
    ShowLcd("!! INTRUDER !!", line);
  }
  else if (arming)
  {
    ShowLcd("ARMING...", line);
  }
  else
  {
    ShowLcd("SYSTEM ARMED", line);
  }
}

void StartAlarm(void)
{
  state = ALARM;
  arming = 0;
  pirEnabled = 0;
  count = 0;
  stars[0] = '\0';
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
  SetColor(255, 0, 0);
  ShowLcd("!! INTRUDER !!", "Enter code");
}

void Disarm(void)
{
  state = DISARMED;
  arming = 0;
  pirEnabled = 0;
  count = 0;
  stars[0] = '\0';
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
  SetColor(0, 255, 0);
  ShowLcd("SYSTEM DISARMED", "Press A to arm");
}
/* USER CODE END Application */
