/*
 * latido.c — la interrupción de 16 kHz del arpa y lo que cuelga de ella (ver latido.h).
 *
 * Interrupciones de este archivo:
 *   TIMER1_COMPA  el latido: audio, ADC, láseres y detección (16 000 veces por segundo)
 *   TIMER0_COMPA  reloj de 1 ms
 *   INT0          botones de sostener (cualquier cambio en D2)
 *
 * Cómo se reparte un medio periodo de los láseres (MEDIO = 16 latidos = 1 ms):
 *   latido 0        cambia los láseres (prendidos en una mitad, apagados en la otra)
 *   latidos 1 y 2   una conversión auxiliar: perilla de volumen, perilla de decaimiento o fuente, por turnos
 *   latidos 3 a 7   espera: el PT334, saturado por su láser, tarda en apagarse (437 µs medidos en el banco)
 *   latidos 8 a 15  llegan las 8 lecturas de los sensores, una por latido
 * Un barrido completo (con láser y sin láser) dura 2 ms y un corte se decide en 2 barridos: 4 a 6 ms.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include "latido.h"
#include "tablas.h"

#define PIN_PEDAL   (1 << PD2)
#define PIN_OE      (1 << PD3)
#define MUX_BITS    0x70               /* D4, D5, D6 */
#define PIN_NEBLINA (1 << PD7)
#define PIN_RCLK    (1 << PB0)
#define CS_IZQ      (1 << PB1)         /* D9 */
#define CS_DER      (1 << PB2)         /* D10 (es también SS: debe ser salida para que el SPI siga de maestro) */
#define PIN_MOSI    (1 << PB3)
#define PIN_SCK     (1 << PB5)
#define ADMUX_BASE  ((1 << REFS0) | (1 << ADLAR))   /* referencia AVCC y resultado de 8 bits en ADCH */

#define MEDIO  16                      /* latidos por medio periodo */
#define ESPERA 7                       /* latidos entre el cambio de los láseres y la primera conversión de sensores */
#define FISICO(haz) (7 - (haz))        /* haz 0..7 (izquierda a derecha) → bit del 74HC595 y canal del CD4051 */

volatile voz_t voz[N_HACES];

/* ---- lo que comparten el latido y el lazo (los índices de aquí son FÍSICOS) ---- */
static volatile uint8_t modo_laser = LASER_APAGADO;
static volatile uint8_t haces_on;              /* haces habilitados */
static volatile uint8_t cortado;               /* un bit por haz, ya con histéresis */
static volatile uint8_t ev_corte, ev_libre;    /* cambios que el lazo todavía no recoge */
static volatile uint8_t barridos;
static volatile uint8_t con_laser[N_HACES], sin_laser[N_HACES], dif[N_HACES];
static volatile uint8_t ref50[N_HACES], ref70[N_HACES];
static uint8_t sin_antes[N_HACES], cuenta[N_HACES], ref_haz[N_HACES];
static volatile uint8_t aux[3];                /* volumen, decaimiento y fuente */
static uint8_t paso, canal_aux;
static uint8_t sal_ih = 0x38, sal_il, sal_dh = 0x38, sal_dl;   /* la muestra que sale en el próximo latido (0x3800 = 2.5 V) */
static volatile uint32_t ms;
static volatile uint8_t pedal_movido, pedal_ms;
static uint8_t pedal_estado;

#define ESPERA_SPI() while (!(SPSR & (1 << SPIF))) {}
#ifdef PRUEBA_PC                               /* en el simulador de la PC, esperar es dejar correr el tiempo simulado */
void pc_ocio(void);
#define OCIO() pc_ocio()
#else
#define OCIO()
#endif

/* Una voz: avanza su fase, lee su tabla y la suma a las dos bocinas. Una voz apagada (gi = gd = 0) no cuesta nada. */
#define VOZ(i)                                                              \
  do {                                                                      \
    uint8_t gi_ = voz[i].gi, gd_ = voz[i].gd;                               \
    if (gi_ | gd_) {                                                        \
      uint16_t f_ = voz[i].fase + voz[i].inc;                               \
      voz[i].fase = f_;                                                     \
      int8_t s_ = (int8_t)pgm_read_byte(voz[i].tabla + (uint8_t)(f_ >> 8)); \
      ai += (int16_t)(s_ * (int16_t)gi_);                                   \
      ad += (int16_t)(s_ * (int16_t)gd_);                                   \
    }                                                                       \
  } while (0)

