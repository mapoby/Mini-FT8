#!/usr/bin/env python3
import subprocess
import os
import sys

# Set up ESP-IDF environment
os.environ['IDF_PATH'] = 'C:\\Espressif\\esp-idf-v5.5.1'
os.environ['IDF_TOOLS_PATH'] = 'C:\\Users\\mpo\\.espressif'

# Add tools to PATH
tools_path = [
    'C:\\Users\\mpo\\.espressif\\tools\\xtensa-esp-elf\\esp-14.2.0_20241119\\xtensa-esp-elf\\bin',
    'C:\\Users\\mpo\\.espressif\\tools\\ninja\\1.12.1',
    'C:\\Users\\mpo\\.espressif\\tools\\cmake\\3.30.2\\bin',
    'C:\\Espressif\\esp-idf-v5.5.1\\tools',
]
os.environ['PATH'] = os.pathsep.join(tools_path) + os.pathsep + os.environ['PATH']

os.chdir('C:\\GitHub\\Mini-FT8')

print("Building firmware...")
result = subprocess.run([sys.executable, 'C:\\Espressif\\esp-idf-v5.5.1\\tools\\idf.py', 'build'],
                       capture_output=False)

if result.returncode != 0:
    print(f"Build failed with exit code {result.returncode}")
    sys.exit(1)

print("\nFlashing to COM12...")
result = subprocess.run([sys.executable, 'C:\\Espressif\\esp-idf-v5.5.1\\tools\\idf.py', '-p', 'COM12', 'flash'],
                       capture_output=False)

if result.returncode != 0:
    print(f"Flash failed with exit code {result.returncode}")
    sys.exit(1)

print("\nBuild and flash completed successfully!")
