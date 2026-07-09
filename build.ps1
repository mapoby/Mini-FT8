$env:IDF_PATH = "C:\Espressif\esp-idf-v5.5.1"
$env:PATH = "C:\Users\mpo\.espressif\python_env\idf5.5_py3.13_env\Scripts;" + `
             "C:\Users\mpo\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;" + `
             "C:\Users\mpo\.espressif\tools\ninja\1.12.1;" + `
             "C:\Users\mpo\.espressif\tools\cmake\3.30.2\bin;" + `
             $env:PATH

Set-Location C:\GitHub\Mini-FT8

Write-Host "Building firmware..."
python $env:IDF_PATH\tools\idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed with exit code $LASTEXITCODE"
    exit 1
}

Write-Host "`nFlashing to COM12..."
python $env:IDF_PATH\tools\idf.py -p COM12 flash
if ($LASTEXITCODE -ne 0) {
    Write-Host "Flash failed with exit code $LASTEXITCODE"
    exit 1
}

Write-Host "`nBuild and flash completed successfully!"
