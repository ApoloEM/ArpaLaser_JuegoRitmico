/*
 * sonido.c — envolventes y tablas de las 8 voces (ver sonido.h). Todo esto corre en el lazo principal.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "sonido.h"
#include "latido.h"
#include "tablas.h"

enum { V_LIBRE, V_ATAQUE, V_MANO, V_SUELTA, V_GOLPE, V_CALLA };

typedef struct {
  uint16_t nivel;          /* envolvente, 0..65535 */
  uint16_t pico;           /* hasta dónde sube en el ataque */
  uint16_t factor;         /* por cuánto se multiplica en cada tick al apagarse (V_GOLPE) */
  uint16_t edad;           /* ticks de 2 ms desde el golpe */
  uint16_t suelta_en;      /* tick en que se suelta sola (0 = cuando salga la mano) */
  uint16_t inc_base, inc_extra;   /* tambores: tono final y lo que le sobra al principio */
  uint8_t estado;
  uint8_t sonido;          /* S_ARPA, S_PIANO, S_GUITARRA o S_BATERIA */
  uint8_t registro;        /* 0..3, según qué tan aguda es la nota */
  uint8_t paso;            /* en cuál de sus tablas va (0 = la más brillante) */
  uint8_t mano;            /* 1 = la mano sigue en el haz */
  uint8_t tambor;
} env_t;

static env_t env[N_HACES];
static uint8_t volumen = 180, mudo, pedal;
static uint16_t factor_suelta = 65300;
static uint16_t reloj;                         /* milisegundos ya atendidos (16 bits bastan para restar) */

static const int8_t *tabla_de(uint8_t sonido, uint8_t registro, uint8_t paso) {
  uint8_t t;
  if (registro >= 3) t = T_SENO;
  else if (registro == 2) t = (uint8_t)(sonido * TABLAS_POR_SONIDO + 7 + (paso >= 2));      /* 2 tablas */
  else if (registro == 1) t = (uint8_t)(sonido * TABLAS_POR_SONIDO + 4 + (paso > 2 ? 2 : paso));   /* 3 tablas */
  else t = (uint8_t)(sonido * TABLAS_POR_SONIDO + paso);                                  /* 4 tablas */
  return ONDAS[t];
}

/* inc y tabla son de 2 bytes y la interrupción los lee: se cambian con las interrupciones cerradas. */
static void voz_poner(uint8_t haz, uint16_t inc, const int8_t *tabla, uint8_t reiniciar) {
  uint8_t s = SREG;
  cli();
  voz[haz].inc = inc;
  voz[haz].tabla = tabla;
  if (reiniciar) voz[haz].fase = 0;
  SREG = s;
}

void sonido_iniciar(void) {
  uint8_t i;
  for (i = 0; i < N_HACES; i++) {
    env[i].estado = V_LIBRE;
    voz[i].gi = voz[i].gd = 0;
    voz_poner(i, 0, ONDAS[T_SENO], 1);
  }
  reloj = (uint16_t)ahora();
}

uint16_t nota_decihertz(uint8_t midi) {
  uint16_t inc = pgm_read_word(&INC_NOTA[midi - MIDI_MIN]);
  return (uint16_t)(((uint32_t)inc * (FS_HZ * 10UL)) >> 16);
}

/* La interrupción tiene tiempo para 6 voces sonando sin ahogar al lazo principal. Antes de empezar una nota nueva,
 * si ya suenan 6 se apaga rápido la más débil (≈20 ms); si con todo y eso hay 7, la más débil se corta en seco. */
#define MAX_VOCES 6
static void hacer_lugar(uint8_t haz) {
  uint8_t i, n = 0, callando = 0, debil = 0xFF;
  for (i = 0; i < N_HACES; i++) {
    if (i == haz || env[i].estado == V_LIBRE) continue;
    n++;
    if (env[i].estado == V_CALLA) callando++;
    if (debil == 0xFF || env[i].nivel < env[debil].nivel) debil = i;
  }
  if (n < MAX_VOCES) return;
  if (n > MAX_VOCES || callando == 0) {
    if (n > MAX_VOCES) { env[debil].estado = V_LIBRE; env[debil].nivel = 0; voz[debil].gi = 0; voz[debil].gd = 0; }
    else env[debil].estado = V_CALLA;
  }
}

void nota_tocar(uint8_t haz, uint8_t midi, uint8_t sonido, uint8_t fuerza) {
  env_t *e = &env[haz];
  uint8_t sonaba = e->estado != V_LIBRE;
  hacer_lugar(haz);
  if (midi < MIDI_MIN) midi = MIDI_MIN;
  if (midi > MIDI_MAX) midi = MIDI_MAX;
  if (sonido >= S_BATERIA) sonido = S_ARPA;
  e->sonido = sonido;
  e->registro = midi >= MIDI_REG3 ? 3 : midi >= MIDI_REG2 ? 2 : midi >= MIDI_REG1 ? 1 : 0;
  e->paso = 0;
  e->edad = 0;
  e->suelta_en = 0;
  e->mano = 1;
  e->pico = (uint16_t)fuerza << 8;
  e->estado = V_ATAQUE;                        /* si ya sonaba, sube desde donde iba: no hay chasquido */
  voz_poner(haz, pgm_read_word(&INC_NOTA[midi - MIDI_MIN]), tabla_de(sonido, e->registro, 0), !sonaba);
}

void nota_soltar(uint8_t haz) {
  env_t *e = &env[haz];
  e->mano = 0;
  if (e->estado == V_MANO) e->estado = V_SUELTA;
}

