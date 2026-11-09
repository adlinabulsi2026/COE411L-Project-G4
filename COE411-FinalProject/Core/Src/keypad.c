#include "keypad.h"

/* Rows PC0-PC3 are outputs, columns PC4-PC7 are inputs with pull-down */
#define KEYPAD_PORT GPIOC
#define ROW_ALL (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)
#define COL_ALL (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)

static const uint16_t ROW_PINS[4] = { GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3 };
static const uint16_t COL_PINS[4] = { GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7 };

static const char KEYMAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

/* Configure the keypad pins (does not rely on CubeMX) */
void Keypad_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(KEYPAD_PORT, ROW_ALL, GPIO_PIN_RESET);
    g.Pin   = ROW_ALL;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KEYPAD_PORT, &g);

    g.Pin  = COL_ALL;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(KEYPAD_PORT, &g);
}

/* Returns the key currently held down, or 0 if none.
 * No delays and no waiting: debouncing is done in keypadTask. */
char Keypad_Scan(void)
{
    char key = 0;

    for (int r = 0; r < 4 && key == 0; r++)
    {
        HAL_GPIO_WritePin(KEYPAD_PORT, ROW_ALL, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(KEYPAD_PORT, ROW_PINS[r], GPIO_PIN_SET);

        for (volatile int d = 0; d < 50; d++) { }   /* let the line settle */

        for (int c = 0; c < 4; c++)
        {
            if (HAL_GPIO_ReadPin(KEYPAD_PORT, COL_PINS[c]) == GPIO_PIN_SET)
            {
                key = KEYMAP[r][c];
                break;
            }
        }
    }

    HAL_GPIO_WritePin(KEYPAD_PORT, ROW_ALL, GPIO_PIN_RESET);
    return key;
}