ISR(TIMER1_COMPA_vect, __attribute__((optimize("O2")))) {   /* siempre -O2, aunque el proyecto se compile de otra forma */
#ifdef MEDIR_LATIDO
  PORTD |= PIN_NEBLINA;                      /* D7 sube al entrar y baja al salir: su ancho, visto en el osciloscopio, */
#endif                                       /* es lo que dura la interrupción (debe quedar abajo de 47 µs)            */
  /* 1. La muestra calculada en el latido anterior sale siempre en el mismo instante. Mientras cada byte viaja
   *    (16 ciclos a 8 MHz) se aprovecha para leer el ADC y preparar lo demás. */
  PORTB &= ~CS_IZQ;
  SPDR = sal_ih;
  uint8_t lectura = ADCH;                    /* la conversión que arrancó en el latido anterior */
  ADCSRA |= (1 << ADSC);                     /* arranca la siguiente: su canal ya está puesto */
  ESPERA_SPI();
  SPDR = sal_il;
  uint8_t p = paso;
  uint8_t mitad = p >= MEDIO;
  uint8_t m = mitad ? p - MEDIO : p;
  ESPERA_SPI();
  PORTB |= CS_IZQ;                           /* el MCP4921 izquierdo cambia su salida aquí */
  PORTB &= ~CS_DER;
  SPDR = sal_dh;
  uint8_t on = haces_on;
  uint8_t modo = modo_laser;
  ESPERA_SPI();
  SPDR = sal_dl;
  ESPERA_SPI();
  PORTB |= CS_DER;

  /* 2. Láseres. Los 8 parpadean juntos: así la luz de un haz vecino sólo puede sumar a la lectura «con láser». */
  if (m == 0) {
    uint8_t b = 0;
    if (modo == LASER_NORMAL) b = mitad ? 0 : on;
    else if (modo == LASER_FIJO) b = on;
    SPDR = b;                                /* los DAC no lo ven: sus CS están en alto */
    ESPERA_SPI();
    PORTB |= PIN_RCLK;
    PORTB &= ~PIN_RCLK;
  }

  /* 3. La lectura que llegó: un sensor (latidos 8 a 15) o la conversión auxiliar (latido 2). */
  if (m > ESPERA) {
    uint8_t h = m - ESPERA - 1;
    if (!mitad) con_laser[h] = lectura;      /* en esta mitad su láser estaba prendido */
    else {
      /* la luz del salón está en las dos lecturas: se resta el promedio de «sin láser» de antes y de después */
      uint8_t bit = (uint8_t)(1 << h);
      uint8_t base = (uint8_t)(((uint16_t)sin_antes[h] + lectura) >> 1);
      uint8_t c = con_laser[h];
      uint8_t d = c > base ? c - base : 0;
      sin_antes[h] = lectura;
      sin_laser[h] = lectura;
      dif[h] = d;
      if (modo == LASER_NORMAL && (on & bit)) {
        if (cortado & bit) {                 /* para liberarse debe pasar del 70 % dos barridos seguidos */
          if (d > ref70[h]) { if (++cuenta[h] >= 2) { cortado &= ~bit; ev_libre |= bit; cuenta[h] = 0; } } else cuenta[h] = 0;
        } else {                             /* para cortarse debe bajar del 50 % dos barridos seguidos */
          if (d < ref50[h]) { if (++cuenta[h] >= 2) { cortado |= bit; ev_corte |= bit; cuenta[h] = 0; } } else cuenta[h] = 0;
        }
      } else cuenta[h] = 0;
    }
  } else if (m == 2) aux[canal_aux] = lectura;

  /* 4. Canal de la conversión que arrancará en el PRÓXIMO latido. La de este latido tomó su muestra 3 µs después
   *    de ADSC y de eso ya pasaron más de 100 ciclos, así que cambiar aquí no mezcla dos lecturas. */
#ifdef PROTEUS                                 /* el ADC de Proteus muestrea al final de la conversión, no al inicio */
  if (m >= ESPERA && m <= ESPERA + 7) PORTD = (uint8_t)((PORTD & ~MUX_BITS) | ((m - ESPERA) << 4));
#else
  if (m >= ESPERA - 1 && m <= ESPERA + 6) PORTD = (uint8_t)((PORTD & ~MUX_BITS) | ((m - (ESPERA - 1)) << 4));
#endif
  else if (m == 0) ADMUX = (uint8_t)(ADMUX_BASE | (canal_aux + 1));          /* A1, A2 o A3 */
  else if (m == 2) { ADMUX = ADMUX_BASE; if (++canal_aux > 2) canal_aux = 0; } /* de regreso a A0 */

  if (++p >= 2 * MEDIO) { p = 0; barridos++; }
  paso = p;

  /* 5. La muestra siguiente: 8 voces. Ocho productos de 16 bits no caben en 16 bits: la suma va en 24. */
  __int24 ai = 0, ad = 0;
  VOZ(0); VOZ(1); VOZ(2); VOZ(3); VOZ(4); VOZ(5); VOZ(6); VOZ(7);
  int16_t si = (int16_t)((ai << 2) >> 8);    /* ÷ 64: escala completa con 4 voces al máximo; de ahí en adelante, satura */
  int16_t sd = (int16_t)((ad << 2) >> 8);
  if (si > 2047) si = 2047; else if (si < -2048) si = -2048;
  if (sd > 2047) sd = 2047; else if (sd < -2048) sd = -2048;
  si += 2048;
  sd += 2048;
  sal_ih = (uint8_t)(0x30 | (uint8_t)(si >> 8));   /* 0011: escribir, sin búfer, ganancia 1, salida activa */
  sal_il = (uint8_t)si;
  sal_dh = (uint8_t)(0x30 | (uint8_t)(sd >> 8));
  sal_dl = (uint8_t)sd;
#ifdef MEDIR_LATIDO
  PORTD &= (uint8_t)~PIN_NEBLINA;
#endif
}