void nota_puntear(uint8_t haz, uint8_t midi, uint8_t sonido, uint8_t fuerza, uint16_t ms_) {
  nota_tocar(haz, midi, sonido, fuerza);
  env[haz].suelta_en = (uint16_t)(ms_ / TICK_MS) + 1;
}

void tambor_tocar(uint8_t haz, uint8_t cual, uint8_t fuerza) {
  env_t *e = &env[haz];
  tambor_t t;
  memcpy_P(&t, &TAMBORES[cual & 7], sizeof t);
  hacer_lugar(haz);
  e->sonido = S_BATERIA;
  e->tambor = cual & 7;
  e->edad = 0;
  e->suelta_en = 0;
  e->mano = 0;
  e->inc_base = t.inc;
  e->inc_extra = t.inc_extra;
  e->factor = t.factor;
  e->nivel = (uint16_t)(((uint16_t)t.fuerza * fuerza) >> 8) << 8;   /* un tambor no tiene ataque: empieza en su fuerza */
  e->pico = e->nivel;
  e->estado = V_GOLPE;
  voz_poner(haz, (uint16_t)(t.inc + t.inc_extra), ONDAS[t.tabla], 1);
}

void sonido_callar(void) {
  uint8_t i;
  for (i = 0; i < N_HACES; i++)
    if (env[i].estado != V_LIBRE) env[i].estado = V_CALLA;
}

void sonido_volumen(uint8_t v) { volumen = v; }
void sonido_decaimiento(uint8_t v) { factor_suelta = pgm_read_word(&FACTOR_SUELTA[v >> 3]); }
void sonido_sostener(uint8_t on) { pedal = on; }
void sonido_mudo(uint8_t on) { mudo = on; }
uint8_t sonido_nivel(uint8_t haz) { return env[haz].estado == V_LIBRE ? 0 : (uint8_t)(env[haz].nivel >> 8); }

static uint16_t por(uint16_t a, uint16_t factor) { return (uint16_t)(((uint32_t)a * factor) >> 16); }

/* Un tick de 2 ms para la voz de un haz. */
static void tick(uint8_t haz) {
  env_t *e = &env[haz];
  uint8_t a;
  if (e->estado == V_LIBRE) return;
  if (e->edad != 0xFFFF) e->edad++;

  switch (e->estado) {
    case V_ATAQUE: {                           /* sube en 3 ticks (≈5 ms) */
      uint16_t escalon = e->pico / 3 + 1;
      if (e->nivel >= e->pico || e->pico - e->nivel <= escalon) { e->nivel = e->pico; e->estado = e->mano ? V_MANO : V_SUELTA; }
      else e->nivel += escalon;
      break;
    }
    case V_MANO: e->nivel = por(e->nivel, FACTOR_MANO); break;
    case V_SUELTA: e->nivel = por(e->nivel, pedal ? FACTOR_PEDAL : factor_suelta); break;
    case V_CALLA: e->nivel = por(e->nivel, FACTOR_RAPIDO); break;
    default: e->nivel = por(e->nivel, e->factor); break;
  }
  if (e->suelta_en && e->edad >= e->suelta_en) { e->suelta_en = 0; nota_soltar(haz); }

  if (e->estado == V_CALLA) {
    /* apagándose: ya no cambia de tabla ni de tono */
  } else if (e->sonido == S_BATERIA) {
    if (e->inc_extra) {                        /* bombo y toms: el tono cae hacia su nota final */
      e->inc_extra = por(e->inc_extra, FACTOR_BARRIDO);
      voz_poner(haz, (uint16_t)(e->inc_base + e->inc_extra), voz[haz].tabla, 0);
    }
    if (e->tambor == 4) {                      /* palmas: tres golpes secos (0, 12 y 22 ms) y luego la cola */
      if (e->edad == 6 || e->edad == 11) e->nivel = e->pico;
      e->factor = e->edad < 12 ? FACTOR_RAPIDO : pgm_read_word(&TAMBORES[4].factor);
    }
  } else if (e->paso < 3 && e->edad >= pgm_read_byte(&CAMBIO_TABLA[e->sonido][e->paso])) {
    e->paso++;                                 /* la nota pierde brillo: pasa a su tabla siguiente */
    voz_poner(haz, voz[haz].inc, tabla_de(e->sonido, e->registro, e->paso), 0);
  }

  if (e->nivel < 48 && e->estado != V_ATAQUE) {
    e->estado = V_LIBRE;
    e->nivel = 0;
  }
  /* volumen final de la voz hacia cada bocina: envolvente × perilla maestra × paneo del haz */
  a = mudo ? 0 : (uint8_t)(((uint16_t)(e->nivel >> 8) * volumen) >> 8);
  voz[haz].gi = (uint8_t)(((uint16_t)a * pgm_read_byte(&PAN_I[haz])) >> 8);
  voz[haz].gd = (uint8_t)(((uint16_t)a * pgm_read_byte(&PAN_D[haz])) >> 8);
}

void sonido_tarea(void) {
  uint16_t t = (uint16_t)ahora();
  uint16_t atraso = (uint16_t)(t - reloj);
  uint8_t i;
  if (atraso < TICK_MS) return;
  if (atraso > 40) { reloj = (uint16_t)(t - 40); atraso = 40; }   /* si el lazo se atrasó mucho, no intenta recuperar todo */
  while (atraso >= TICK_MS) {
    for (i = 0; i < N_HACES; i++) tick(i);
    reloj += TICK_MS;
    atraso -= TICK_MS;
  }
}
