#!/bin/bash
set -e

echo -e "\e[36mVerifica e installazione strumenti (Conan, Ninja)...\e[0m"

# Trova la versione corretta di pip
if command -v pip3 &> /dev/null; then
    PIP_CMD="pip3"
elif command -v pip &> /dev/null; then
    PIP_CMD="pip"
else
    echo -e "\e[31mPython/pip non trovato! Installa Python prima di continuare.\e[0m"
    exit 1
fi

# Installa/Aggiorna Conan e Ninja automaticamente per l'utente corrente
echo -e "\e[90mInstallazione/Aggiornamento di Conan e Ninja via pip...\e[0m"
$PIP_CMD install --upgrade --user conan ninja

# Determina come chiamare Conan aggirando i problemi di PATH
if command -v conan &> /dev/null; then
    CONAN_CMD="conan"
elif python3 -m conan --version &> /dev/null; then
    CONAN_CMD="python3 -m conan"
elif [ -x "$HOME/.local/bin/conan" ]; then
    CONAN_CMD="$HOME/.local/bin/conan"
else
    echo -e "\e[31mImpossibile trovare l'eseguibile di Conan dopo l'installazione.\e[0m"
    exit 1
fi

echo -e "\n\e[36mConfigurazione profilo Conan...\e[0m"
$CONAN_CMD profile detect --force

echo -e "\n\e[36mInstallazione dipendenze in corso...\e[0m"
# IMPORTANTE: Rimosso --output-folder. Ci pensa il cmake_layout del conanfile.
$CONAN_CMD install . --build=missing -s build_type=Release -s compiler.cppstd=20 -c tools.cmake.cmaketoolchain:generator=Ninja

echo -e "\e[32m\nSetup completato con successo!\e[0m"
echo "I file di build sono stati generati correttamente in un'unica cartella."
echo "Ora puoi compilare il progetto con:"
echo "cmake --preset conan-default"
echo "cmake --build --preset conan-default"
