/*
 * arpa.h — lo que comparten los archivos del programa del arpa láser.
 *
 *   main.c        arranque, lazo principal, seguridad de los láseres, modo Tocar, modo PC y teclado
 *   pantalla.c    lo que se ve en la LCD en cada modo
 *   canciones.c   las 8 obras: Sola (el arpa toca y prende el láser de cada nota) y Aprende (espera tu corte)
 *   grabar.c      grabar lo que tocas y repetirlo para tocar encima
 *   juegos.c      Simón, reflejos y duelo para dos
 *   memoria.c     EEPROM: ajustes, récords y la grabación
 *   ../comun      latido (audio, ADC, láseres), sonido, I2C, LCD y teclado; ../../juego/firmware: el puerto serie
 *
 * Menú (teclado 4 × 4): las letras cambian de modo y los números eligen dentro del modo.
 *   A Tocar      1 arpa  2 piano  3 guitarra  4 batería · 5 mayor  6 pentatónica  7 menor · 8 tonalidad
 *                9 neblina (mantener: espectáculo) · * # octava · 0 silencio (mantener: calibrar)
 *   B Canciones  1 a 8 empiezan una obra · B otra vez: Sola o Aprende · * # tempo · 0 parar
 *   C Grabar     1 grabar o terminar · 2 repetir o parar · 3 borrar (# confirma) · * # octava · 0 parar
 *   D Juegos     1 Simón  2 reflejos  3 duelo  4 Arpa Hero (PC) · * # nivel · 0 salir
 */
#ifndef ARPA_H
#define ARPA_H

#include <stdint.h>
#include <avr/pgmspace.h>
#include "latido.h"
#include "sonido.h"

enum { M_TOCAR, M_CANCIONES, M_GRABAR, M_JUEGOS, M_PC };
enum { E_MAYOR, E_PENTA, E_MENOR };

typedef struct {
  uint8_t sonido;            /* S_ARPA … S_BATERIA */
  uint8_t escala;            /* E_MAYOR, E_PENTA o E_MENOR */
  uint8_t tonica;            /* 0 = Do … 11 = Si */
  uint8_t octava;            /* 4 a 6 */
  uint8_t nivel;             /* dificultad de los juegos, 1 a 3 */
  uint8_t tempo;             /* de las canciones, en %: 50 a 150 */
} ajustes_t;

/* ---- main.c ---- */
extern ajustes_t aj;
extern uint8_t modo;
extern uint8_t luz;                           /* haces que el modo quiere prendidos (bit 0 = izquierda) */
extern uint8_t ultima_midi, ultimo_tambor;    /* lo último que sonó, para la LCD (0 y 0xFF = nada) */
uint8_t nota_de_haz(uint8_t haz, const ajustes_t *a);
void golpe(uint8_t haz);                      /* el haz se cortó: suena su nota (o su tambor) con los ajustes de ahora */
void golpe_con(uint8_t haz, const ajustes_t *a);
uint8_t sonido_de_cancion(void);              /* el sonido elegido; con batería, arpa */
void nota_de_cancion(uint8_t haz, uint8_t midi, uint8_t fuerza, uint16_t ms);
uint8_t azar(uint8_t n);                      /* 0 … n − 1 */
void guardar_ajustes(void);
void entrar_modo(uint8_t m);
uint8_t haces_dormidos(void);                 /* haces apagados por seguridad */

/* ---- pantalla.c ---- */
void pantalla(void);                          /* pinta el modo que esté activo */
void mensaje_P(PGM_P a, PGM_P b, PGM_P c, PGM_P d, uint16_t ms);   /* 4 renglones fijos durante ms (0 = hasta quitar_mensaje) */
void quitar_mensaje(void);
uint8_t hay_mensaje(void);
void pintar_barras(uint8_t fila);
void pintar_nota(uint8_t fila, uint8_t tonica);
extern const char NOMBRE_SONIDO[4][9] PROGMEM;

/* ---- canciones.c ---- */
extern uint8_t can_elegida, can_aprende;      /* obra 0..7; 0 = Sola, 1 = Aprende */
uint8_t can_activa(void);
void can_empezar(uint8_t cual);
void can_parar(void);
void can_tarea(void);
uint8_t can_corte(uint8_t haz);               /* 1 = la canción se quedó con el corte */
void can_pantalla(void);

/* ---- grabar.c ---- */
void gra_tecla(char k);
void gra_evento(uint8_t haz, uint8_t corte);  /* lo que se toca en vivo, por si se está grabando */
void gra_tarea(void);
void gra_parar(void);
void gra_pantalla(void);

/* ---- juegos.c ---- */
uint8_t jue_activo(void);
void jue_tecla(char k);
void jue_corte(uint8_t haz);
void jue_tarea(void);
void jue_salir(void);
void jue_pantalla(void);

/* ---- memoria.c ---- */
#define GRA_MAX 200                           /* eventos de la grabación: 2 bytes cada uno */
typedef struct {
  ajustes_t aj;                               /* sonido, escala, tonalidad y octava con que se grabó */
  uint8_t n;                                  /* eventos */
  uint16_t dur;                               /* duración en unidades de 8 ms */
} gra_cabeza_t;
extern gra_cabeza_t gra_cab;
extern uint8_t gra_ev[GRA_MAX][2];            /* [0] = unidades de 8 ms desde el evento anterior; [1] = haz | 8 si es corte (0xFF = sólo espera) */
extern uint8_t record_simon;
extern uint16_t record_reflejos;              /* ms; 0 = todavía no hay */
void mem_cargar(void);
void mem_guardar(uint8_t que);                /* MEM_AJUSTES o MEM_GRABACION: se escribe poco a poco, sin detener el arpa */
void mem_tarea(void);
enum { MEM_AJUSTES = 1, MEM_GRABACION = 2 };

#endif
