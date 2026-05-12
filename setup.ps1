# Setup script per Windows (PowerShell)
Write-Host "Verifica e installazione strumenti (Conan, Ninja)..." -ForegroundColor Cyan

# Verifica presenza di Python
if (!(Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Error "Python non trovato! Devi installare Python prima di continuare."
    exit 1
}

# Installa/Aggiorna Conan e Ninja automaticamente
Write-Host "Installazione/Aggiornamento di Conan e Ninja via pip..." -ForegroundColor DarkGray
python -m pip install --upgrade pip conan ninja

# Funzione intelligente per aggirare il problema del PATH di Windows
function Invoke-Conan {
    if (Get-Command conan -ErrorAction SilentlyContinue) {
        conan $args
    } elseif (Get-Command python -ErrorAction SilentlyContinue) {
        $check = python -m conan --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            python -m conan $args
        } else {
            Write-Error "Modulo Conan non trovato. L'installazione di pip è fallita."
            exit 1
        }
    } else {
        Write-Error "Comando non trovato."
        exit 1
    }
}

Write-Host "`nConfigurazione profilo Conan..." -ForegroundColor Cyan
Invoke-Conan profile detect --force

Write-Host "`nInstallazione dipendenze in corso..." -ForegroundColor Cyan
# IMPORTANTE: Rimosso --output-folder. Ci pensa il cmake_layout del conanfile.
Invoke-Conan install . --build=missing -s build_type=Release -s compiler.cppstd=20 -c tools.cmake.cmaketoolchain:generator=Ninja

Write-Host "`nSetup completato con successo!" -ForegroundColor Green
Write-Host "I file di build sono stati generati correttamente in un'unica cartella."
Write-Host "Ora puoi compilare il progetto con:"
Write-Host "cmake --preset conan-default"
Write-Host "cmake --build --preset conan-default"