ISR(TIMER0_COMPA_vect) { ms++; }

/* Botones de sostener: sólo se anota que D2 cambió y cuándo; el lazo lee el pin 15 ms después (rebotes). */
ISR(INT0_vect) {
  pedal_ms = (uint8_t)ms;
  pedal_movido = 1;
}

/* ------------------------------------------------------------------------------------------------ */
static uint8_t espejo(uint8_t m) {           /* máscara por haz (bit 0 = izquierda) ↔ máscara física: el mismo espejo */
  uint8_t r = 0, k;
  for (k = 0; k < 8; k++) if (m & (1 << k)) r |= (uint8_t)(0x80 >> k);
  return r;
}

static void spi_byte(uint8_t b) {
  SPDR = b;
  ESPERA_SPI();
}

void latido_iniciar(void) {
  MCUSR = 0;
  wdt_disable();

  PORTB = CS_IZQ | CS_DER;                   /* los dos DAC sin seleccionar */
  DDRB = PIN_RCLK | CS_IZQ | CS_DER | PIN_MOSI | PIN_SCK;
  PORTD = PIN_PEDAL;                         /* resistencia interna en D2; D3 (/OE) sigue como entrada: su 10 kΩ apaga los láseres */
  DDRD = MUX_BITS | PIN_NEBLINA;
  SPCR = (1 << SPE) | (1 << MSTR);           /* SPI maestro, modo 0 */
  SPSR = (1 << SPI2X);                       /* 16 MHz / 2 = 8 MHz */
  spi_byte(0);                               /* ceros al 74HC595 antes de habilitar sus salidas */
  PORTB |= PIN_RCLK;
  PORTB &= ~PIN_RCLK;
  DDRD |= PIN_OE;                            /* PORTD3 = 0: salidas del 74HC595 habilitadas */
  PORTB &= ~CS_IZQ; spi_byte(0x38); spi_byte(0); PORTB |= CS_IZQ;     /* los dos DAC a media escala: sin golpe al encender */
  PORTB &= ~CS_DER; spi_byte(0x38); spi_byte(0); PORTB |= CS_DER;

  ADMUX = ADMUX_BASE;                        /* canal A0 */
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS0);   /* 16 MHz / 32 = 500 kHz: 26 µs por conversión */
  DIDR0 = 0x0F;                              /* A0 a A3 sólo analógicos */

  TCCR0A = (1 << WGM01);                     /* Timer0 en CTC: 16 MHz / 64 / 250 = 1 kHz */
  OCR0A = 249;
  TCCR0B = (1 << CS01) | (1 << CS00);
  TIMSK0 = (1 << OCIE0A);

  TCCR1A = 0;                                /* Timer1 en CTC sin preescala: 16 MHz / 1000 = 16 kHz */
  OCR1A = 999;
  TCCR1B = (1 << WGM12) | (1 << CS10);
  TIMSK1 = (1 << OCIE1A);

  EICRA = (1 << ISC00);                      /* INT0 con cualquier cambio de D2 */
  EIMSK = (1 << INT0);
}

uint32_t ahora(void) {
  uint32_t t;
  uint8_t s = SREG;
  cli();
  t = ms;
  SREG = s;
  return t;
}

/* ---- láseres y sensores ---- */
void haces_modo(uint8_t modo) {
  modo_laser = modo;
  if (modo != LASER_NORMAL) {
    uint8_t s = SREG;
    cli();
    cortado = 0;
    ev_corte = 0;
    ev_libre = 0;
    SREG = s;
  }
}
uint8_t haces_modo_actual(void) { return modo_laser; }
void haces_prender(uint8_t mascara) { haces_on = espejo(mascara); }
uint8_t haces_prendidos(void) { return espejo(haces_on); }
uint8_t haces_cortados(void) { return espejo(cortado); }

