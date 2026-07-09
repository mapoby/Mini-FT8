call C:\Espressif\esp-idf-v5.5.1\export.bat
cd /d C:\GitHub\Mini-FT8
idf.py build
idf.py -p COM12 flash
