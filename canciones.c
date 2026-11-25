/*
 * canciones.c — las 8 obras del arpa (los datos están en canciones_datos.c, generados por herramientas/generar_datos.py).
 *
 *   Sola:    el arpa toca la obra y prende sólo el láser de cada nota mientras suena. Los cortes no cuentan.
 *   Aprende: sólo está prendido el haz de la nota que sigue; al cortarlo suena y pasa a la siguiente. Si tardas más
 *            de 3 s, ese haz parpadea.
 * El tiempo se lleva en semicorcheas: una dura 15000 / tempo ms (tempo en negras por minuto), ajustado por * y #.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "arpa.h"
#include "lcd.h"
#include "canciones_datos.h"

uint8_t can_elegida, can_aprende;

static uint8_t activa, fin;
static const uint8_t *notas;                   /* la nota que sigue (3 bytes, en la flash) */
static uint16_t total, idx, pos16, cambio;
static uint8_t tempo, tempo2;
static uint32_t t_sig;                         /* Sola: cuándo toca la nota que sigue. Aprende: desde cuándo se espera el corte */
static uint32_t apaga[N_HACES];                /* Sola: cuándo se apaga el láser de cada haz (0 = ya está apagado) */
static uint16_t aciertos;

uint8_t can_activa(void) { return activa; }

static uint16_t ms16(void) {                   /* milisegundos de una semicorchea en este punto de la obra */
  uint8_t bpm = pos16 >= cambio ? tempo2 : tempo;
  return (uint16_t)(1500000UL / ((uint16_t)bpm * aj.tempo));
}

void can_empezar(uint8_t cual) {
  cancion_t c;
  uint8_t i;
  if (cual >= N_CANCIONES) return;
  memcpy_P(&c, &CANCIONES[cual], sizeof c);
  can_elegida = cual;
  notas = c.notas;
  total = c.n;
  tempo = c.tempo;
  tempo2 = c.tempo2;
  cambio = c.cambio;
  idx = 0;
  pos16 = 0;
  aciertos = 0;
  fin = 0;
  for (i = 0; i < N_HACES; i++) apaga[i] = 0;
  sonido_callar();
  luz = 0;
  activa = 1;
  t_sig = ahora() + (can_aprende ? 0 : 400);
  pos16 = pgm_read_byte(notas);                /* la primera nota puede no caer en el tiempo 0 */
  if (!can_aprende) t_sig += (uint32_t)pos16 * ms16();
}

void can_parar(void) {
  if (!activa) return;
  activa = 0;
  luz = 0xFF;
}

void can_tarea(void) {
  uint32_t t = ahora();
  uint8_t i;
  if (!activa) return;
  if (fin) {                                   /* terminó: un momento de calma y regresa al menú */
    if ((int32_t)(t - t_sig) >= 0) can_parar();
    return;
  }
  if (can_aprende) {
    uint8_t haz = pgm_read_byte(notas + 1) & 7;
    uint8_t bit = (uint8_t)(1 << haz);
    luz = (t - t_sig > 3000 && ((uint8_t)(t >> 8) & 1)) ? 0 : bit;     /* después de 3 s, parpadea */
    return;
  }
  /* Sola: toca todas las notas que ya debían sonar */
  while (idx < total && (int32_t)(t - t_sig) >= 0) {
    uint8_t dh = pgm_read_byte(notas + 1), midi = pgm_read_byte(notas + 2) & 0x7F;
    uint8_t haz = dh & 7;
    uint16_t visible = (uint16_t)(dh >> 3) * ms16();
    visible = visible > 180 ? visible - 60 : 120;      /* 60 ms a oscuras antes de la nota siguiente: así se ve una nota repetida */
    if (visible > 600) visible = 600;
    nota_de_cancion(haz, midi, 240, 70);       /* una «mano invisible» puntea y suelta */
    apaga[haz] = t + visible;
    notas += 3;
    idx++;
    if (idx < total) {
      uint8_t delta = pgm_read_byte(notas);
      pos16 += delta;
      t_sig += (uint32_t)delta * ms16();
    }
  }
  luz = 0;
  for (i = 0; i < N_HACES; i++) {
    if (!apaga[i]) continue;
    if ((int32_t)(t - apaga[i]) >= 0) apaga[i] = 0;
    else luz |= (uint8_t)(1 << i);
  }
  if (idx >= total && !luz) { fin = 1; t_sig = t + 1200; }
}

uint8_t can_corte(uint8_t haz) {
  if (!activa) return 0;                       /* sin obra en curso, el arpa se toca normal */
  if (!can_aprende || fin) return 1;
  if (haz != (pgm_read_byte(notas + 1) & 7)) return 1;
  nota_de_cancion(haz, pgm_read_byte(notas + 2) & 0x7F, 255, 120);
  aciertos++;
  notas += 3;
  idx++;
  t_sig = ahora();
  if (idx >= total) {
    fin = 1;
    t_sig += 2000;
    luz = 0xFF;
    mensaje_P(PSTR("Terminaste!"), PSTR(""), PSTR(""), PSTR(""), 2000);
    lcd_numero(1, 0, aciertos, 3);
    lcd_texto_P(1, 4, PSTR("notas"));
  }
  return 1;
}

void can_pantalla(void) {
  cancion_t c;
  memcpy_P(&c, &CANCIONES[can_elegida], sizeof c);
  lcd_fila(0, c.titulo);
  if (!activa) {
    lcd_fila_P(1, can_aprende ? PSTR("Modo: Aprende") : PSTR("Modo: Sola"));
    lcd_fila_P(2, PSTR("Elige 1 a 8"));
  } else {
    lcd_fila_P(1, can_aprende ? PSTR("Aprende") : PSTR("Sola"));
    lcd_numero(1, 8, idx, 3);
    lcd_car(1, 11, '/');
    lcd_numero(1, 12, total, 3);
    if (can_aprende && idx < total) {
      lcd_fila_P(2, PSTR("Corta el haz"));
      lcd_car(2, 13, (char)('1' + (pgm_read_byte(notas + 1) & 7)));
    } else {
      lcd_fila_P(2, PSTR("Tempo    %"));
      lcd_numero(2, 6, aj.tempo, 3);
    }
  }
  lcd_fila_P(3, ((uint8_t)(ahora() / 3000) & 1) ? PSTR("*# tempo   0 parar") : PSTR("1-8 obra  B sola/apr"));
}
