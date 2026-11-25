/*
 * main.c — programa del arpa láser (ATmega328P a 16 MHz, Arduino UNO). Ver arpa.h para el mapa de archivos y el menú.
 *
 * Periféricos e interrupciones que usa el programa completo:
 *   Timer1 + SPI   latido de 16 kHz: 2 DAC MCP4921 y láseres (74HC595)     TIMER1_COMPA   (comun/latido.c)
 *   ADC            8 sensores, 2 perillas y la fuente, dentro del latido
 *   Timer0         reloj de 1 ms                                           TIMER0_COMPA   (comun/latido.c)
 *   Externa        botones de sostener                                     INT0           (comun/latido.c)
 *   USART0         juego Arpa Hero y monitor, 38 400 baudios               USART_RX y USART_UDRE (juego_serie.c)
 *   I2C (TWI)      LCD 20 × 4 y teclado 4 × 4, por sondeo                  (comun/twi.c, lcd.c, teclado.c)
 *   EEPROM         ajustes, récords y grabación                            (memoria.c)
 *
 * El lazo principal (vuelta) nunca espera: cada tarea hace un poquito y regresa. Lo único que tarda es calibrar (0.15 s).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "arpa.h"
#include "tablas.h"
#include "twi.h"
#include "lcd.h"
#include "teclado.h"
#include "juego_serie.h"

ajustes_t aj;
uint8_t modo = M_TOCAR;
uint8_t luz = 0xFF;
uint8_t ultima_midi, ultimo_tambor = 0xFF;

static const uint8_t ESCALAS[3][7] PROGMEM = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 7, 9, 0, 0}, {0, 2, 3, 5, 7, 8, 10}};
static const uint8_t NOTAS_ESCALA[3] PROGMEM = {7, 5, 7};
static const uint8_t QUINTAS[12] PROGMEM = {0, 7, 2, 9, 4, 11, 5, 10, 3, 8, 1, 6};   /* tonalidades en orden de quintas, como los teclados */

/* ---- estado del lazo ---- */
static uint8_t dormidos;                       /* haces apagados por seguridad (bit 0 = izquierda) */
static uint8_t probando;                       /* haces dormidos que se prendieron un instante para ver si ya ven su láser */
static uint16_t desde[N_HACES];                /* desde cuándo está cortado cada haz, en unidades de 64 ms */
static uint8_t todos_oscuros;
static uint32_t t_todos, t_prueba, t_perillas, t_pantalla, t_fuente, t_rescate, t_ver, t_ajustes, t_neblina;
static uint8_t sin_fuente, ignorar_fuente, fuente_baja;
static uint8_t pedal_antes, perilla_enviada, ver, ajustes_sucios;
static uint8_t afinacion_pc[N_HACES], luz_pc = 0xFF, pc_pinta;   /* modo PC: notas, haces y si la PC ya escribió en la LCD */
static uint8_t show;                           /* paso del espectáculo; 0 = no hay */
static uint32_t t_show;
static uint16_t semilla = 0xACE1;

/* ------------------------------------------------------------------------------------------------ */
uint8_t nota_de_haz(uint8_t haz, const ajustes_t *a) {
  uint8_t n = pgm_read_byte(&NOTAS_ESCALA[a->escala]);
  return (uint8_t)(12 * (a->octava + 1) + a->tonica + pgm_read_byte(&ESCALAS[a->escala][haz % n]) + 12 * (haz / n));
}

void golpe_con(uint8_t haz, const ajustes_t *a) {
  if (a->sonido == S_BATERIA) {
    tambor_tocar(haz, haz, 255);
    ultimo_tambor = haz;
    ultima_midi = 0;
  } else {
    ultima_midi = nota_de_haz(haz, a);
    ultimo_tambor = 0xFF;
    nota_tocar(haz, ultima_midi, a->sonido, 255);
  }
}
void golpe(uint8_t haz) { golpe_con(haz, &aj); }

