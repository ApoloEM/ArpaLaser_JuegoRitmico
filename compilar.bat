@echo off
rem Compila el programa del arpa con el avr-gcc de Microchip Studio y deja arpa.hex.
rem -Os -flto: el programa ocupa 30 KB de los 31.5 que deja el bootloader. La interrupcion del audio se compila
rem siempre con -O2 (lo pide comun\latido.c), asi que aqui no hace falta cambiar nada.
cd /d "%~dp0"
set TC=C:\Program Files (x86)\Atmel\Studio\7.0\toolchain\avr8\avr8-gnu-toolchain\bin
set COMUN=..\comun\latido.c ..\comun\sonido.c ..\comun\tablas.c ..\comun\twi.c ..\comun\lcd.c ..\comun\teclado.c ..\..\juego\firmware\juego_serie.c
"%TC%\avr-gcc" -mmcu=atmega328p -DF_CPU=16000000UL -Os -flto -mrelax -std=gnu99 -Wall -Wextra -I. -I..\comun -I..\..\juego\firmware -o arpa.elf main.c pantalla.c canciones.c grabar.c juegos.c memoria.c canciones_datos.c %COMUN% || goto :fin
"%TC%\avr-objcopy" -O ihex -R .eeprom arpa.elf arpa.hex
"%TC%\avr-size" arpa.elf
echo Flash: text + data debe quedar abajo de 32256. RAM: data + bss abajo de 1500.
:fin
pause
