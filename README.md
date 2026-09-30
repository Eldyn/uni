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

A behavior graph opens a response window with a `window` node. Two optional
fields on that node shape it:

- **`duration`**: an integer number of milliseconds (`>= 0`) or `"env"` (the
  server's configured window length, `UNI_WINDOW_MS`). Negative, fractional or
  boolean values are rejected when the mod loads. Under
  `UNI_WINDOW_MODE=half_turn` the window lasts `min(duration, remaining turn
  time / 2)`. The card-level `window.when_played.duration` stays a string.
- **`kind`**: a presentation tag matching `^[a-z0-9_:.-]{1,32}$`, default
  `generic`, declared on the node (it is not derived from the node id). An
  invalid `kind` is a load error. `jump_in` is the one kind the engine acts on:
  it makes the window a hold (below).

Windows opened in the same situation form **one group**: responders are the
union, the duration is the longest member's, and the engine emits one
`window_open` and one `window_close` per group (a mod that hooks them per
member gets them once per group). A response is accepted for a member only if
the player is in that member's own `responders` and its `respond_with` accepts
the card; the first accepting member in load order owns the response and
supplies its `on_response` route. With no response, every member runs its
`default` route in member order. Windows deferred behind a parked input prompt
(a wild +4) merge the same way. The group's `kind` is its first member's.

**Hold and pass.** A group with a `jump_in` member holds for that member's
`duration` (800 ms in the shipped `jump_in` mod, capped at the group duration).
During the hold nobody may pass. After it, only the draw-debt victim may pass,
and a pass draws the debt immediately and closes the group; the timer ending
gives the same result. A `jump_in`-only window has no pass and closes at its
duration. Windows with no hold (`draw_stacking` alone, generic mod windows)
keep the plain pass: any responder may pass, and all responders passing closes
the window early. A victim's pass while a jump-in winner is already recorded
does not draw, because the debt has moved on.

**Jump-in keeps and accumulates debt.** An identical card jumped in over a
debt-carrying play is resolved like a stack response by the jumper: the debt
accumulates, lands on the jumper's next opponent, and the turn continues from
there. With no debt a jump-in only redirects the turn. The window re-opened
over the jumper's card holds only `draw_stacking` (no `jump_in` member, no
hold), so that card cannot itself be jumped into; the next card played opens its
own full group.

**Eligible cards.** While a window is open, `can_play` in a responder's own
snapshot follows the window (via `PlayEvaluator`), and the board highlights
those cards automatically.

**Wire and UI.** `window_open` carries `kinds` (member order), `kind`
(`kinds[0]`), `hold_ms` (omitted when 0) and `duration_ms`; its `deadline_ms`
is the time remaining, whereas the snapshot's `window.deadline_ms` and
`prompts[].deadline_ms` are absolute epoch ms, paired with `server_now_ms` so
the client can correct clock skew. `prompt_open.duration_ms`, the public
snapshot `prompt_wait {deadline_ms, duration_ms}` and `players_ready.timeout_ms`
give the other player-facing waits the same shape. The client draws one fuse
line along the bottom edge for whichever timer is active: the window group
(hold as a hatched segment), a prompt (observers included), the ready barrier
or the turn clock, and gates the victim's Draw action on the hold. A label
listing the window kinds appears beside it only with `?debug` in the URL or
`localStorage` `uni:debug=1`; it is not localised.

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
