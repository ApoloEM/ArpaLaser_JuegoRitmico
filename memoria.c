/*
 * memoria.c — lo que el arpa recuerda apagada (EEPROM del ATmega328P, 1024 bytes).
 *
 *   0        firma 0xA7      1  versión
 *   2 a 7    ajustes: sonido, escala, tonalidad, octava, nivel y tempo
 *   8        récord del Simón          9 y 10  récord de reflejos (ms)
 *   16       firma de la grabación (0x5A = hay una)
 *   17 a 25  cabeza de la grabación    32 en adelante  sus eventos, 2 bytes cada uno
 *
 * Escribir un byte tarda 3.4 ms. Para no detener el arpa, mem_tarea() escribe UN byte por vuelta del lazo y sólo
 * cuando la EEPROM terminó el anterior; eeprom_update_byte además se salta los bytes que no cambiaron.
 */
#include <avr/io.h>
#include <avr/eeprom.h>
#include "arpa.h"

#define FIRMA 0xA7
#define VERSION 1
#define FIRMA_GRA 0x5A
#define DIR_GRA 16
#define DIR_EVENTOS 32

gra_cabeza_t gra_cab;
uint8_t gra_ev[GRA_MAX][2];
uint8_t record_simon;
uint16_t record_reflejos;

static uint8_t pendiente;                      /* MEM_AJUSTES | MEM_GRABACION */
static uint16_t pos_aj, pos_gra;               /* por qué byte va cada trabajo */

void mem_cargar(void) {
  uint8_t i;
  aj.sonido = S_ARPA; aj.escala = E_MAYOR; aj.tonica = 0; aj.octava = 5; aj.nivel = 2; aj.tempo = 100;
  record_simon = 0;
  record_reflejos = 0;
  gra_cab.n = 0;
  if (eeprom_read_byte((uint8_t *)0) != FIRMA || eeprom_read_byte((uint8_t *)1) != VERSION) return;   /* EEPROM nueva */
  for (i = 0; i < sizeof aj; i++) ((uint8_t *)&aj)[i] = eeprom_read_byte((uint8_t *)(uint16_t)(2 + i));
  if (aj.sonido >= N_SONIDOS) aj.sonido = S_ARPA;                /* por si quedó basura */
  if (aj.escala > E_MENOR) aj.escala = E_MAYOR;
  if (aj.tonica > 11) aj.tonica = 0;
  if (aj.octava < 4 || aj.octava > 6) aj.octava = 5;
  if (aj.nivel < 1 || aj.nivel > 3) aj.nivel = 2;
  if (aj.tempo < 50 || aj.tempo > 150) aj.tempo = 100;
  record_simon = eeprom_read_byte((uint8_t *)8);
  record_reflejos = (uint16_t)(eeprom_read_byte((uint8_t *)9) | ((uint16_t)eeprom_read_byte((uint8_t *)10) << 8));
  if (eeprom_read_byte((uint8_t *)DIR_GRA) == FIRMA_GRA) {
    eeprom_read_block(&gra_cab, (uint8_t *)(DIR_GRA + 1), sizeof gra_cab);
    if (gra_cab.n > GRA_MAX) gra_cab.n = 0;
    eeprom_read_block(gra_ev, (uint8_t *)DIR_EVENTOS, (uint16_t)gra_cab.n * 2);
  }
}

void mem_guardar(uint8_t que) {
  pendiente |= que;
  if (que & MEM_AJUSTES) pos_aj = 0;           /* si ya iba escribiendo, empieza otra vez con lo nuevo */
  if (que & MEM_GRABACION) pos_gra = 0;
}

void mem_tarea(void) {
  if (!pendiente || !eeprom_is_ready()) return;
  if (pendiente & MEM_AJUSTES) {
    uint8_t v;
    uint16_t k = pos_aj;
    if (k == 0) v = FIRMA;
    else if (k == 1) v = VERSION;
    else if (k < 8) v = ((uint8_t *)&aj)[k - 2];
    else if (k == 8) v = record_simon;
    else v = (uint8_t)(k == 9 ? record_reflejos : record_reflejos >> 8);
    eeprom_update_byte((uint8_t *)k, v);
    if (++pos_aj > 10) pendiente &= (uint8_t)~MEM_AJUSTES;
    return;
  }
  {                                            /* la grabación: firma, cabeza y sólo los eventos usados */
    uint16_t k = pos_gra, total = (uint16_t)(1 + sizeof gra_cab + (uint16_t)gra_cab.n * 2);
    if (k == 0) eeprom_update_byte((uint8_t *)DIR_GRA, gra_cab.n ? FIRMA_GRA : 0);
    else if (k <= sizeof gra_cab) eeprom_update_byte((uint8_t *)(DIR_GRA + k), ((uint8_t *)&gra_cab)[k - 1]);
    else { uint16_t e = (uint16_t)(k - 1 - sizeof gra_cab); eeprom_update_byte((uint8_t *)(DIR_EVENTOS + e), ((uint8_t *)gra_ev)[e]); }
    if (++pos_gra >= total) pendiente &= (uint8_t)~MEM_GRABACION;
  }
}
