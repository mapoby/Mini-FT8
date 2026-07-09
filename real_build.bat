@echo off
set IDF_PATH=C:\Espressif\esp-idf-v5.5.1
set PATH=C:\Users\mpo\.espressif\python_env\idf5.5_py3.13_env\Scripts;C:\Users\mpo\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\Users\mpo\.espressif\tools\ninja\1.12.1;C:\Users\mpo\.espressif\tools\cmake\3.30.2\bin;%PATH%
cd /d C:\GitHub\Mini-FT8
python %IDF_PATH%\tools\idf.py build
if errorlevel 1 exit /b 1
python %IDF_PATH%\tools\idf.py -p COM12 flash
