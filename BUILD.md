# 🛠️ Building Uni-Uno

Standardized, cross-platform build using **Conan**, **CMake presets** and **npm**.
The same commands work on Linux, macOS and Windows — CMake's native presets handle
the platform differences, so there are no OS-specific setup scripts.

For a fully reproducible production environment, see the Docker workflow in
[`README.md`](README.md#-docker) instead.

---

## Prerequisites

| Tool | Used for |
|------|----------|
| **C++23 compiler** (GCC ≥ 13 / Clang ≥ 16 / MSVC 2022) | backend |
| **CMake** ≥ 3.23 | backend configure/build (presets v3) |
| **Conan** 2.x | backend dependencies |
| **Ninja** | backend generator (installed by Conan as a `tool_require`) |
| **Python 3** + `pip` | installing Conan, contract codegen |
| **Node.js** + **npm** | frontend only |
| **OpenSSL** | generating local TLS certificates |

```bash
pip install --upgrade conan
```

---

## 1. Conan profile (one time)

Conan needs a default profile describing your compiler. Create it **without**
`--force` so an existing profile (and any customisation you've made) is left
untouched:

```bash
conan profile detect
```

> ⚠️ Do **not** pass `--force`. `conan profile detect --force` overwrites your
> existing default profile, discarding any manual configuration. Plain
> `conan profile detect` only writes the profile if one does not already exist;
> if you already have a default profile, keep it as-is (this command will simply
> report that one exists).

---

## 2. Frontend

The Vite build writes the static bundle straight into the project-root `public/`
folder (`outDir: ../public`, `emptyOutDir: true`):

```bash
cd frontend
npm install
npm run build
cd ..
```

This produces `public/` at the repository root — the directory the backend serves
by default (`FRONTEND_PATH`, see below).

---

## 3. Backend dependencies (Conan)

```bash
conan install . --build=missing -s compiler.cppstd=23
```

This resolves and (if needed) builds the C++ dependencies and generates the
toolchain at `build/Release/generators/conan_toolchain.cmake`.

> The committed `CMakePresets.json` owns the `conan-release` preset, so the Conan
> recipe disables `CMakeUserPresets.json` generation (`user_presets_path = False`)
> to avoid a duplicate-preset clash.

---

## 4. Configure & build (CMake presets)

```bash
cmake --preset conan-release      # configure
cmake --build --preset release    # build
```

- `conan-release` — configure preset: Ninja generator, Conan toolchain,
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON` (for clangd/LSP), output in `build/Release`.
- `release` — build preset for the `conan-release` configuration.

The resulting binary is `build/Release/uni_server`. The build copies `cert.pem`,
`key.pem`, `.env` and `public/` next to the executable, so it can be launched
from either the project root or `build/Release`.

---

## 5. Runtime configuration (environment variables)

The backend reads all paths from the environment, with local-friendly defaults:

| Variable | Default | Purpose |
|----------|---------|---------|
| `PORT` | `9999` | listening port |
| `DB_PATH` | `./build/uni_uno.sqlite` | SQLite database file |
| `FRONTEND_PATH` | `public` | directory with the built frontend |
| `SSL_CERT_PATH` | `cert.pem` | TLS certificate |
| `SSL_KEY_PATH` | `key.pem` | TLS private key |

These can be set in the shell, injected by Docker Compose, or placed in a `.env`
file at the working directory (also still used for `JWT_SECRET` and
`PASSWORD_PEPPER`).

### Local TLS certificate

```bash
openssl req -newkey rsa:2048 -nodes -x509 -days 365 \
  -keyout key.pem -out cert.pem \
  -subj "/C=IT/ST=Italy/L=Uniba/O=uni/CN=localhost"
```

### Secrets (`.env`)

```bash
printf 'JWT_SECRET=%s\nPASSWORD_PEPPER=%s\n' \
  "$(openssl rand -hex 32)" "$(openssl rand -hex 32)" > .env
```

---

## Run

```bash
./build/Release/uni_server
```

Open <https://localhost:9999> and accept the self-signed certificate warning.
