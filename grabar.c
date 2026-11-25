/*
 * grabar.c — grabar lo que se toca y repetirlo en ciclo para tocar encima.
 *
 * Se graban HACES, no notas: cada evento son 2 bytes (cuánto pasó desde el anterior, en unidades de 8 ms, y qué haz
 * se cortó o se liberó). La cabeza guarda el sonido, la escala, la tonalidad y la octava con que se grabó, así la
 * repetición suena igual aunque después cambies los ajustes para tocar encima. Caben 200 eventos (unas 100 notas),
 * y la grabación se guarda en la EEPROM: sigue ahí al volver a encender.
 *
 *   1 grabar o terminar · 2 repetir o parar · 3 borrar (# confirma) · * # octava · 0 parar todo
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "arpa.h"
#include "lcd.h"

enum { G_QUIETO, G_GRABANDO, G_REPITIENDO };
static uint8_t estado, confirmar;
static uint32_t t_inicio, t_ultimo;            /* grabando: cuándo empezó y cuándo fue el último evento */
static uint32_t t_ciclo, t_evento;             /* repitiendo: cuándo empezó la vuelta y cuándo toca el evento que sigue */
static uint8_t idx;
static uint8_t fantasma;                       /* haces que la repetición tiene «cortados» */

static void anotar(uint8_t dato) {
  uint32_t t = ahora();
  uint16_t u = (uint16_t)((t - t_ultimo) >> 3);
  while (u > 255 && gra_cab.n < GRA_MAX) {     /* una pausa larga se parte en esperas de 2 s */
    gra_ev[gra_cab.n][0] = 255;
    gra_ev[gra_cab.n][1] = 0xFF;
    gra_cab.n++;
    u -= 255;
  }
  if (gra_cab.n >= GRA_MAX) return;
  gra_ev[gra_cab.n][0] = (uint8_t)u;
  gra_ev[gra_cab.n][1] = dato;
  gra_cab.n++;
  t_ultimo = t;
}

static void repetir(void) {
  if (!gra_cab.n) return;
  estado = G_REPITIENDO;
  idx = 0;
  fantasma = 0;
  t_ciclo = ahora();
  t_evento = t_ciclo + ((uint32_t)gra_ev[0][0] << 3);
}

static void dejar_de_repetir(void) {
  uint8_t i;
  for (i = 0; i < N_HACES; i++) if (fantasma & (1 << i)) nota_soltar(i);
  fantasma = 0;
  if (estado == G_REPITIENDO) estado = G_QUIETO;
}

static void terminar(void) {
  uint8_t i, notas = 0, cortados = haces_cortados();
  for (i = 0; i < N_HACES; i++) if (cortados & (1 << i)) anotar(i);      /* lo que seguía cortado, se libera */
  gra_cab.dur = (uint16_t)((ahora() - t_inicio) >> 3);
  for (i = 0; i < gra_cab.n; i++) if (gra_ev[i][1] != 0xFF && (gra_ev[i][1] & 8)) notas = 1;
  estado = G_QUIETO;
  if (!notas) gra_cab.n = 0;                   /* no se tocó nada: no hay grabación */
  mem_guardar(MEM_GRABACION);
  repetir();
}

void gra_evento(uint8_t haz, uint8_t corte) {
  if (estado != G_GRABANDO) return;
  anotar((uint8_t)(haz | (corte ? 8 : 0)));
  if (gra_cab.n >= GRA_MAX) terminar();        /* memoria llena */
}

void gra_parar(void) {
  if (estado == G_GRABANDO) terminar();
  dejar_de_repetir();
  confirmar = 0;
}

void gra_tecla(char k) {
  if (confirmar) {                             /* se pidió borrar: # lo confirma, cualquier otra lo cancela */
    confirmar = 0;
    if (k == '#') { dejar_de_repetir(); gra_cab.n = 0; mem_guardar(MEM_GRABACION); }
    return;
  }
  switch (k) {
    case '1':
      if (estado == G_GRABANDO) { terminar(); break; }
      dejar_de_repetir();
      gra_cab.aj = aj;
      gra_cab.n = 0;
      estado = G_GRABANDO;
      t_inicio = t_ultimo = ahora();
      break;
    case '2': if (estado == G_REPITIENDO) dejar_de_repetir(); else if (estado == G_QUIETO) repetir(); break;
    case '3': if (gra_cab.n && estado != G_GRABANDO) confirmar = 1; break;
    case '*': if (aj.octava > 4) { aj.octava--; guardar_ajustes(); } break;
    case '#': if (aj.octava < 6) { aj.octava++; guardar_ajustes(); } break;
    case '0': gra_parar(); sonido_callar(); break;
    default: break;
  }
}

void gra_tarea(void) {
  uint32_t t = ahora();
  if (estado != G_REPITIENDO) return;
  while (idx < gra_cab.n && (int32_t)(t - t_evento) >= 0) {
    uint8_t d = gra_ev[idx][1];
    if (d != 0xFF) {
      uint8_t haz = d & 7, bit = (uint8_t)(1 << haz);
      if (d & 8) { golpe_con(haz, &gra_cab.aj); fantasma |= bit; }
      else { fantasma &= (uint8_t)~bit; if (!(haces_cortados() & bit)) nota_soltar(haz); }
    }
    if (++idx < gra_cab.n) t_evento += (uint32_t)gra_ev[idx][0] << 3;
  }
  if (t - t_ciclo >= ((uint32_t)gra_cab.dur << 3)) {      /* terminó la vuelta: empieza otra, sin perder el compás */
    t_ciclo += (uint32_t)gra_cab.dur << 3;
    idx = 0;
    t_evento = t_ciclo + ((uint32_t)gra_ev[0][0] << 3);
  }
}

static void reloj(uint8_t fila, uint8_t col, uint32_t ms) {     /* m:ss */
  uint16_t s = (uint16_t)(ms / 1000);
  lcd_car(fila, col, (char)('0' + (s / 60) % 10));
  lcd_car(fila, (uint8_t)(col + 1), ':');
  lcd_car(fila, (uint8_t)(col + 2), (char)('0' + (s % 60) / 10));
  lcd_car(fila, (uint8_t)(col + 3), (char)('0' + s % 10));
}

void gra_pantalla(void) {
  lcd_fila_P(0, PSTR("Grabar y repetir"));
  if (confirmar) lcd_fila_P(1, PSTR("# borra, otra no"));
  else if (estado == G_GRABANDO) { lcd_fila_P(1, PSTR("Grabando")); reloj(1, 9, ahora() - t_inicio); lcd_numero(1, 15, gra_cab.n, 3); }
  else if (!gra_cab.n) lcd_fila_P(1, PSTR("Sin grabacion"));
  else {
    lcd_fila_P(1, estado == G_REPITIENDO ? PSTR("Repite") : PSTR("Listo"));
    reloj(1, 7, (uint32_t)gra_cab.dur << 3);
    lcd_numero(1, 13, gra_cab.n, 3);
    lcd_texto_P(1, 17, PSTR("ev"));
  }
  pintar_barras(2);
  lcd_fila_P(3, ((uint8_t)(ahora() / 3000) & 1) ? PSTR("3borrar  *# octava") : PSTR("1grabar 2repite 0alto"));
}
