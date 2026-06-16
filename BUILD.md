# 🛠️ Building UNI

The same commands work on Linux, macOS and Windows — CMake's native presets handle
the platform differences, so there are no OS-specific setup scripts.

For a fully reproducible production environment, see the Docker workflow in
[`README.md`](README.md#-docker) instead.

---

## Prerequisites

| Tool                                                   | Used for                                                   |
| ------------------------------------------------------ | ---------------------------------------------------------- |
| **C++23 compiler** (GCC ≥ 13 / Clang ≥ 16 / MSVC 2022) | backend                                                    |
| **CMake** ≥ 3.23                                       | backend configure/build (presets v3)                       |
| **Conan** 2.x                                          | backend dependencies                                       |
| **Ninja**                                              | backend generator (installed by Conan as a `tool_require`) |
| **Python 3** + `pip`                                   | installing Conan, contract codegen                         |
| **Node.js** + **npm**                                  | frontend only                                              |
| **OpenSSL** / **Git Bash**                             | generating local TLS certificates                          |

```bash
pip install --upgrade conan
```

---

## 1. Conan profile (one time)

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

The Vite build writes the static bundle straight into the project-root's `public/` folder.

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
conan install . -pr:a conan/release --build=missing
```

This resolves and (if needed) builds the C++ dependencies and generates the
toolchain at `build/Release/generators/conan_toolchain.cmake`.

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

### TLS vs plain HTTP (`UNI_ENABLE_SSL`)

uWebSockets bakes the SSL flag into its types, so the transport is chosen at
compile time. It defaults to **ON** (`uWS::SSLApp`, TLS with a local cert):

```bash
cmake --preset conan-release -DUNI_ENABLE_SSL=OFF   # plain HTTP (uWS::App)
```

Build with it **OFF** when a platform terminates TLS at its edge and proxies
plain HTTP to the container — e.g. **Render**, Fly, or any reverse proxy. The
frontend adapts automatically (`wss://` over HTTPS pages, `ws://` over HTTP).
The Docker image builds with `UNI_ENABLE_SSL=OFF` by default for this reason
(override via `--build-arg UNI_ENABLE_SSL=ON`).

### Unit tests (`UNI_BUILD_TESTS`)

Defaults to **ON**: configuring builds the `uni_tests` target (doctest) and
registers it with CTest. Keeping it on also writes the test translation units
into `compile_commands.json`, so clangd/LSP can resolve `<doctest/doctest.h>`
and give full diagnostics on the files under `tests/unit/`.

```bash
ctest --test-dir build/Release            # run the unit tests
cmake --preset conan-release -DUNI_BUILD_TESTS=OFF   # lighter build, no tests
```

Turn it **OFF** for a leaner build: it skips the doctest dependency and the
`uni_tests` target entirely. The trade-off is that editors lose IntelliSense on
the test sources, since those files no longer appear in the compile database.

---

## 5. Runtime configuration (environment variables)

The backend reads all paths from the environment, with local-friendly defaults:

| Variable        | Default                  | Purpose                           |
| --------------- | ------------------------ | --------------------------------- |
| `PORT`          | `9999`                   | listening port                    |
| `DB_PATH`       | `uni.sqlite`             | SQLite database file (cwd-relative) |
| `FRONTEND_PATH` | `public`                 | directory with the built frontend |
| `SSL_CERT_PATH` | `cert.pem`               | TLS certificate                   |
| `SSL_KEY_PATH`  | `key.pem`                | TLS private key                   |

These can be set in the shell, injected by Docker Compose, or placed in a `.env`
file at the working directory.

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
