#include "lcd_i2c.h"
#include "i2c.h"
#include <string.h>

/* PCF8574 backpack bits: P0 = RS, P2 = EN, P3 = backlight, P4..P7 = D4..D7 */
#define LCD_RS 0x01U
#define LCD_EN 0x04U
#define LCD_BL 0x08U

extern I2C_HandleTypeDef hi2c1;

/* Send the upper 4 bits of 'nibble', pulsing EN high then low */
static void LCD_WriteNibble(uint8_t nibble, uint8_t mode)
{
    uint8_t d = (uint8_t)((nibble & 0xF0U) | mode | LCD_BL);
    uint8_t seq[2] = { (uint8_t)(d | LCD_EN), d };
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, seq, 2, 100);
}

/* Send a full byte as two nibbles. mode = 0 for a command, LCD_RS for a character */
static void LCD_Send(uint8_t data, uint8_t mode)
{
    LCD_WriteNibble(data & 0xF0U, mode);
    LCD_WriteNibble((uint8_t)(data << 4), mode);
}

/* Must be called from lcdTask only (it owns the I2C bus) */
void LCD_Init(void)
{
    /* HD44780 power-on sequence: three 0x3 nibbles with the required
     * waits, then 0x2 to switch to 4-bit mode */
    HAL_Delay(50);
    LCD_WriteNibble(0x30, 0); HAL_Delay(5);
    LCD_WriteNibble(0x30, 0); HAL_Delay(1);
    LCD_WriteNibble(0x30, 0); HAL_Delay(1);
    LCD_WriteNibble(0x20, 0); HAL_Delay(1);

    LCD_Command(0x28);   // 4-bit, 2 lines, 5x8 font
    LCD_Command(0x0C);   // display ON, cursor OFF
    LCD_Command(0x06);   // entry mode: cursor moves right
    LCD_Clear();
}

void LCD_Command(uint8_t cmd)
{
    LCD_Send(cmd, 0);
}

void LCD_PrintChar(char data)
{
    LCD_Send((uint8_t)data, LCD_RS);
}

void LCD_Print(const char *str)
{
    while (*str) LCD_PrintChar(*str++);
}

void LCD_Clear(void)
{
    LCD_Command(0x01);
    HAL_Delay(2);        // clear needs ~1.5 ms
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t address = (uint8_t)((row == 0 ? 0x80 : 0xC0) + col);
    LCD_Command(address);
}

/* Write a whole row, padded with spaces to 16 characters,
 * so text from the previous message is always overwritten */
void LCD_PrintLine(uint8_t row, const char *text)
{
    LCD_SetCursor(row, 0);
    for (uint8_t i = 0; i < 16; i++)
    {
        LCD_PrintChar((*text != '\0') ? *text++ : ' ');
    }
}