uint8_t haces_eventos(uint8_t *cortes, uint8_t *libres) {
  uint8_t c, l, cor, s = SREG;
  cli();
  c = ev_corte;
  l = ev_libre;
  ev_corte = 0;
  ev_libre = 0;
  cor = cortado;
  SREG = s;
  *cortes = espejo(c);
  *libres = espejo(l);
  return espejo(cor);
}

void haces_olvidar(uint8_t mascara) {
  uint8_t f = espejo(mascara), s = SREG;
  cli();
  cortado &= (uint8_t)~f;
  ev_corte &= (uint8_t)~f;
  ev_libre &= (uint8_t)~f;
  SREG = s;
}

void haces_esperar(uint8_t n) {
  uint8_t b0 = barridos;
  while ((uint8_t)(barridos - b0) < n) { OCIO(); }
}

static void fijar_ref(uint8_t h, uint8_t r) {
  uint8_t s = SREG;
  cli();
  if (r < 16) { ref50[h] = 0; ref70[h] = 255; r = 0; }           /* no ve su láser: nunca cuenta como cortado */
  else { ref50[h] = r >> 1; ref70[h] = (uint8_t)(((uint16_t)r * 7) / 10); }
  cuenta[h] = 0;
  SREG = s;
  ref_haz[h] = r;
}

/* Mide la diferencia de cada haz sin manos (64 barridos) y fija los umbrales: cortado bajo el 50 %, libre arriba
 * del 70 %. Un haz con menos de 16 de diferencia no ve su láser y se deja fuera. */
uint8_t haces_calibrar(void) {
  uint16_t suma[N_HACES];
  uint8_t h, k, bien = 0;
  uint8_t on_antes = haces_on, modo_antes = modo_laser;
  haces_on = 0xFF;
  for (h = 0; h < N_HACES; h++) { suma[h] = 0; ref50[h] = 0; ref70[h] = 255; }
  haces_modo(LASER_NORMAL);
  haces_esperar(8);
  for (k = 0; k < 64; k++) {
    haces_esperar(1);
    for (h = 0; h < N_HACES; h++) suma[h] += dif[h];
  }
  for (h = 0; h < N_HACES; h++) {
    fijar_ref(h, (uint8_t)(suma[h] >> 6));
    if (ref_haz[h]) bien |= (uint8_t)(1 << h);
  }
  haces_on = on_antes;
  haces_modo(modo_antes == LASER_APAGADO ? LASER_NORMAL : modo_antes);
  haces_olvidar(0xFF);
  return espejo(bien);
}

/* Un haz tapado durante la calibración se queda sin referencia y no sonaría nunca: en cuanto ve su láser, la toma. */
uint8_t haces_rescatar(void) {
  uint8_t h, cambio = 0;
  if (modo_laser != LASER_NORMAL) return 0;
  for (h = 0; h < N_HACES; h++)
    if (ref_haz[h] == 0 && (haces_on & (1 << h)) && dif[h] >= 40) { fijar_ref(h, dif[h]); cambio |= (uint8_t)(1 << h); }
  return espejo(cambio);
}

void haces_lectura(uint8_t haz, uint8_t *con, uint8_t *sin, uint8_t *d, uint8_t *ref) {
  uint8_t h = FISICO(haz);
  *con = con_laser[h];
  *sin = sin_laser[h];
  *d = dif[h];
  *ref = ref_haz[h];
}

void haces_emergencia(uint8_t apagar) {
  if (apagar) PORTD |= PIN_OE;               /* salidas del 74HC595 en alta impedancia: el ULN2803A suelta los 8 láseres */
  else PORTD &= (uint8_t)~PIN_OE;
}

/* ---- perillas, fuente, botones y neblina ---- */
uint8_t perilla_volumen(void) { return aux[0]; }
uint8_t perilla_decaimiento(void) { return aux[1]; }
uint8_t fuente_potencia(void) { return aux[2]; }

uint8_t sostener(void) {
  if (pedal_movido && (uint8_t)((uint8_t)ms - pedal_ms) >= 15) {
    pedal_movido = 0;
    pedal_estado = !(PIND & PIN_PEDAL);      /* apretado = D2 a GND */
  }
  return pedal_estado;
}

void neblina(uint8_t prender) {
#ifdef MEDIR_LATIDO
  (void)prender;                             /* en esa compilación D7 es la punta de medición */
#else
  if (prender) PORTD |= PIN_NEBLINA;
  else PORTD &= (uint8_t)~PIN_NEBLINA;
#endif
}
uint8_t neblina_prendida(void) { return (PORTD & PIN_NEBLINA) != 0; }