uint8_t sonido_de_cancion(void) { return aj.sonido == S_BATERIA ? S_ARPA : aj.sonido; }
void nota_de_cancion(uint8_t haz, uint8_t midi, uint8_t fuerza, uint16_t ms) {
  ultima_midi = midi;
  ultimo_tambor = 0xFF;
  nota_puntear(haz, midi, sonido_de_cancion(), fuerza, ms);
}

uint8_t azar(uint8_t n) {                      /* registro de corrimiento de 16 bits, revuelto con el reloj */
  uint8_t b;
  semilla ^= (uint16_t)ahora();
  if (!semilla) semilla = 0xACE1;
  b = (uint8_t)(semilla & 1);
  semilla >>= 1;
  if (b) semilla ^= 0xB400;
  return (uint8_t)((uint8_t)(semilla >> 5) % n);
}

void guardar_ajustes(void) {                   /* se escriben 2 s después del último cambio */
  ajustes_sucios = 1;
  t_ajustes = ahora();
}

uint8_t haces_dormidos(void) { return dormidos; }

void entrar_modo(uint8_t m) {
  can_parar();
  jue_salir();
  gra_parar();
  quitar_mensaje();
  luz = 0xFF;
  luz_pc = 0xFF;
  pc_pinta = 0;
  sonido_mudo(0);
  modo = m;
}

/* ---- calibración y espectáculo ---- */
static void mandar_refs(void) {
  uint8_t i, con, sin, d, ref;
  serie_letra('K');
  for (i = 0; i < N_HACES; i++) {
    haces_lectura(i, &con, &sin, &d, &ref);
    serie_letra(i ? ',' : ' ');
    serie_numero(ref);
  }
  serie_letra('\n');
}

static void calibrar(void) {
  uint8_t bien, i, col = 0;
  mensaje_P(PSTR("Calibrando"), PSTR("No toques los haces"), PSTR(""), PSTR(""), 0);
  lcd_vaciar();
  sonido_callar();
  haces_prender(0xFF);
  bien = haces_calibrar();
  haces_modo(LASER_NORMAL);                    /* por si venía del espectáculo, con los láseres fijos */
  dormidos = 0;
  probando = 0;
  todos_oscuros = 0;
  mandar_refs();
  if (bien == 0xFF) mensaje_P(PSTR("Listo: 8 de 8 haces"), PSTR(""), PSTR(""), PSTR(""), 1500);
  else {
    mensaje_P(PSTR("Sin luz en el haz:"), PSTR(""), PSTR("Apunta su laser y"), PSTR("manten 0: calibrar"), 4000);
    for (i = 0; i < N_HACES; i++) if (!(bien & (1 << i))) { lcd_car(1, col, (char)('1' + i)); col += 2; }
  }
}

/* Espectáculo de encendido: los láseres se prenden de izquierda a derecha con su nota, baja la escala, tres
 * destellos con un acorde, y al final se calibra. Pasos 1 a 8, 9 a 15, 16 a 21 y 22. */
static void espectaculo(void) {
  entrar_modo(M_TOCAR);
  sonido_callar();
  haces_modo(LASER_FIJO);
  luz = 0;
  show = 1;
  t_show = ahora() + 200;
  mensaje_P(PSTR("     ARPA LASER"), PSTR(""), PSTR("  Microcontroladores"), PSTR("     UABC 2026-2"), 0);
}

