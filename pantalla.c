/*
 * pantalla.c — lo que muestra la LCD de 20 × 4 en cada modo. Se llama 10 veces por segundo; sólo escribe en la
 * copia en RAM y lcd_tarea() va mandando lo que cambió.
 *
 * Modo Tocar:   fila 1  la nota que sonó: nombre en español, en inglés y frecuencia («Sol5  G5   784.0Hz»)
 *               fila 2  sonido, tonalidad, escala y octava («Arpa     Do may O5»)
 *               fila 3  una barra doble por haz con el volumen de su nota; al final N (neblina), S (sostener), ! (haz apagado)
 *               fila 4  ayuda que va rotando cada 3 s
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "arpa.h"
#include "lcd.h"

const char NOMBRE_SONIDO[4][9] PROGMEM = {"Arpa", "Piano", "Guitarra", "Bateria"};
static const char NATURAL[7][4] PROGMEM = {"Do", "Re", "Mi", "Fa", "Sol", "La", "Si"};
static const char NATURAL_EN[7] PROGMEM = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
static const uint8_t ABAJO[12] PROGMEM = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};      /* la nota natural igual o debajo de cada semitono */
static const char ESCALA_CORTA[3][5] PROGMEM = {"may", "pent", "men"};
static const char TAMBOR[8][14] PROGMEM = {"Bombo", "Tarola", "Plat. cerrado", "Plat. abierto", "Palmas", "Tom bajo", "Tom alto", "Cencerro"};

static const char AY_T1[] PROGMEM = "1Ar 2Pi 3Gu 4Ba";
static const char AY_T2[] PROGMEM = "5May 6Pen 7Men 8Ton";
static const char AY_T3[] PROGMEM = "*# octava  9 neblina";
static const char AY_T4[] PROGMEM = "0 silencio, mant cal";
static PGM_P const AYUDA_TOCAR[4] PROGMEM = {AY_T1, AY_T2, AY_T3, AY_T4};

static uint32_t mensaje_hasta;
static uint8_t mensaje_fijo, con_mensaje;

void mensaje_P(PGM_P a, PGM_P b, PGM_P c, PGM_P d, uint16_t ms) {
  if (a) lcd_fila_P(0, a);
  if (b) lcd_fila_P(1, b);
  if (c) lcd_fila_P(2, c);
  if (d) lcd_fila_P(3, d);
  con_mensaje = 1;
  mensaje_fijo = ms == 0;
  mensaje_hasta = ahora() + ms;
}
void quitar_mensaje(void) { con_mensaje = 0; }
uint8_t hay_mensaje(void) { return con_mensaje; }

/* Las tonalidades con bemoles (Fa, Si♭, Mi♭, La♭ y Re♭) escriben las teclas negras como bemoles; las demás, como sostenidos. */
static uint8_t con_bemoles(uint8_t tonica) { return tonica == 5 || tonica == 10 || tonica == 3 || tonica == 8 || tonica == 1; }

/* Escribe el nombre del semitono pc (0 = Do) y devuelve la columna siguiente. */
static uint8_t nombre(uint8_t fila, uint8_t col, uint8_t pc, uint8_t bemol, uint8_t ingles) {
  uint8_t nat = pgm_read_byte(&ABAJO[pc]);
  uint8_t negra = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
  char c;
  if (negra && bemol) nat++;
  if (ingles) lcd_car(fila, col++, (char)pgm_read_byte(&NATURAL_EN[nat]));
  else {
    PGM_P s = NATURAL[nat];
    while ((c = (char)pgm_read_byte(s++)) != 0) lcd_car(fila, col++, c);
  }
  if (negra) lcd_car(fila, col++, bemol ? 'b' : '#');
  return col;
}

void pintar_barras(uint8_t fila) {
  uint8_t i;
  for (i = 0; i < N_HACES; i++) {
    char b = lcd_barra((uint8_t)((sonido_nivel(i) + 24) >> 5));
    lcd_car(fila, (uint8_t)(2 * i), b);
    lcd_car(fila, (uint8_t)(2 * i + 1), b);
  }
  lcd_car(fila, 16, ' ');
  lcd_car(fila, 17, neblina_prendida() ? 'N' : ' ');
  lcd_car(fila, 18, sostener() ? 'S' : ' ');
  lcd_car(fila, 19, haces_dormidos() ? '!' : ' ');
}

/* Fila 1: la última nota («♪ Sol5  G5   784.0Hz») o el último tambor. */
void pintar_nota(uint8_t fila, uint8_t tonica) {
  uint8_t col, bemol = con_bemoles(tonica);
  lcd_fila_P(fila, PSTR(""));
  if (ultimo_tambor != 0xFF) { lcd_car(fila, 0, LCD_NOTA); lcd_texto_P(fila, 2, TAMBOR[ultimo_tambor]); return; }
  if (!ultima_midi) { lcd_texto_P(fila, 0, PSTR("Corta un haz")); return; }
  lcd_car(fila, 0, LCD_NOTA);
  col = nombre(fila, 2, (uint8_t)(ultima_midi % 12), bemol, 0);
  lcd_car(fila, col, (char)('0' + ultima_midi / 12 - 1));
  col = nombre(fila, 8, (uint8_t)(ultima_midi % 12), bemol, 1);
  lcd_car(fila, col, (char)('0' + ultima_midi / 12 - 1));
  {
    uint16_t dhz = nota_decihertz(ultima_midi);
    if (dhz >= 10000) lcd_numero(fila, 14, dhz / 10, 4);
    else { lcd_numero(fila, 12, dhz / 10, 4); lcd_car(fila, 16, '.'); lcd_car(fila, 17, (char)('0' + dhz % 10)); }
    lcd_texto_P(fila, 18, PSTR("Hz"));
  }
}

static void pantalla_tocar(void) {
  uint8_t col;
  pintar_nota(0, aj.tonica);
  lcd_fila_P(1, NOMBRE_SONIDO[aj.sonido]);
  col = nombre(1, 9, aj.tonica, con_bemoles(aj.tonica), 0);
  lcd_texto_P(1, (uint8_t)(col + 1), ESCALA_CORTA[aj.escala]);
  lcd_car(1, 18, 'O');
  lcd_car(1, 19, (char)('0' + aj.octava));
  pintar_barras(2);
  if (sostener()) lcd_fila_P(3, PSTR("SOSTENER"));
  else lcd_fila_P(3, (PGM_P)pgm_read_ptr(&AYUDA_TOCAR[(uint8_t)(ahora() / 3000) & 3]));
}

void pantalla(void) {
  if (con_mensaje) {
    if (mensaje_fijo || (int32_t)(ahora() - mensaje_hasta) < 0) return;
    con_mensaje = 0;
  }
  switch (modo) {
    case M_TOCAR: pantalla_tocar(); break;
    case M_CANCIONES: can_pantalla(); break;
    case M_GRABAR: gra_pantalla(); break;
    case M_JUEGOS: jue_pantalla(); break;
    default:
      lcd_fila_P(0, PSTR("Arpa Hero (PC)"));
      lcd_fila_P(1, PSTR("Teclas -> juego"));
      lcd_fila_P(2, PSTR("La PC usa la LCD"));
      lcd_fila_P(3, PSTR("Manten D: salir"));
      break;
  }
}
