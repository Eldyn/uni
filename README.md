# 🃏 Uni-Uno

Un'implementazione multigiocatore in tempo reale del gioco di carte **UNO**, sicura
(HTTPS/WSS), con **backend in C++23** (uWebSockets), persistenza su **SQLite** e
**frontend in Svelte 5**.

I giocatori possono registrarsi, creare o unirsi a lobby (anche tramite codice di
invito a 6 caratteri), configurare le regole della partita e giocare contro altri
utenti o contro dei bot.

### ✨ Funzionalità principali

- **Autenticazione** con email/password, sessione via cookie HttpOnly e token JWT (HS256).
- **Lobby** pubbliche/private, gestione dei membri (host, promozione, espulsione),
  riconnessione automatica dopo una disconnessione.
- **Motore di gioco** estendibile basato su una coda di _effetti_ e _regole_, con
  supporto a mod opzionali: _Draw Stacking_, _Progressive_, _Force Play_, _Jump In_,
  _No Bluffing_, _Seven-Zero_.
- **Bot** con euristiche, usati anche per gestire i turni in caso di inattività (AFK).
- **Animazioni** delle carte (volo dal mazzo alla mano e agli scarti, contatore `+N`
  per le penalità accumulate).
- **Salvataggio e ripresa** di partite interrotte, **statistiche** personali e
  **classifica** globale.

---

## 🛠️ Prerequisiti di Sistema

### Backend (C++)

- Compilatore C++ con supporto a **C++23** (GCC o Clang)
- **Python 3** (con `pip`) — usato per installare Conan e Ninja
- **CMake**
- **OpenSSL** — per generare i certificati TLS

### Frontend (Svelte)

- **Node.js** e **npm**

_Installazione dei tool di base:_

#### 🐧 Linux

- **Fedora/RHEL**: `sudo dnf install gcc-c++ python3 pip cmake openssl nodejs npm`
- **Ubuntu/Debian**: `sudo apt install g++ python3 python3-pip cmake openssl nodejs npm`

#### 🪟 Windows

1. **Compilatore**: [Visual Studio 2022](https://visualstudio.microsoft.com/) con il
   carico di lavoro _"Sviluppo di applicazioni desktop con C++"_.
2. **Python**: da [python.org](https://www.python.org/downloads/) o dal Microsoft Store.
3. **CMake**: dal [sito ufficiale](https://cmake.org/download/).
4. **Node.js** (include npm): da [nodejs.org](https://nodejs.org/).
5. **OpenSSL**: incluso in **Git Bash**, oppure installabile con
   `winget install ShiningLight.OpenSSL.Light` (o `choco install openssl`).

---

## 🚀 Setup e Build

### 1. Clona il repository

```bash
git clone https://github.com/Eldyn/uni-uno.git
cd uni-uno
```

### 2. Genera i certificati SSL

Il server usa HTTPS/WSS, quindi servono i file `key.pem` e `cert.pem` nella root del
progetto. Per uno sviluppo locale è sufficiente un certificato self-signed per `localhost`.

#### 🐧 Linux / macOS / Git Bash

```bash
openssl req -newkey rsa:2048 -nodes -x509 -days 365 \
  -keyout key.pem -out cert.pem \
  -subj "/C=IT/ST=Italy/L=Uniba/O=uni-uno/CN=localhost"
```

#### 🪟 Windows (PowerShell)

Con **OpenSSL** installato e disponibile nel PATH (vedi prerequisiti), esegui nella
root del progetto:

```powershell
openssl req -newkey rsa:2048 -nodes -x509 -days 365 `
  -keyout key.pem -out cert.pem `
  -subj "/C=IT/ST=Italy/L=Uniba/O=uni-uno/CN=localhost"
```

> In alternativa, apri **Git Bash** nella cartella del progetto e usa lo stesso comando
> della sezione Linux.

### 3. Installazione delle dipendenze (automatico)

Esegui lo script di setup per il tuo sistema: verifica la presenza di Python, installa
**Conan** e **Ninja**, rileva il profilo di compilazione e scarica tutte le librerie
del backend.

```powershell
# Windows
./setup.ps1
```

```bash
# Linux / macOS
chmod +x setup.sh
./setup.sh
```

### 4. Build del frontend

Compila il frontend Svelte e copia il risultato nella cartella `public/` servita dal server:

```bash
./deploy_frontend.sh
```

> Lo script esegue `npm install`/`npm run build` nella cartella `frontend/`. Vedi
> [`frontend/README.md`](frontend/README.md) per i dettagli e per la modalità di sviluppo.

### 5. Build del backend

Usa i preset CMake generati automaticamente da Conan:

```bash
# Configura
cmake --preset conan-release
# Compila
cmake --build --preset conan-release
```

> **Nota per gli sviluppatori (clangd / LSP):** Conan genera `compile_commands.json`
> in `build/Release/generators/`. Se il tuo editor lo richiede nella root del progetto:
>
> - Linux: `ln -s build/Release/generators/compile_commands.json`
> - Windows: `New-Item -ItemType SymbolicLink -Path "compile_commands.json" -Target "build\Release\generators\compile_commands.json"`

---

## 🏃 Esecuzione

L'eseguibile compilato si trova nella cartella di build:

```bash
# Linux / macOS
./build/Release/uno_server

# Windows
build\Release\uno_server.exe
```

CMake copia automaticamente `cert.pem` e `key.pem` accanto all'eseguibile. Assicurati
che il database SQLite (`game.db`) e la cartella `public/` del frontend siano
raggiungibili dal percorso in cui avvii il server. Per impostazione predefinita il
server è raggiungibile su **https://localhost:9999** (accetta l'avviso del browser
sul certificato self-signed).

---

## 📚 Documentazione

La documentazione completa è già allegata come **`documentazione-completa.pdf`** (analisi,
casi d'uso e test del frontend + manuale del backend Doxygen + API del frontend TypeDoc).

Per **rigenerarla** servono `doxygen`, `xelatex` (TeX Live), `mutool` (MuPDF) e Node.js;
quindi:

```bash
./docs/build-full-docs.sh
```
