@echo off
rem Carga arpa.hex al UNO por su bootloader. Uso: cargar.bat [COM5]
rem Cierra antes el monitor, Arpa Hero o cualquier programa que tenga abierto el puerto.
cd /d "%~dp0"
set PUERTO=%1
if "%PUERTO%"=="" set PUERTO=COM5
set AVRDUDE=%LOCALAPPDATA%\Arduino15\packages\arduino\tools\avrdude
for /d %%d in ("%AVRDUDE%\*") do set AD=%%d
"%AD%\bin\avrdude.exe" -C "%AD%\etc\avrdude.conf" -p atmega328p -c arduino -P %PUERTO% -b 115200 -D -U flash:w:arpa.hex:i
pause
