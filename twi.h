/*
 * twi.h — I2C (TWI) del ATmega328P como maestro, por sondeo: A4 = SDA, A5 = SCL, 100 kHz.
 * Lo usan la LCD y el teclado, los dos por un PCF8574. Sólo se llama desde el lazo principal.
 */
#ifndef TWI_H
#define TWI_H

#include <stdint.h>

void twi_iniciar(void);
uint8_t twi_escribir(uint8_t direccion, const uint8_t *datos, uint8_t n);   /* 1 = el chip contestó a todo */
uint8_t twi_leer(uint8_t direccion, uint8_t *dato);                         /* lee un byte; 1 = bien */
uint8_t twi_existe(uint8_t direccion);                                      /* 1 = hay un chip en esa dirección */

#endif