static void show_tarea(uint32_t t) {
  uint8_t p = show;
  if (!p || (int32_t)(t - t_show) < 0) return;
  if (p <= 8) {                                /* sube: un láser más y su nota */
    luz |= (uint8_t)(1 << (p - 1));
    nota_de_cancion((uint8_t)(p - 1), nota_de_haz((uint8_t)(p - 1), &aj), 190, 60);
    t_show += 130;
  } else if (p <= 15) {                        /* baja la escala */
    nota_de_cancion((uint8_t)(15 - p), nota_de_haz((uint8_t)(15 - p), &aj), 150, 60);
    t_show += p == 15 ? 270 : 70;
  } else if (p <= 21) {                        /* tres destellos con acorde */
    if (p & 1) {
      static const uint8_t ACORDE[4] PROGMEM = {0, 2, 4, 7};
      uint8_t k;
      luz = 0xFF;
      for (k = 0; k < 4; k++) { uint8_t h = pgm_read_byte(&ACORDE[k]); nota_de_cancion(h, nota_de_haz(h, &aj), 180, 90); }
    } else luz = 0;
    t_show += p == 21 ? 500 : 160;
  } else {
    show = 0;
    luz = 0xFF;
    calibrar();
    return;
  }
  show = (uint8_t)(p + 1);
}

/* ---- seguridad de los láseres (clase 3R) ----
 * Si los 8 sensores quedan a oscuras a la vez (quitaron la barra o algo tapa todo), los láseres se apagan en menos
 * de 50 ms. Si un solo haz lleva 10 s cortado, se apaga ese. Cada 2 s los apagados destellan 40 ms: si su sensor
 * vuelve a ver la luz, regresan solos. */
static void seguridad(uint8_t cortados, uint8_t *cortes, uint8_t *libres, uint32_t t) {
  uint8_t i, activos = (uint8_t)(luz & ~dormidos);
  uint16_t t64 = (uint16_t)(t >> 6);
  for (i = 0; i < N_HACES; i++) {
    uint8_t bit = (uint8_t)(1 << i);
    if ((activos & cortados & bit) == 0) desde[i] = t64;
    else if ((uint16_t)(t64 - desde[i]) >= 156) { dormidos |= bit; nota_soltar(i); }      /* 10 s */
  }
  if (activos == 0xFF && cortados == 0xFF) {
    if (!todos_oscuros) { todos_oscuros = 1; t_todos = t; }
    else if (t - t_todos >= 30) { dormidos = 0xFF; sonido_callar(); t_prueba = t; }
  } else todos_oscuros = 0;

  if (probando) {
    uint8_t ya = (uint8_t)(*libres & probando);                 /* estos volvieron a ver su láser */
    dormidos &= (uint8_t)~ya;
    probando &= (uint8_t)~ya;
    if (t - t_prueba >= 40) { probando = 0; t_prueba = t; }
  } else if (dormidos && t - t_prueba >= 2000) {
    probando = dormidos;
    t_prueba = t;
  }
  *cortes &= (uint8_t)~dormidos;
}

/* ---- lo que pasa al cortar y al liberar un haz ---- */
static void al_cortar(uint8_t i) {
  serie_haz((uint8_t)(i + 1), 1);
  switch (modo) {
    case M_CANCIONES: if (!can_corte(i)) golpe(i); break;
    case M_GRABAR: golpe(i); gra_evento(i, 1); break;
    case M_JUEGOS: if (jue_activo()) jue_corte(i); else golpe(i); break;
    case M_PC:
      if (afinacion_pc[i]) { ultima_midi = afinacion_pc[i]; ultimo_tambor = 0xFF; nota_tocar(i, ultima_midi, sonido_de_cancion(), 255); }
      else golpe(i);
      break;
    default: golpe(i); break;
  }
}
static void al_liberar(uint8_t i) {
  serie_haz((uint8_t)(i + 1), 0);
  nota_soltar(i);
  if (modo == M_GRABAR) gra_evento(i, 0);
}

/* ---- teclado ---- */
static void octava(int8_t d) {
  uint8_t o = (uint8_t)(aj.octava + d);
  if (o >= 4 && o <= 6) { aj.octava = o; guardar_ajustes(); }
}

static void tecla_tocar(char k) {
  if (k >= '1' && k <= '4') aj.sonido = (uint8_t)(k - '1');
  else if (k >= '5' && k <= '7') aj.escala = (uint8_t)(k - '5');
  else if (k == '8') {
    uint8_t q;
    for (q = 0; q < 12; q++) if (pgm_read_byte(&QUINTAS[q]) == aj.tonica) break;
    aj.tonica = pgm_read_byte(&QUINTAS[(q + 1) % 12]);
  } else if (k == '*') { octava(-1); return; }
  else if (k == '#') { octava(1); return; }
  else return;
  guardar_ajustes();
}

