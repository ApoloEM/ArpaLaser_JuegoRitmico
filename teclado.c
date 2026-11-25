/*
 * teclado.c — teclado 4 × 4 por PCF8574 (ver teclado.h).
 *
 * El PCF8574 no tiene registro de dirección: una pata con un 1 escrito es una entrada con una resistencia débil a
 * 5 V, y con un 0 es una salida a tierra. Para leer una línea se escribe 0 sólo en ella y se lee el byte completo.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "teclado.h"
#include "twi.h"
#include "latido.h"

static const char MAPA[] PROGMEM = TECLADO_MAPA;
static uint8_t dir;
static uint8_t linea;                          /* línea P0..P3 que toca revisar */
static uint8_t visto = 0xFF;                   /* cruce visto en la vuelta que va */
static uint8_t estable = 0xFF, candidato = 0xFF, repite;
static uint8_t t_linea;
static uint16_t t_pulsa;
static uint8_t larga_avisada;
static uint8_t ev_tipo;
static char ev_tecla;

uint8_t teclado_iniciar(void) {
  uint8_t d, uno = 0xFF;
  dir = 0;
  for (d = 0x20; d <= 0x26 && !dir; d++) if (twi_existe(d)) dir = d;         /* 0x27 es la LCD */
  for (d = 0x38; d <= 0x3E && !dir; d++) if (twi_existe(d)) dir = d;         /* PCF8574A */
  if (dir) twi_escribir(dir, &uno, 1);
  return dir;
}

uint8_t teclado_cruce(void) { return estable; }

void teclado_tarea(void) {
  uint8_t ms = (uint8_t)ahora(), sal, ent, k;
  if (!dir || (uint8_t)(ms - t_linea) < 5) return;
  t_linea = ms;

  sal = (uint8_t)~(1 << linea);                /* un 0 en la línea que se revisa */
  if (twi_escribir(dir, &sal, 1) && twi_leer(dir, &ent)) {
    for (k = 0; k < 4; k++)
      if (!(ent & (0x10 << k))) visto = (uint8_t)(linea * 4 + k);
  }
  if (++linea < 4) return;
  linea = 0;

  /* Terminó una vuelta completa (20 ms). Una tecla vale cuando se ve igual dos vueltas seguidas. */
  if (visto == candidato) { if (repite < 2) repite++; } else { candidato = visto; repite = 0; }
  visto = 0xFF;
  if (repite >= 1 && candidato != estable) {
    uint16_t t = (uint16_t)ahora();
    if (estable != 0xFF && !larga_avisada) {   /* se soltó (o cambió) antes de 0.9 s */
      ev_tipo = TECLA_CORTA;
      ev_tecla = (char)pgm_read_byte(&MAPA[estable]);
    }
    estable = candidato;
    if (estable != 0xFF) {
      if (ev_tipo == TECLA_NADA) { ev_tipo = TECLA_PULSA; ev_tecla = (char)pgm_read_byte(&MAPA[estable]); }
      t_pulsa = t;
      larga_avisada = 0;
    }
  } else if (estable != 0xFF && !larga_avisada && (uint16_t)((uint16_t)ahora() - t_pulsa) >= TECLA_LARGA_MS) {
    larga_avisada = 1;
    ev_tipo = TECLA_LARGA;
    ev_tecla = (char)pgm_read_byte(&MAPA[estable]);
  }
}

uint8_t teclado_evento(char *tecla) {
  uint8_t t = ev_tipo;
  if (t == TECLA_NADA) return TECLA_NADA;
  *tecla = ev_tecla;
  ev_tipo = TECLA_NADA;
  return t;
}
