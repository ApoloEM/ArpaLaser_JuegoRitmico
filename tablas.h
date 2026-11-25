/* tablas.h: GENERADO por herramientas/generar_datos.py. No editar a mano.
 * Tablas de onda de 256 muestras de 8 bits y los números de la síntesis, todo en la flash. */
#ifndef TABLAS_H
#define TABLAS_H
#include <stdint.h>
#include <avr/pgmspace.h>

#define FS_HZ 16000u
#define N_TABLAS 30
#define TABLAS_POR_SONIDO 9         /* registro 0: 4 tablas, registro 1: 3, registro 2: 2 */
#define T_SENO 27
#define T_RUIDO 28
#define T_AGUDO 29
#define MIDI_MIN 36
#define MIDI_MAX 108
#define MIDI_REG1 75      /* desde esta nota (600 Hz), tablas con 6 armónicos */
#define MIDI_REG2 87      /* desde 1200 Hz, con 3 */
#define MIDI_REG3 99      /* desde 2400 Hz, un seno */
#define TICK_MS 2                 /* las envolventes avanzan cada 2 ms */
#define FACTOR_MANO 65209u       /* por tick: tau = 0.4 s, con la mano en el haz */
#define FACTOR_PEDAL 65503u      /* tau = 4 s, con el botón de sostener */
#define FACTOR_RAPIDO 46959u     /* tau = 6 ms, para callar sin chasquido */
#define FACTOR_BARRIDO 62340u    /* tau = 40 ms, la caída del tono de bombo y toms */

typedef struct { uint8_t tabla; uint16_t inc, inc_extra, factor; uint8_t fuerza; } tambor_t;

extern const int8_t ONDAS[N_TABLAS][256] PROGMEM;
extern const uint16_t INC_NOTA[MIDI_MAX - MIDI_MIN + 1] PROGMEM;   /* incremento de fase de cada nota MIDI: 65536 f / 16000 */
extern const uint16_t FACTOR_SUELTA[32] PROGMEM;                    /* perilla de decaimiento: tau de 0.3 a 3 s */
extern const uint8_t PAN_I[8] PROGMEM, PAN_D[8] PROGMEM;            /* cuánto de cada haz va a la bocina izquierda y a la derecha */
extern const uint8_t CAMBIO_TABLA[3][3] PROGMEM;                    /* en qué tick pasa cada sonido a su tabla siguiente */
extern const tambor_t TAMBORES[8] PROGMEM;

#endif
