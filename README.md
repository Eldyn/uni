# 🃏 UNI

A secure (HTTPS/WSS), real-time multiplayer implementation of the **UNO** card game,
with a **C++23 backend** (uWebSockets), **SQLite** persistence and a **Svelte 5
frontend**.

Players can register, create or join lobbies (including via a 6-character invite
code), configure the match rules and play against other users or against bots.

### ✨ Key features

- **Authentication** with email/password, session via HttpOnly cookie and JWT token (HS256).
- **Public/private lobbies**, member management (host, promotion, kicking),
  automatic reconnection after a disconnection.
- **Extensible game engine** based on a queue of _effects_ and _rules_, with support
  for optional mods: _Draw Stacking_, _Progressive_, _Force Play_, _Jump In_,
  _No Bluffing_, _Seven-Zero_.
- **Bots** with heuristics, also used to handle turns in case of inactivity (AFK).
- **Card animations** (flight from the deck to the hand and to the discard pile, `+N`
  counter for accumulated penalties).
- **Saving and resuming** interrupted matches, personal **statistics** and a global
  **leaderboard**.

---

## 🛠️ System prerequisites

### Backend (C++)

- A C++ compiler with **C++23** support (GCC or Clang)
- **Python 3** (with `pip`) — used to install Conan and Ninja
- **CMake**
- **OpenSSL** — to generate the TLS certificates

### Frontend (Svelte)

- **Node.js** and **npm**

_Installing the base tools:_

#### 🐧 Linux

- **Fedora/RHEL**: `sudo dnf install gcc-c++ python3 pip cmake openssl nodejs npm`
- **Ubuntu/Debian**: `sudo apt install g++ python3 python3-pip cmake openssl nodejs npm`

#### 🪟 Windows

1. **Compiler**: [Visual Studio 2022](https://visualstudio.microsoft.com/) with the
   _"Desktop development with C++"_ workload.
2. **Python**: from [python.org](https://www.python.org/downloads/) or the Microsoft Store.
3. **CMake**: from the [official site](https://cmake.org/download/).
4. **Node.js** (includes npm): from [nodejs.org](https://nodejs.org/).
5. **OpenSSL**: included in **Git Bash**, or installable with
   `winget install ShiningLight.OpenSSL.Light` (or `choco install openssl`).

---

## 🚀 Setup and Build

### Clone the repository

```bash
git clone https://github.com/Eldyn/uni-uno.git
cd uni-uno  # or your chosen folder name
```

The build is fully standardized and cross-platform — pure **Conan** + **CMake
presets** + **npm**, with no OS-specific setup scripts. The complete step-by-step
instructions live in **[`BUILD.md`](BUILD.md)**.

In short:

```bash
# Frontend (npm lives strictly in frontend/)
cd frontend && npm install && npm run build && cd ..

# Backend
conan profile detect                              # one time — NO --force
conan install . -pr:a conan/release --build=missing
cmake --preset conan-release                       # configure
cmake --build --preset release                     # build
```

> ⚠️ Use `conan profile detect` **without** `--force`: the previous setup scripts
> passed `--force`, which silently overwrote any existing default profile. Plain
> `detect` preserves a profile you already have.

The build copies `cert.pem`, `key.pem`, `.env` and the `public/` folder next to the
executable. See [`BUILD.md`](BUILD.md) for prerequisites, TLS/`.env` generation, and
the full list of runtime environment variables (`PORT`, `DB_PATH`, `FRONTEND_PATH`,
`SSL_CERT_PATH`, `SSL_KEY_PATH`).

---

## 🏃 Running

The server reads its paths from environment variables (`DB_PATH`, `FRONTEND_PATH`,
`SSL_CERT_PATH`, `SSL_KEY_PATH`, `PORT`) with sensible local defaults — by default it
looks for the certificates, `.env` and the `public/` folder in the **current working
directory** and creates the SQLite database at `./build/uni_uno.sqlite`. Since the
build copies the runtime assets next to the executable, you can start it in two
equivalent ways:

```bash
# Linux / macOS — from the project root
./build/Release/uni_server

# …or from the build folder (assets already copied next to the binary)
cd build/Release && ./uni_server
```

```powershell
# Windows — from the project root
build\Release\uni_server.exe

# …or from the build folder
cd build\Release; .\uni_server.exe
```

The SQLite database is created automatically on first launch if it does not exist
(default `./build/uni_uno.sqlite`, override with `DB_PATH`). By default the server is
reachable at **https://localhost:9999** (accept the browser warning about the
self-signed certificate).

---

## 📚 Documentation

The complete documentation is already attached as **`documentazione-completa.pdf`**
(analysis, use cases and frontend tests + Doxygen backend manual + TypeDoc frontend API).

To **regenerate it** you need `doxygen`, `xelatex` (TeX Live), `mutool` (MuPDF) and
Node.js; then run:

```bash
./docs/build-full-docs.sh
```

---

## 🙏 Credits

See [`ATTRIBUTION.md`](ATTRIBUTION.md) for the full list of third-party assets. In short:

- **SNKRX font** — from [SNKRX](https://github.com/a327ex/SNKRX) by a327ex (MIT License).
- **Lobby music** — "Lo-Fi 16-bit Funky Fresh Beats To Chill and Jazz Out To
  [Sega Megadrive/Genesis Oscilloscope]" by birdrun
  ([source](https://www.youtube.com/watch?v=a2xtQqsuDRI)).
- **Backgrounds** — AI-generated with Google Gemini ("Nano Banana").

---

## ⚠️ Disclaimer

This project is an independent, non-commercial work. It is **not affiliated with,
endorsed by, or sponsored by Mattel, Inc. or the UNO® brand**. UNO® is a registered
trademark of Mattel, Inc. All trademarks are the property of their respective owners.
