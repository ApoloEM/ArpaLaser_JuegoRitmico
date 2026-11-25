/*
 * lcd.h — pantalla LCD de 20 × 4 (HD44780) con adaptador I2C (PCF8574 en 0x27 o 0x3F).
 *
 * El programa escribe en una copia de la pantalla en RAM y lcd_tarea() manda, en cada vuelta del lazo, a lo más
 * dos caracteres que hayan cambiado. Así pintar la pantalla nunca detiene al arpa más de 1 ms.
 * La ROM de estas pantallas no trae acentos ni ñ: los textos van sin ellos.
 */
#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define LCD_COLS 20
#define LCD_FILAS 4
#define LCD_NOTA '\x0F'                        /* la nota musical (carácter propio 7) */
#define LCD_LLENO '\xFF'                       /* bloque lleno */
/* Barras: lcd_barra(n) da el carácter de una barra de n renglones, de n = 0 (espacio) a n = 8 (LCD_LLENO). */

uint8_t lcd_iniciar(void);                     /* devuelve la dirección I2C que contestó, o 0 si no hay pantalla */
void lcd_tarea(void);
uint8_t lcd_pendiente(void);                   /* 1 = todavía falta mandar algo */
void lcd_vaciar(void);                         /* manda todo lo pendiente (bloquea hasta 60 ms) */

void lcd_fila(uint8_t fila, const char *texto);        /* fila 0..3; rellena con espacios hasta 20 */
void lcd_fila_P(uint8_t fila, PGM_P texto);            /* lo mismo, con el texto en la flash */
void lcd_texto(uint8_t fila, uint8_t col, const char *texto);
void lcd_texto_P(uint8_t fila, uint8_t col, PGM_P texto);
void lcd_car(uint8_t fila, uint8_t col, char c);
void lcd_numero(uint8_t fila, uint8_t col, uint16_t v, uint8_t ancho);   /* alineado a la derecha en «ancho» lugares */
void lcd_borrar(void);
char lcd_barra(uint8_t n);

#endif
