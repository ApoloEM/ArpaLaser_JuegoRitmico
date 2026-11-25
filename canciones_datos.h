/* canciones_datos.h: GENERADO por herramientas/generar_datos.py. No editar a mano. */
#ifndef CANCIONES_DATOS_H
#define CANCIONES_DATOS_H
#include <stdint.h>
#include <avr/pgmspace.h>

#define N_CANCIONES 8
typedef struct {
  char titulo[21];
  const uint8_t *notas;      /* 3 bytes por nota */
  uint16_t n;                /* cuántas notas */
  uint8_t tempo;             /* negras por minuto */
  uint16_t cambio;           /* semicorchea donde cambia el tempo (0xFFFF = nunca) */
  uint8_t tempo2;
} cancion_t;
extern const cancion_t CANCIONES[N_CANCIONES] PROGMEM;

#endif
