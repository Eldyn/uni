# 🃏 Uni-Uno

Un'implementazione sicura (HTTPS/WSS) del gioco UNO, con backend in C++, database SQLite per la persistenza e frontend in Svelte.

---

## 🛠️ Prerequisiti di Sistema

Scarica i tool:

### 🐧 Linux (Fedora/Ubuntu)

- Compilatore C++ (GCC o Clang)
- **Python 3** (con `pip`)
- **CMake**

_Per installarli:_

- **Fedora/RHEL**: `sudo dnf install gcc-c++ python3 pip cmake`
- **Ubuntu/Debian**: `sudo apt install g++ python3 python3-pip cmake`

### 🪟 Windows

1. **Compilatore**: [Visual Studio 2022](https://visualstudio.microsoft.com/) (assicurati di aver installato il carico di lavoro "Sviluppo di applicazioni desktop con C++").
2. **Python**: Scaricabile da [python.org](https://www.python.org/downloads/) o dal Microsoft Store.
3. **CMake**: Scaricabile dal [sito ufficiale](https://cmake.org/download/).

---

## 🚀 Setup e Build

**1. Clona il repository**

```bash
git clone [https://github.com/Eldyn/uni-uno.git](https://github.com/Eldyn/uni-uno.git)
cd uni-uno
```

**2. Genera i certificati SSL**
Usa OpenSSL (su Windows puoi usare Git Bash o installarlo a parte) per generare i certificati necessari al server HTTPS/WSS.
Esegui questo comando nella root del progetto:

```bash
openssl req -newkey rsa:2048 -new -nodes -x509 -days 365 -keyout key.pem -out cert.pem -subj "/C=IT/ST=Italy/L=Uniba/O=uni-uno/CN=localhost"
```

**3. Installazione Dipendenze (Automatico)**
Esegui lo script di setup relativo al tuo sistema. Lo script verificherà la presenza di Python, installerà automaticamente Conan e Ninja, configurerà i profili di compilazione e scaricherà tutte le librerie necessarie.

```bash
./setup.ps1 # Windows
# Linux
chmod +x setup.sh
./setup.sh
```

**4. Compilazione**
Una volta completato il setup, usa i preset di CMake (generati automaticamente da Conan) per compilare il progetto.

```bash
# Opzionale: se hai bisogno di preparare i file del frontend
./deploy_frontend.sh

# Configura backend C++
cmake --preset conan-release
# Compila ed esegui
cmake --build --preset conan-release && build\Release\uno_server.exe
```

Nota per gli sviluppatori (clangd / LSP): Conan genera automaticamente il file compile_commands.json all'interno della cartella build/Release/generators/. Se il tuo editor lo richiede nella root del progetto, puoi collegarlo così:
Linux: `ln -s build/Release/generators/compile_commands.json`
Windows: `New-Item -ItemType SymbolLink -Path "compile_commands.json" -Target "build\Release\generators\compile_commands.json"`

## 🏃‍♂️ Esecuzione

L'eseguibile compilato si troverà all'interno della cartella di build (es. build/Release/uno_server o build/Release/uno_server.exe).

Importante: CMake si occuperà automaticamente di copiare i file cert.pem e key.pem nella cartella in cui risiede l'eseguibile. Assicurati che il file del database SQLite e la cartella del frontend siano accessibili dal percorso in cui avvii il server!
