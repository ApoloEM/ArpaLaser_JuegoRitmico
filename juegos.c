/*
 * juegos.c — tres juegos con los haces. El nivel (1 a 3, con * y #) cambia la velocidad del Simón.
 *
 *   1 Simón:    el arpa prende una secuencia de haces con su nota y hay que repetirla; cada ronda crece una nota.
 *   2 Reflejos: se prende un haz al azar y se mide en milisegundos cuánto tardas en cortarlo; son 5 intentos.
 *   3 Duelo:    para dos. Se prende un haz de cada lado al mismo tiempo; gana el punto quien corte el suyo primero.
 * Los récords del Simón y de reflejos se guardan en la EEPROM. En los juegos sólo cuentan los haces prendidos:
 * la detección del latido ignora un haz cuyo láser está apagado.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "arpa.h"
#include "lcd.h"

enum { J_NADA, J_SIMON, J_REFLEJOS, J_DUELO };
enum { F_MUESTRA, F_TURNO, F_BIEN, F_ESPERA, F_RONDA, F_FIN };
#define SIMON_MAX 32

static uint8_t juego, fase;
static uint32_t t_sig;                         /* cuándo pasa lo siguiente */
static uint8_t secuencia[SIMON_MAX], largo, pos, prendido;      /* Simón */
static uint8_t intento, objetivo;              /* reflejos */
static uint16_t tiempos, ultimo, promedio;
static uint32_t t_luz;
static uint8_t izq, der, gano;                 /* duelo: puntos y quién ganó la ronda (0 nadie, 1 izquierda, 2 derecha) */

uint8_t jue_activo(void) { return juego != J_NADA; }

void jue_salir(void) {
  if (juego == J_NADA) return;
  juego = J_NADA;
  luz = 0xFF;
}

static void error(void) {                      /* dos notas graves de piano, casi juntas */
  nota_puntear(0, 40, S_PIANO, 255, 300);
  nota_puntear(1, 41, S_PIANO, 255, 300);
}

static uint16_t paso_simon(void) {             /* ms entre notas al mostrar la secuencia: más rápido con el nivel y con la ronda */
  static const uint16_t BASE[3] PROGMEM = {720, 580, 440};
  uint16_t p = (uint16_t)(pgm_read_word(&BASE[aj.nivel - 1]) - largo * 20);
  return p < 220 || p > 720 ? 220 : p;
}

static void simon_ronda(void) {
  if (largo < SIMON_MAX) secuencia[largo++] = azar(N_HACES);
  fase = F_MUESTRA;
  pos = 0;
  prendido = 0;
  luz = 0;
  t_sig = ahora() + 400;
}

static void empezar(uint8_t cual) {
  juego = cual;
  luz = 0;
  sonido_callar();
  if (cual == J_SIMON) { largo = 0; simon_ronda(); }
  else if (cual == J_REFLEJOS) { intento = 0; tiempos = 0; ultimo = 0; promedio = 0; fase = F_ESPERA; t_sig = ahora() + 1000 + azar(200) * 10; }
  else { izq = 0; der = 0; gano = 0; fase = F_ESPERA; t_sig = ahora() + 1200 + azar(180) * 10; }
}

void jue_tecla(char k) {
  if (k == '1') empezar(J_SIMON);
  else if (k == '2') empezar(J_REFLEJOS);
  else if (k == '3') empezar(J_DUELO);
  else if (k == '0') jue_salir();
  else if (k == '*' && aj.nivel > 1) { aj.nivel--; guardar_ajustes(); }
  else if (k == '#' && aj.nivel < 3) { aj.nivel++; guardar_ajustes(); }
}

void jue_tarea(void) {
  uint32_t t = ahora();
  if (juego == J_NADA || (int32_t)(t - t_sig) < 0) return;
  if (juego == J_SIMON) {
    if (fase == F_MUESTRA) {
      uint16_t paso = paso_simon();
      if (prendido) {                          /* apaga el haz que mostraba; falta el hueco hasta el siguiente */
        luz = 0;
        prendido = 0;
        t_sig = t + paso * 3 / 10;
      } else if (pos < largo) {
        uint8_t h = secuencia[pos++];
        luz = (uint8_t)(1 << h);
        prendido = 1;
        nota_de_cancion(h, nota_de_haz(h, &aj), 230, 80);
        t_sig = t + paso * 7 / 10;
      } else {                                 /* terminó de mostrar: te toca */
        fase = F_TURNO;
        pos = 0;
        luz = 0xFF;
        t_sig = t;
      }
    } else if (fase == F_BIEN) simon_ronda();
  } else if (juego == J_REFLEJOS) {
    if (fase == F_ESPERA) {                    /* prende un haz al azar y arranca el cronómetro */
      objetivo = azar(N_HACES);
      luz = (uint8_t)(1 << objetivo);
      fase = F_RONDA;
      t_luz = t;
    }
  } else if (fase == F_ESPERA) {               /* duelo: un haz de cada lado, al mismo tiempo */
    luz = (uint8_t)((1 << azar(4)) | (0x10 << azar(4)));
    gano = 0;
    fase = F_RONDA;
  }
}

