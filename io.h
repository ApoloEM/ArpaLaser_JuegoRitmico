/* Registros de mentira del ATmega328P para correr el firmware del arpa en la PC (ver ../simulador.c). */
#pragma once
#include <stdint.h>

extern uint8_t ADCH, ADCSRA, ADMUX, DIDR0, SPCR, PORTB, PORTD, DDRB, DDRD, PINB, PIND, SREG, MCUSR;
extern uint8_t TCCR0A, TCCR0B, OCR0A, TIMSK0, TCCR1A, TCCR1B, TIMSK1, EICRA, EIMSK;
extern uint8_t UCSR0A, UCSR0B, UCSR0C, UDR0, TWSR, TWBR, TWCR, TWDR;
extern uint16_t OCR1A, UBRR0;
uint8_t *sim_spdr(void);                       /* cada escritura a SPDR y cada lectura de SPSR pasan por el simulador */
uint8_t *sim_spsr(void);
#define SPDR (*sim_spdr())
#define SPSR (*sim_spsr())
#define __int24 int32_t

enum { ADSC = 6, ADEN = 7, ADPS2 = 2, ADPS0 = 0, REFS0 = 6, ADLAR = 5, SPIF = 7, SPE = 6, MSTR = 4, SPI2X = 0,
       PB0 = 0, PB1 = 1, PB2 = 2, PB3 = 3, PB5 = 5, PD2 = 2, PD3 = 3, PD7 = 7,
       WGM01 = 1, CS01 = 1, CS00 = 0, OCIE0A = 1, WGM12 = 3, CS10 = 0, OCIE1A = 1, ISC00 = 0, INT0 = 0,
       U2X0 = 1, UCSZ01 = 2, UCSZ00 = 1, RXEN0 = 4, TXEN0 = 3, RXCIE0 = 7, UDRIE0 = 5,
       TWINT = 7, TWSTA = 5, TWSTO = 4, TWEN = 2 };
