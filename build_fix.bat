@echo off
call C:\Espressif\esp-idf-v5.5.1\export.bat
cd C:\GitHub\Mini-FT8
idf.py build
if errorlevel 1 exit /b 1
idf.py -p COM12 flash
