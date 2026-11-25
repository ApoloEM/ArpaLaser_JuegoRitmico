/*
 * sonido.h — las 8 voces del arpa, una por haz: qué nota, con qué sonido y con qué volumen en cada momento.
 *
 * La interrupción (latido.c) sólo suma las voces. Aquí, en el lazo principal y cada 2 ms, se calcula su
 * envolvente con la regla de volumen del proyecto:
 *   - al cortar el haz, la nota sube a su fuerza en ≈5 ms;
 *   - mientras la mano sigue en el haz se apaga con tau = 0.4 s (como apagar una cuerda con el dedo);
 *   - al sacar la mano sigue sonando y se apaga sola con el tau de la perilla de decaimiento (0.3 a 3 s);
 *   - con un botón de sostener apretado, tau = 4 s.
 * La batería es un golpe por corte: dejar la mano no cambia nada.
 */
#ifndef SONIDO_H
#define SONIDO_H

#include <stdint.h>

enum { S_ARPA, S_PIANO, S_GUITARRA, S_BATERIA, N_SONIDOS };

void sonido_iniciar(void);
void sonido_tarea(void);                       /* llamar en cada vuelta del lazo: avanza las envolventes */

void nota_tocar(uint8_t haz, uint8_t midi, uint8_t sonido, uint8_t fuerza);   /* golpe con la mano dentro del haz */
void nota_soltar(uint8_t haz);                                                /* la mano salió del haz */
void nota_puntear(uint8_t haz, uint8_t midi, uint8_t sonido, uint8_t fuerza, uint16_t ms);   /* golpe que se suelta solo a los ms */
void tambor_tocar(uint8_t haz, uint8_t cual, uint8_t fuerza);                 /* cual: 0 bombo … 7 cencerro */
void sonido_callar(void);                      /* apaga todas las voces en ≈20 ms, sin chasquido */

void sonido_volumen(uint8_t v);                /* perilla maestra, 0..255 */
void sonido_decaimiento(uint8_t v);            /* perilla de decaimiento, 0..255 → tau de 0.3 a 3 s */
void sonido_sostener(uint8_t on);
void sonido_mudo(uint8_t on);                  /* el juego de la PC pide callar el sonido propio del arpa */
uint8_t sonido_nivel(uint8_t haz);             /* 0..255: qué tan fuerte suena ese haz ahora (barras de la LCD) */
uint16_t nota_decihertz(uint8_t midi);         /* frecuencia real de la nota, en décimas de Hz */

#endif
