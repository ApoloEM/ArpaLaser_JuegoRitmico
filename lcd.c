/*
 * lcd.c — LCD 20 × 4 por I2C (ver lcd.h).
 *
 * El adaptador es un PCF8574: P0 = RS, P1 = R/W, P2 = E, P3 = luz, P4 a P7 = D4 a D7. La pantalla trabaja en 4 bits:
 * cada byte son dos mitades, y cada mitad se escribe dos veces (con E en alto y con E en bajo) para dar el pulso.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "lcd.h"
#include "twi.h"

#define RS 0x01
#define EN 0x04
#define LUZ 0x08

static uint8_t dir;                            /* dirección I2C; 0 = sin pantalla */
static char quiero[LCD_FILAS][LCD_COLS];       /* lo que el programa quiere ver */
static char hay[LCD_FILAS][LCD_COLS];          /* lo que ya se mandó */
static uint8_t cur_f = 0xFF, cur_c;            /* dónde quedó el cursor de la pantalla */
static uint8_t busca_f, busca_c;               /* por dónde va la búsqueda de cambios */

/* 7 barras (1 a 7 renglones de abajo hacia arriba) y la nota musical. */
static const uint8_t PROPIOS[8][8] PROGMEM = {
  {0, 0, 0, 0, 0, 0, 0, 0x1F}, {0, 0, 0, 0, 0, 0, 0x1F, 0x1F}, {0, 0, 0, 0, 0, 0x1F, 0x1F, 0x1F}, {0, 0, 0, 0, 0x1F, 0x1F, 0x1F, 0x1F},
  {0, 0, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, {0, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, {0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F},
  {0x04, 0x06, 0x05, 0x05, 0x04, 0x0C, 0x1C, 0x08},
};

static uint8_t byte_lcd(uint8_t b, uint8_t rs) {
  uint8_t t[4];
  uint8_t alto = (uint8_t)((b & 0xF0) | LUZ | rs), bajo = (uint8_t)(((b << 4) & 0xF0) | LUZ | rs);
  t[0] = alto | EN; t[1] = alto; t[2] = bajo | EN; t[3] = bajo;
  return twi_escribir(dir, t, 4);
}
static void mitad(uint8_t b) {                 /* sólo durante el arranque, cuando la pantalla aún está en 8 bits */
  uint8_t t[2];
  t[0] = (uint8_t)(b | LUZ | EN); t[1] = (uint8_t)(b | LUZ);
  twi_escribir(dir, t, 2);
}

uint8_t lcd_iniciar(void) {
  uint8_t f, c;
  twi_iniciar();
  dir = twi_existe(0x27) ? 0x27 : twi_existe(0x3F) ? 0x3F : 0;
  for (f = 0; f < LCD_FILAS; f++) for (c = 0; c < LCD_COLS; c++) { quiero[f][c] = ' '; hay[f][c] = ' '; }
  if (!dir) return 0;
  _delay_ms(50);
  mitad(0x30); _delay_ms(5);                   /* la secuencia de arranque del HD44780 para pasar a 4 bits */
  mitad(0x30); _delay_ms(1);
  mitad(0x30); _delay_ms(1);
  mitad(0x20); _delay_ms(1);
  byte_lcd(0x28, 0);                           /* 4 bits, 2 renglones (así se manejan las de 4), letra de 5 × 8 */
  byte_lcd(0x08, 0);                           /* apagada mientras se prepara */
  byte_lcd(0x01, 0); _delay_ms(3);             /* borrar */
  byte_lcd(0x06, 0);                           /* el cursor avanza a la derecha */
  byte_lcd(0x40, 0);                           /* los 8 caracteres propios */
  for (f = 0; f < 8; f++) for (c = 0; c < 8; c++) byte_lcd(pgm_read_byte(&PROPIOS[f][c]), RS);
  byte_lcd(0x0C, 0);                           /* encendida, sin cursor */
  cur_f = 0xFF;
  return dir;
}

uint8_t lcd_pendiente(void) {
  uint8_t f, c;
  if (!dir) return 0;
  for (f = 0; f < LCD_FILAS; f++) for (c = 0; c < LCD_COLS; c++) if (quiero[f][c] != hay[f][c]) return 1;
  return 0;
}

/* Manda un carácter cambiado, si lo hay. La búsqueda sigue donde se quedó, para repartir entre los 4 renglones. */
static uint8_t uno(void) {
  static const uint8_t INICIO[4] PROGMEM = {0x00, 0x40, 0x14, 0x54};
  uint8_t n;
  for (n = 0; n < LCD_FILAS * LCD_COLS; n++) {
    uint8_t f = busca_f, c = busca_c;
    if (++busca_c >= LCD_COLS) { busca_c = 0; if (++busca_f >= LCD_FILAS) busca_f = 0; }
    if (quiero[f][c] == hay[f][c]) continue;
    if (f != cur_f || c != cur_c) {            /* si el cursor ya está ahí, se ahorra la orden de moverlo */
      if (!byte_lcd((uint8_t)(0x80 | (pgm_read_byte(&INICIO[f]) + c)), 0)) return 0;
    }
    if (!byte_lcd((uint8_t)quiero[f][c], RS)) { cur_f = 0xFF; return 0; }
    hay[f][c] = quiero[f][c];
    cur_f = f;
    cur_c = (uint8_t)(c + 1);
    if (cur_c >= LCD_COLS) cur_f = 0xFF;
    return 1;
  }
  return 0;
}

void lcd_tarea(void) {
  if (!dir) return;
  if (uno()) uno();
}

void lcd_vaciar(void) {
  uint8_t n = LCD_FILAS * LCD_COLS;
  if (!dir) return;
  while (n-- && uno()) {}
}

void lcd_car(uint8_t fila, uint8_t col, char c) {
  if (fila < LCD_FILAS && col < LCD_COLS) quiero[fila][col] = c;
}
void lcd_texto(uint8_t fila, uint8_t col, const char *t) {
  while (*t) lcd_car(fila, col++, *t++);
}
void lcd_texto_P(uint8_t fila, uint8_t col, PGM_P t) {
  char c;
  while ((c = (char)pgm_read_byte(t++)) != 0) lcd_car(fila, col++, c);
}
void lcd_fila(uint8_t fila, const char *t) {
  uint8_t c;
  for (c = 0; c < LCD_COLS; c++) lcd_car(fila, c, *t ? *t++ : ' ');
}
void lcd_fila_P(uint8_t fila, PGM_P t) {
  uint8_t c;
  char x = 1;
  for (c = 0; c < LCD_COLS; c++) {
    if (x) x = (char)pgm_read_byte(t++);
    lcd_car(fila, c, x ? x : ' ');
  }
}
void lcd_numero(uint8_t fila, uint8_t col, uint16_t v, uint8_t ancho) {
  uint8_t k = ancho;
  while (k--) {
    lcd_car(fila, (uint8_t)(col + k), (v || k == ancho - 1) ? (char)('0' + v % 10) : ' ');
    v /= 10;
  }
}
void lcd_borrar(void) {
  uint8_t f, c;
  for (f = 0; f < LCD_FILAS; f++) for (c = 0; c < LCD_COLS; c++) quiero[f][c] = ' ';
}
char lcd_barra(uint8_t n) {
  if (n == 0) return ' ';
  if (n >= 8) return LCD_LLENO;
  return (char)(0x07 + n);                     /* caracteres propios 0 a 6, por sus códigos 8 a 14 */
}
