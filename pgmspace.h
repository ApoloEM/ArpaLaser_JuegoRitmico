/* En la PC no hay flash aparte: PROGMEM no hace nada y leer de la flash es leer de memoria. */
#pragma once
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define PSTR(s) (s)
typedef const char *PGM_P;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_ptr(p) (*(void *const *)(p))
#define memcpy_P memcpy