static void tecla(uint8_t tipo, char k) {
  if (modo == M_PC) {                          /* todo va al juego; sólo mantener D regresa a Tocar */
    if (tipo == TECLA_PULSA) serie_tecla(k);
    if (tipo == TECLA_LARGA && k == 'D') entrar_modo(M_TOCAR);
    return;
  }
  if (sin_fuente && tipo == TECLA_PULSA && k == '#') { ignorar_fuente = 1; return; }
  /* 0 y 9 tienen dos usos: corto al soltar y largo al mantener. Las demás actúan al apretar. */
  if (k == '0' || k == '9') {
    if (tipo == TECLA_PULSA) return;
    if (tipo == TECLA_LARGA) {
      if (k == '0') calibrar();
      else espectaculo();
      return;
    }
  } else if (tipo != TECLA_PULSA) return;

  if (k >= 'A' && k <= 'D') {
    uint8_t m = (uint8_t)(k - 'A');
    if (m != modo) entrar_modo(m);
    else if (k == 'B') { can_aprende = !can_aprende; if (can_activa()) can_empezar(can_elegida); }
    return;
  }
  if (k == '9') {                              /* neblina: se apaga sola a los 5 minutos */
    neblina(!neblina_prendida());
    t_neblina = ahora();
    return;
  }
  switch (modo) {
    case M_TOCAR:
      if (k == '0') sonido_callar(); else tecla_tocar(k);
      break;
    case M_CANCIONES:
      if (k >= '1' && k <= '8') can_empezar((uint8_t)(k - '1'));
      else if (k == '*' && aj.tempo > 50) { aj.tempo -= 10; guardar_ajustes(); }
      else if (k == '#' && aj.tempo < 150) { aj.tempo += 10; guardar_ajustes(); }
      else if (k == '0') { can_parar(); sonido_callar(); }
      break;
    case M_GRABAR: gra_tecla(k); break;
    default:
      if (k == '4' && !jue_activo()) { entrar_modo(M_PC); break; }
      jue_tecla(k);
      break;
  }
}

/* ---- puerto serie: el juego de la PC y el monitor del banco ---- */
static void mandar_lecturas(void) {
  uint8_t i, con, sin, d, ref;
  serie_letra('V');
  for (i = 0; i < N_HACES; i++) {
    haces_lectura(i, &con, &sin, &d, &ref);
    serie_letra(i ? ',' : ' ');
    serie_numero(con);
    serie_letra(':');
    serie_numero(sin);
  }
  serie_letra('\n');
}

static void serie_tarea(void) {
  comando_t c;
  uint8_t i;
  while (serie_comando(&c)) {
    if (c.tipo == CMD_BANCO) {                 /* las órdenes del monitor del banco siguen sirviendo para apuntar los láseres */
      switch (c.valor) {
        case 'k': calibrar(); break;
        case 'n': luz = 0xFF; haces_modo(LASER_NORMAL); break;
        case 'f': haces_modo(LASER_FIJO); break;
        case 'x': haces_modo(LASER_APAGADO); break;
        case 'v': ver = (c.texto[0] == '1'); break;
        default: break;
      }
      continue;
    }
    if (c.tipo == CMD_NEBLINA) { neblina(c.valor); t_neblina = ahora(); continue; }
    if (modo != M_PC) entrar_modo(M_PC);       /* cualquier orden del juego mete al arpa en el modo PC */
    switch (c.tipo) {
      case CMD_SILENCIO: sonido_mudo(c.valor); break;
      case CMD_AFINACION: for (i = 0; i < N_HACES; i++) afinacion_pc[i] = c.notas[i] >= MIDI_MIN && c.notas[i] <= MIDI_MAX ? c.notas[i] : 0; break;
      case CMD_HACES: luz_pc = c.valor; break;
      case CMD_LCD: pc_pinta = 1; lcd_fila((uint8_t)(c.fila - 1), c.texto); break;
      default: break;
    }
  }
}

