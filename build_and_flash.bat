@echo off
call C:\Espressif\esp-idf-v5.5.1\export.bat
cd /d C:\GitHub\Mini-FT8
idf.py build
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
idf.py flash
