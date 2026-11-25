/*
 * twi.c — I2C maestro por sondeo (ver twi.h). Cada paso espera a TWINT con un tope, para que una pantalla
 * desconectada no cuelgue el arpa: el audio sigue en su interrupción.
 */
#include <avr/io.h>
#include "twi.h"

#define TOPE 6000                              /* vueltas de espera: muy por encima de un byte a 100 kHz */

void twi_iniciar(void) {
  TWSR = 0;                                    /* preescala 1 */
  TWBR = 72;                                   /* 16 MHz / (16 + 2 · 72) = 100 kHz */
  TWCR = (1 << TWEN);
}

static uint8_t espera(void) {                  /* devuelve el estado (TWSR sin la preescala) o 0xFF si no llegó */
  uint16_t n = TOPE;
  while (!(TWCR & (1 << TWINT))) if (--n == 0) return 0xFF;
  return TWSR & 0xF8;
}

static void alto(void) {
  uint16_t n = TOPE;
  TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
  while ((TWCR & (1 << TWSTO)) && --n) {}
}

static uint8_t inicio(uint8_t direccion_rw) {  /* START + dirección; 1 = contestó */
  uint8_t e;
  TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
  e = espera();
  if (e != 0x08 && e != 0x10) return 0;
  TWDR = direccion_rw;
  TWCR = (1 << TWINT) | (1 << TWEN);
  e = espera();
  return e == 0x18 || e == 0x40;               /* SLA+W o SLA+R con ACK */
}

uint8_t twi_escribir(uint8_t direccion, const uint8_t *datos, uint8_t n) {
  uint8_t bien = inicio((uint8_t)(direccion << 1));
  while (bien && n--) {
    TWDR = *datos++;
    TWCR = (1 << TWINT) | (1 << TWEN);
    bien = espera() == 0x28;
  }
  alto();
  return bien;
}

uint8_t twi_leer(uint8_t direccion, uint8_t *dato) {
  uint8_t bien = inicio((uint8_t)((direccion << 1) | 1));
  if (bien) {
    TWCR = (1 << TWINT) | (1 << TWEN);         /* un byte, sin ACK: es el último */
    bien = espera() == 0x58;
    *dato = TWDR;
  }
  alto();
  return bien;
}

uint8_t twi_existe(uint8_t direccion) {
  uint8_t bien = inicio((uint8_t)(direccion << 1));
  alto();
  return bien;
}