/* ------------------------------------------------------------------------------------------------ */
void iniciar(void) {
  latido_iniciar();
  serie_iniciar();
  mem_cargar();
  sonido_iniciar();
  lcd_iniciar();                               /* antes de sei(): sus esperas de arranque no se alargan */
  teclado_iniciar();
  sei();
  serie_texto("# arpa laser");
  espectaculo();
}

void vuelta(void) {
  uint8_t cortes, libres, cortados, i, p, m;
  uint32_t t = ahora();
  char k;

  sonido_tarea();
  teclado_tarea();
  lcd_tarea();
  mem_tarea();

  /* la fuente de 12 V: sin ella no hay láseres (el UNO puede estar prendido sólo por el USB) */
  if (t - t_fuente >= 500) {
    uint8_t f = fuente_potencia();
    t_fuente = t;
    if (f < 60) { if (fuente_baja < 2) fuente_baja++; } else if (f > 100) fuente_baja = 0;
    if (!sin_fuente && fuente_baja >= 2 && !ignorar_fuente) {
      sin_fuente = 1;
      mensaje_P(PSTR("Conecta la fuente"), PSTR("de 12 V y prende"), PSTR("el interruptor"), PSTR("# seguir sin ella"), 0);
    } else if (sin_fuente && (fuente_baja == 0 || ignorar_fuente)) {
      sin_fuente = 0;
      if (!show) calibrar();
    }
  }

  /* haces */
  cortados = haces_eventos(&cortes, &libres);
  if (sin_fuente || show || haces_modo_actual() != LASER_NORMAL) cortes = libres = 0;
  else {
    seguridad(cortados, &cortes, &libres, t);
    for (i = 0; i < N_HACES; i++) {
      uint8_t bit = (uint8_t)(1 << i);
      if (cortes & bit) al_cortar(i);
      if (libres & bit) al_liberar(i);
    }
  }

  /* teclado, botones de sostener y puerto serie */
  p = teclado_evento(&k);
  if (p != TECLA_NADA) tecla(p, k);
  p = sostener();
  if (p != pedal_antes) { pedal_antes = p; sonido_sostener(p); serie_sostener(p); }
  serie_tarea();

  /* perillas: el volumen va al cuadrado para que la perilla se sienta pareja */
  if (t - t_perillas >= 50) {
    uint8_t v = perilla_volumen();
    t_perillas = t;
    sonido_volumen((uint8_t)(((uint16_t)v * v) >> 8));
    sonido_decaimiento(perilla_decaimiento());
    if (modo == M_PC && (uint8_t)(v - perilla_enviada + 3) > 6) { perilla_enviada = v; serie_perilla((uint16_t)v << 2); }
  }

  /* lo propio de cada modo */
  show_tarea(t);
  can_tarea();
  gra_tarea();
  jue_tarea();

  /* láseres: lo que pide el modo, menos los dormidos, más los que se están probando */
  m = modo == M_PC ? luz_pc : luz;
  m = (uint8_t)((m & ~dormidos) | probando);
  if (m != haces_prendidos()) haces_prender(m);

  if (t - t_rescate >= 500) { t_rescate = t; haces_rescatar(); }
  if (neblina_prendida() && t - t_neblina >= 300000UL) neblina(0);
  if (ver && t - t_ver >= 100) { t_ver = t; mandar_lecturas(); }
  if (ajustes_sucios && t - t_ajustes >= 2000) { ajustes_sucios = 0; mem_guardar(MEM_AJUSTES); }
  if (t - t_pantalla >= 100) {
    t_pantalla = t;
    if (!(modo == M_PC && pc_pinta)) pantalla();
  }
}

#ifndef PRUEBA_PC
int main(void) {
  iniciar();
  for (;;) vuelta();
}
#endif
