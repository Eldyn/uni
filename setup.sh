#!/bin/bash
# =============================================================================
#  Uni-Uno — Setup e build completo (Linux / macOS)
#
#  Eseguendo questo script da una checkout pulita ottieni un eseguibile pronto
#  all'uso. In sequenza:
#    1. verifica i prerequisiti di sistema;
#    2. genera certificati TLS self-signed (se mancanti);
#    3. genera il file .env con dei segreti casuali (se mancante);
#    4. compila il frontend Svelte in public/;
#    5. installa Conan + Ninja e scarica le dipendenze del backend;
#    6. configura e compila il backend;
#    7. CMake copia certificati, .env e public/ accanto all'eseguibile.
# =============================================================================
set -euo pipefail

# Lavora sempre dalla cartella dello script (la root del progetto).
cd "$(dirname "$0")"

c_info()  { echo -e "\e[36m$*\e[0m"; }
c_step()  { echo -e "\n\e[36m==> $*\e[0m"; }
c_ok()    { echo -e "\e[32m$*\e[0m"; }
c_warn()  { echo -e "\e[33m$*\e[0m"; }
c_err()   { echo -e "\e[31m$*\e[0m" >&2; }

# -----------------------------------------------------------------------------
# 1. Prerequisiti
# -----------------------------------------------------------------------------
c_step "Verifica dei prerequisiti..."

missing=0
require() {
    if ! command -v "$1" &>/dev/null; then
        c_err "  [MANCANTE] $1 — $2"
        missing=1
    else
        echo "  [ok] $1"
    fi
}

# pip può chiamarsi pip3 o pip: lo gestiamo a parte.
if command -v pip3 &>/dev/null; then PIP_CMD="pip3"
elif command -v pip &>/dev/null; then PIP_CMD="pip"
else
    c_err "  [MANCANTE] pip — installa Python 3 con pip"
    missing=1
fi

require python3 "installa Python 3 (pacchetto: python3)"
require cmake   "installa CMake (pacchetto: cmake)"
require openssl "necessario per generare i certificati TLS (pacchetto: openssl)"
require node    "installa Node.js (pacchetto: nodejs)"
require npm     "installa npm (pacchetto: npm)"
if ! command -v g++ &>/dev/null && ! command -v clang++ &>/dev/null; then
    c_err "  [MANCANTE] g++/clang++ — serve un compilatore C++23 (pacchetto: gcc-c++ o clang)"
    missing=1
else
    echo "  [ok] compilatore C++"
fi

if [ "$missing" -ne 0 ]; then
    c_err "\nMancano dei prerequisiti. Installali e riesegui questo script."
    c_err "  Fedora/RHEL : sudo dnf install gcc-c++ python3 pip cmake openssl nodejs npm"
    c_err "  Ubuntu/Debian: sudo apt install g++ python3 python3-pip cmake openssl nodejs npm"
    exit 1
fi
c_ok "Tutti i prerequisiti sono presenti."

# -----------------------------------------------------------------------------
# 2. Certificati TLS self-signed
# -----------------------------------------------------------------------------
c_step "Certificati TLS..."
if [ -f cert.pem ] && [ -f key.pem ]; then
    echo "  cert.pem e key.pem già presenti — salto la generazione."
else
    echo "  Genero un certificato self-signed per localhost (valido 365 giorni)..."
    openssl req -newkey rsa:2048 -nodes -x509 -days 365 \
        -keyout key.pem -out cert.pem \
        -subj "/C=IT/ST=Italy/L=Uniba/O=uni-uno/CN=localhost"
    c_ok "  Certificati generati."
fi

# -----------------------------------------------------------------------------
# 3. File .env con segreti
# -----------------------------------------------------------------------------
c_step "File di ambiente (.env)..."
if [ -f .env ]; then
    echo "  .env già presente — lo lascio invariato."
else
    echo "  Genero .env con segreti casuali..."
    JWT_SECRET=$(openssl rand -hex 32)
    PASSWORD_PEPPER=$(openssl rand -hex 32)
    {
        echo "JWT_SECRET=${JWT_SECRET}"
        echo "PASSWORD_PEPPER=${PASSWORD_PEPPER}"
    } > .env
    c_ok "  .env generato."
fi

# -----------------------------------------------------------------------------
# 4. Frontend (deve esistere prima della configurazione di CMake)
# -----------------------------------------------------------------------------
c_step "Build del frontend Svelte..."
# Vite è configurato con outDir "../public" ed emptyOutDir: la build scrive
# direttamente nella cartella public/ alla root del progetto (e la ripulisce).
( cd frontend && npm install && npm run build )
c_ok "  Frontend compilato in public/."

# -----------------------------------------------------------------------------
# 5. Toolchain backend (Conan + Ninja)
# -----------------------------------------------------------------------------
c_step "Installazione di Conan e Ninja via pip..."
$PIP_CMD install --upgrade --user conan ninja

# Determina come invocare Conan aggirando i problemi di PATH.
if command -v conan &>/dev/null; then
    CONAN_CMD="conan"
elif python3 -m conan --version &>/dev/null; then
    CONAN_CMD="python3 -m conan"
elif [ -x "$HOME/.local/bin/conan" ]; then
    CONAN_CMD="$HOME/.local/bin/conan"
else
    c_err "Impossibile trovare l'eseguibile di Conan dopo l'installazione."
    c_err "Aggiungi ~/.local/bin al PATH e riprova."
    exit 1
fi

c_step "Configurazione del profilo Conan..."
$CONAN_CMD profile detect --force

c_step "Download delle dipendenze del backend..."
$CONAN_CMD install . --build=missing -s build_type=Release -s compiler.cppstd=20 \
    -c tools.cmake.cmaketoolchain:generator=Ninja

# -----------------------------------------------------------------------------
# 6. Build del backend
# -----------------------------------------------------------------------------
c_step "Configurazione e compilazione del backend..."
cmake --preset conan-release
cmake --build --preset conan-release

# -----------------------------------------------------------------------------
# Fatto.
# -----------------------------------------------------------------------------
c_ok "\nSetup completato con successo!"
echo "Avvia il server con:"
echo "  ./build/Release/uno_server          (dalla root del progetto)"
echo "oppure"
echo "  cd build/Release && ./uno_server    (asset copiati accanto all'eseguibile)"
echo "Poi apri https://localhost:9999 (accetta l'avviso sul certificato self-signed)."
