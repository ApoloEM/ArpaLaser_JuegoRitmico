/* EEPROM de mentira: un arreglo de 1024 bytes que la prueba puede revisar. */
#pragma once
#include <stdint.h>
#include <string.h>
extern uint8_t sim_eeprom[1024];
extern unsigned long sim_escrituras;
static inline uint8_t eeprom_read_byte(const uint8_t *p) { return sim_eeprom[(uintptr_t)p & 1023]; }
static inline void eeprom_update_byte(uint8_t *p, uint8_t v) { if (sim_eeprom[(uintptr_t)p & 1023] != v) { sim_eeprom[(uintptr_t)p & 1023] = v; sim_escrituras++; } }
static inline void eeprom_read_block(void *d, const void *p, uint16_t n) { memcpy(d, sim_eeprom + ((uintptr_t)p & 1023), n); }
#define eeprom_is_ready() 1
