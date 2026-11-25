/*
 * teclado.h — teclado matricial 4 × 4 leído por un módulo PCF8574 (I2C, dirección 0x20 a 0x27 o 0x38 a 0x3F).
 *
 * Los 8 pines del teclado van, en orden, a P0…P7 del módulo. El programa baja una de las líneas P0 a P3 a la vez
 * y mira cuál de P4 a P7 baja con ella: ese cruce es la tecla. Qué letra corresponde a cada cruce depende de cómo
 * esté armado el teclado, y se fija en la tabla TECLADO_MAPA de abajo: el programa de prueba de la etapa 2 dice
 * el número de cruce de cada tecla que aprietes.
 */
#ifndef TECLADO_H
#define TECLADO_H

#include <stdint.h>

/* Letra de cada cruce, del 0 al 15: cruce = 4 × (línea de P0..P3) + (línea de P4..P7).
 * Así viene si los pines 1 a 4 del teclado son los renglones (de arriba abajo) y los 5 a 8 las columnas.
 * Si en la prueba salen las teclas cambiadas, se reescribe esta cadena con lo que la prueba diga. */
#ifndef TECLADO_MAPA
#define TECLADO_MAPA "123A456B789C*0#D"
#endif

enum { TECLA_NADA, TECLA_PULSA, TECLA_CORTA, TECLA_LARGA };
#define TECLA_LARGA_MS 900

uint8_t teclado_iniciar(void);                 /* devuelve la dirección I2C que contestó, o 0 si no hay módulo */
void teclado_tarea(void);                      /* llamar en cada vuelta: revisa una línea cada 5 ms */
/* Devuelve qué pasó y con qué tecla:
 *   TECLA_PULSA  se acaba de apretar          TECLA_CORTA  se soltó antes de 0.9 s
 *   TECLA_LARGA  lleva 0.9 s apretada (al soltarla ya no avisa)                                */
uint8_t teclado_evento(char *tecla);
uint8_t teclado_cruce(void);                   /* cruce apretado en este momento (0..15) o 0xFF: para la prueba */

#endif
