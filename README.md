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
- **Data-driven game engine**: cards, rules and mods are JSON data validated
  against the shared wire contract and resolved by the ECS + op-catalog pipeline
  (see [Game engine](#-game-engine)). Ships the classic ruleset plus optional
  mods: _Draw Stacking_, _Progressive_, _Force Play_, _Jump In_, _No Bluffing_,
  _Seven-Zero_.
- **Bots** with heuristics, also used to handle turns in case of inactivity (AFK).
- **Card animations** (flight from the deck to the hand and to the discard pile, `+N`
  counter for accumulated penalties).
- **Saving and resuming** interrupted matches, personal **statistics** and a global
  **leaderboard**.

---

## 🛠️ System prerequisites

### Backend (C++)

- A C++ compiler with **C++23** support (GCC or Clang)
- **Python 3** (with `pip`), used to install Conan and Ninja
- **CMake**
- **OpenSSL**, to generate the TLS certificates

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
git clone https://github.com/Eldyn/uni.git
cd uni  # or your chosen folder name
```

The build is fully standardized and cross-platform, pure **Conan** + **CMake
presets** + **npm**, with no OS-specific setup scripts. The complete step-by-step
instructions live in **[`BUILD.md`](BUILD.md)**.

In short:

```bash
# Frontend (npm lives strictly in frontend/)
cd frontend && npm install && npm run build && cd ..

# Backend
conan profile detect                              # one time, NO --force
conan install . -pr:a conan/release --build=missing
cmake --preset conan-release                       # configure
cmake --build --preset release                     # build
```

> ⚠️ Use `conan profile detect` **without** `--force`: the previous setup scripts
> passed `--force`, which silently overwrote any existing default profile. Plain
> `detect` preserves a profile you already have.

The build copies `cert.pem`, `key.pem` and `.env` next to the executable, and
symlinks the `public/` folder there. See [`BUILD.md`](BUILD.md) for prerequisites,
TLS/`.env` generation, the frontend dev loop (`npm run watch`), and the full list
of runtime environment variables (`PORT`, `DB_PATH`, `FRONTEND_PATH`,
`SSL_CERT_PATH`, `SSL_KEY_PATH`, `STATIC_CACHE`).

---

## 🏃 Running

The server reads its paths from environment variables (`DB_PATH`, `FRONTEND_PATH`,
`SSL_CERT_PATH`, `SSL_KEY_PATH`, `PORT`) with sensible local defaults: it
looks for the certificates, `.env`, the `public/` folder and the SQLite database
(`uni.sqlite`) in the **current working directory**. Since the build places the
runtime assets next to the executable (copied for `cert.pem`/`key.pem`/`.env`,
symlinked for `public/`), you can start it in two equivalent ways:

```bash
# Linux / macOS, from the project root
./build/Release/uni_server

# …or from the build folder (assets already copied next to the binary)
cd build/Release && ./uni_server
```

```powershell
# Windows, from the project root
build\Release\uni_server.exe

# …or from the build folder
cd build\Release; .\uni_server.exe
```

The SQLite database is created automatically on first launch if it does not exist
(default `uni.sqlite` in the working directory, override with `DB_PATH`). By default the server is
reachable at **https://localhost:9999** (accept the browser warning about the
self-signed certificate).

---

## 🧩 Game engine

The backend runs a **data-driven, ECS-based engine**. The single source of truth
is the language-neutral **`contract/`** definition: one specification drives code
generation into the TypeScript schemas the frontend consumes and the C++ types
the backend compiles, so the two sides of the wire cannot drift.

- **Content is data.** Cards, rules, mods and decks are authored as JSON and
  validated against the JSON Schemas in `contract/schemas/` before a match can
  use them; a malformed mod folder is rejected with structured errors.
- **EntityStore (ECS).** Match state lives in an entity/component store with
  generational handles, giving cheap lookups and safe reclamation of dead
  entities.
- **Resolver + op catalog.** A play is resolved as an ordered pipeline of hooks
  and operations; every effect a card or rule can express maps to a registered
  op with a typed signature, evaluated under a per-resolution budget so a
  malicious or accidental rule loop aborts cleanly instead of hanging.
- **Ordered event stream.** Resolution emits a single ordered `match_event`
  stream. A per-recipient **view builder** projects that stream — hiding hands
  and deck contents where the rules require — so every client sees only what it
  is entitled to and replays share one canonical transcript.

### Response windows (mod authoring)

Graph `window` nodes open a response window after a play. Fields:

- `duration`: an integer in milliseconds (`>= 0`) or `"env"` (the server default). A `half_turn` window uses `min(duration, remaining turn / 2)`. A negative integer or any non-integer, non-string value (fractional, boolean) is rejected at mod load; the resolver's fallback to `"env"` with a warning is a runtime backstop only. Integer `duration` is available on graph window nodes; card-level `window.when_played.duration` remains a string.
- `kind`: optional string matching `^[a-z0-9_:.-]{1,32}$`, default `generic`. A value that does not match is rejected at mod load (the resolver's fallback to `generic` is a runtime backstop). It is declared on the node (not derived from its id) and appears in the `window_open` payload and the snapshot window. Windows of kind `jump_in` accept no passes: `PassWindow` is refused and the gate runs its full duration (the bundled `jump_in` mod uses 800 ms).
- Eligible cards are highlighted automatically for responders through `can_play`, computed by the engine's `PlayEvaluator` over the responder's own hand. Mods declare nothing for this.
- `window_open`/`window_close` hooks fire once per window group. Windows opened in the same situation form one group: one `window_open` and one `window_close` per group, not per member. Duration is the longest member's, responders are the union, and the group kind is the first member's in mod load order. A member without its own `duration` counts as the env default when the longest is computed. Because the kind is the first member's, a group whose first member is not `jump_in` stays passable even if a later member is `jump_in`; with `jump_in` first, the victim cannot pass early and the gate lasts the longest duration.
- A response is accepted for a member only if the player is in that member's own `responders` and its `respond_with` accepts the card. The first accepting member (load order) owns the response and supplies `on_response`. At close, if a response won, only the owner's route runs and the other members' defaults are skipped; with no winner every member runs its default in load order. A non-stacking win clears the recorded draw debt. Windows deferred behind a parked input prompt (for example a wild +4) merge into one group too.

The engine is the subject of the design record at
[`docs/superpowers/specs/2026-09-19-card-engine-ecs-rewrite-design.md`](docs/superpowers/specs/2026-09-19-card-engine-ecs-rewrite-design.md).

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

- **SNKRX font**, from [SNKRX](https://github.com/a327ex/SNKRX) by a327ex (MIT License).
- **Lobby music**, "Lo-Fi 16-bit Funky Fresh Beats To Chill and Jazz Out To
  [Sega Megadrive/Genesis Oscilloscope]" by birdrun
  ([source](https://www.youtube.com/watch?v=a2xtQqsuDRI)).
- **Backgrounds**, AI-generated with Google Gemini ("Nano Banana").

---

## ⚠️ Disclaimer

This project is an independent work. It is **not affiliated with, endorsed by, or
sponsored by Mattel, Inc. or the UNO® brand**. UNO® is a registered trademark of
Mattel, Inc. All trademarks are the property of their respective owners.

## 📄 License

UNI's original code and assets are released under [CC0 1.0 Universal](LICENSE)
(public domain), use them freely, including commercially. Third-party assets
remain under their own licenses; see [`ATTRIBUTION.md`](ATTRIBUTION.md).
