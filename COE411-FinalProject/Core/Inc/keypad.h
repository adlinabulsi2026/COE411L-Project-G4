#ifndef __KEYPAD_H__
#define __KEYPAD_H__

#include "stm32l4xx_hal.h"

/* Public function prototypes */
void Keypad_Init(void);
char Keypad_Scan(void);

#endif /* __KEYPAD_H__ */