void jue_corte(uint8_t haz) {
  uint32_t t = ahora();
  if (juego == J_SIMON) {
    if (fase != F_TURNO) return;
    golpe(haz);
    if (haz == secuencia[pos]) {
      if (++pos >= largo) { fase = F_BIEN; t_sig = t + 900; }
    } else {
      fase = F_FIN;
      error();
      if ((uint8_t)(largo - 1) > record_simon) { record_simon = (uint8_t)(largo - 1); mem_guardar(MEM_AJUSTES); }
    }
  } else if (juego == J_REFLEJOS) {
    if (fase != F_RONDA || haz != objetivo) return;
    ultimo = (uint16_t)(t - t_luz);
    golpe(haz);
    tiempos += ultimo;
    luz = 0;
    if (++intento >= 5) {
      promedio = tiempos / 5;
      fase = F_FIN;
      if (!record_reflejos || promedio < record_reflejos) { record_reflejos = promedio; mem_guardar(MEM_AJUSTES); }
    } else { fase = F_ESPERA; t_sig = t + 1000 + azar(200) * 10; }
  } else if (juego == J_DUELO) {
    if (fase != F_RONDA) return;
    golpe(haz);
    if (haz < 4) { izq++; gano = 1; } else { der++; gano = 2; }
    luz = 0;
    if (izq >= 5 || der >= 5) fase = F_FIN;
    else { fase = F_ESPERA; t_sig = t + 1200 + azar(180) * 10; }
  }
}

void jue_pantalla(void) {
  if (juego == J_NADA) {
    lcd_fila_P(0, PSTR("Juegos   nivel"));
    lcd_car(0, 15, (char)('0' + aj.nivel));
    lcd_fila_P(1, PSTR("Simon     Refl     ms"));
    lcd_numero(1, 6, record_simon, 2);
    lcd_numero(1, 14, record_reflejos, 4);
    lcd_fila_P(2, PSTR("1Simon 2Reflej 3Duo"));
    lcd_fila_P(3, ((uint8_t)(ahora() / 3000) & 1) ? PSTR("*# nivel   0 salir") : PSTR("4 Arpa Hero (PC)"));
  } else if (juego == J_SIMON) {
    lcd_fila_P(0, PSTR("Simon laser"));
    if (fase == F_FIN) { lcd_fila_P(1, PSTR("Fallaste:    puntos")); lcd_numero(1, 10, (uint8_t)(largo - 1), 2); }
    else {
      lcd_fila_P(1, fase == F_MUESTRA ? PSTR("Ronda    mira") : fase == F_TURNO ? PSTR("Ronda    repite") : PSTR("Ronda    bien!"));
      lcd_numero(1, 6, largo, 2);
    }
    lcd_fila_P(2, PSTR("Record"));
    lcd_numero(2, 7, record_simon, 2);
    lcd_fila_P(3, PSTR("1 de nuevo   0 salir"));
  } else if (juego == J_REFLEJOS) {
    lcd_fila_P(0, PSTR("Reflejos  /5"));
    lcd_car(0, 9, (char)('0' + intento));
    if (fase == F_FIN) { lcd_fila_P(1, PSTR("Promedio      ms")); lcd_numero(1, 9, promedio, 4); }
    else if (fase == F_RONDA) lcd_fila_P(1, PSTR("Corta el haz!"));
    else if (ultimo) { lcd_fila_P(1, PSTR("Ultimo      ms")); lcd_numero(1, 7, ultimo, 4); }
    else lcd_fila_P(1, PSTR("Espera la luz..."));
    lcd_fila_P(2, PSTR("Record      ms"));
    lcd_numero(2, 7, record_reflejos, 4);
    lcd_fila_P(3, PSTR("2 de nuevo   0 salir"));
  } else {
    lcd_fila_P(0, PSTR("Duelo: 2 jugadores"));
    lcd_fila_P(1, PSTR("Izq     Der"));
    lcd_car(1, 4, (char)('0' + izq));
    lcd_car(1, 12, (char)('0' + der));
    lcd_fila_P(2, fase == F_FIN ? PSTR("Fin del duelo") : gano == 1 ? PSTR("Punto: izquierda") : gano == 2 ? PSTR("Punto: derecha") :
                  fase == F_RONDA ? PSTR("Corta tu haz!") : PSTR("Espera la luz..."));
    lcd_fila_P(3, PSTR("A 5 puntos   0 salir"));
  }
}
