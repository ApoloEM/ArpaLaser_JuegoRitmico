/*
 * latido.h — el «latido» del arpa: la interrupción del Timer1 a 16 kHz que hace, en cada muestra,
 *   1) mandar por SPI la muestra de audio a los dos MCP4921 (bocina izquierda y derecha),
 *   2) leer el ADC y arrancar la conversión siguiente (8 sensores por el CD4051, 2 perillas y la fuente),
 *   3) prender y apagar los láseres por el 74HC595 y decidir qué haz está cortado,
 *   4) sumar las 8 voces (una por haz) para la muestra siguiente.
 * Aquí están también el reloj de 1 ms (Timer0), los botones de sostener (INT0) y la neblina.
 *
 * Pines del ATmega328P (Arduino UNO):
 *   D2  ← botones de sostener (a GND, resistencia interna)    D3  → /OE del 74HC595 (10 kΩ a 5 V)
 *   D4, D5, D6 → A, B, C del CD4051                            D7  → nebulizador y ventilador (2.º ULN2803A)
 *   D8  → RCLK del 74HC595 (10 kΩ a GND)                       D9, D10 → CS de los MCP4921 izquierdo y derecho
 *   D11 → SER del 74HC595 y SDI de los MCP4921                 D13 → SRCLK del 74HC595 y SCK de los MCP4921
 *   A0  ← señal de los 8 sensores (COM del CD4051)             A1, A2 ← perillas de volumen y de decaimiento
 *   A3  ← mitad del «5 V de potencia» (dos resistencias de 10 kΩ)
 *
 * Numeración: en todo el programa el haz 0 es el de la IZQUIERDA de quien toca y el 7 el de la derecha (en la
 * regleta, los láseres 1 a 8). El alambrado usa el orden contrario (haz n → salida Q(8 − n) del 74HC595 y canal
 * Y(8 − n) del CD4051, como quedó en el banco de la etapa 1); la conversión se hace aquí adentro y en ningún otro lado.
 *
 * Compilar SIEMPRE con -O2: con -O1 (el «Debug» de Microchip Studio) la interrupción no cabe en una muestra.
 */
#ifndef LATIDO_H
#define LATIDO_H

#include <stdint.h>

#define N_HACES 8

/* Una voz por haz. La interrupción la lee en cada muestra; el lazo principal la cambia (ver sonido.c). */
typedef struct {
  uint16_t fase, inc;        /* acumulador de fase de 16 bits y su incremento: f = inc · 16000 / 65536 */
  const int8_t *tabla;       /* tabla de onda de 256 muestras, en la flash */
  uint8_t gi, gd;            /* volumen hacia la bocina izquierda y hacia la derecha (0 = voz apagada) */
} voz_t;
extern volatile voz_t voz[N_HACES];

enum { LASER_NORMAL, LASER_FIJO, LASER_APAGADO };   /* parpadeando y detectando · fijos para apuntar · apagados */

void latido_iniciar(void);                 /* puertos, SPI, ADC, Timer0, Timer1 e INT0; falta sei() */
uint32_t ahora(void);                      /* milisegundos desde que arrancó */

/* ---- láseres y sensores ---- */
void haces_modo(uint8_t modo);
uint8_t haces_modo_actual(void);
void haces_prender(uint8_t mascara);       /* qué haces quedan prendidos: bit 0 = haz de la izquierda */
uint8_t haces_prendidos(void);
uint8_t haces_cortados(void);              /* un bit por haz cortado en este momento */
uint8_t haces_eventos(uint8_t *cortes, uint8_t *libres);   /* cortes y liberaciones desde la última llamada; devuelve los cortados */
void haces_olvidar(uint8_t mascara);       /* borra el estado de esos haces (al apagarlos a propósito) */
uint8_t haces_calibrar(void);              /* sin manos, ≈0.15 s; devuelve un bit por cada haz que SÍ ve su láser */
uint8_t haces_rescatar(void);              /* da referencia a los haces que no la tenían y ya ven su láser */
void haces_esperar(uint8_t barridos);      /* espera barridos completos (2 ms cada uno) */
void haces_lectura(uint8_t haz, uint8_t *con, uint8_t *sin, uint8_t *dif, uint8_t *ref);
void haces_emergencia(uint8_t apagar);     /* 1: sube /OE y los 8 láseres se apagan en ese instante */

/* ---- perillas, fuente, botones y neblina ---- */
uint8_t perilla_volumen(void);             /* 0..255 */
uint8_t perilla_decaimiento(void);         /* 0..255 */
uint8_t fuente_potencia(void);             /* 0..255: 148 ≈ 5.8 V en el riel de potencia; menos de 100, la fuente no está */
uint8_t sostener(void);                    /* 1 mientras un botón de sostener está apretado (sin rebotes) */
void neblina(uint8_t prender);
uint8_t neblina_prendida(void);

#endif
