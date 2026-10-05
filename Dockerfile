# syntax=docker/dockerfile:1

# =============================================================================
# Stage 1, Frontend (Svelte 5 / Vite) built with Node Alpine.
# Vite's outDir is "../public", so the bundle lands in /app/public.
# The schema generator reads the root contract/asyncapi.yaml, so it is copied too.
# =============================================================================
FROM node:26-alpine AS frontend
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json ./frontend/
RUN cd frontend && npm ci
COPY frontend/ ./frontend/
COPY contract/ ./contract/
COPY VERSION ./
RUN cd frontend && npm run build
# → /app/public

# =============================================================================
# Stage 2, Backend builder (Ubuntu): Conan + CMake presets compile uni_server.
# =============================================================================
FROM ubuntu:24.04 AS backend
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        pkg-config \
        ca-certificates \
        python3 \
        python3-pip \
        python3-yaml \
    && rm -rf /var/lib/apt/lists/*

# Conan (Ubuntu's Python is PEP 668 externally-managed → allow the install).
RUN pip3 install --break-system-packages --no-cache-dir conan

WORKDIR /app

# Resolve dependencies first so this layer is cached unless deps/profile change.
COPY conanfile.py ./
COPY conan.lock ./
COPY conan/ ./conan/
RUN conan profile detect \
    && conan install . -pr:a conan/release --build=missing

# Build the backend with the committed CMake presets.
# Default to plain HTTP: hosts like Render terminate TLS at the edge and proxy
# HTTP to the container. Override with --build-arg UNI_ENABLE_SSL=ON for a
# container that serves TLS directly (requires mounted cert + key).
ARG UNI_ENABLE_SSL=OFF
COPY . .
RUN cmake --preset conan-release -DUNI_ENABLE_SSL=${UNI_ENABLE_SSL} \
    -DUNI_PROD_BUILD=ON \
    && cmake --build --preset release
# → /app/build/Release/uni_server

# Strip dev-only mods (mod.json "dev_only": true, e.g. the test/bombs mod) so
# neither their content nor their assets reach the shipped image. The loader
# also skips them at runtime under UNI_PROD_BUILD; stripping them here keeps the
# bytes out of the image entirely.
RUN grep -rlE '"dev_only"[[:space:]]*:[[:space:]]*true' /app/mods/*/mod.json 2>/dev/null \
    | xargs -r -n1 dirname | xargs -r rm -rf

# =============================================================================
# Stage 3, Runtime: pristine Ubuntu with only the binary + static frontend.
# All dependencies are statically linked by Conan, so only the C++ runtime
# library is needed.
# =============================================================================
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=backend  /app/build/Release/uni_server  /app/uni_server
# Mod content is served from the image (no bind mount in prod); dev-only mods
# were stripped in the backend stage above.
COPY --from=backend  /app/mods                      /app/mods
COPY --from=frontend /app/public                    /app/public

# Mount points for the SQLite database and TLS certificates (see compose volumes).
RUN mkdir -p /app/data /app/certs

# Defaults consumed by the C++ backend (overridable via compose).
ENV PORT=3000 \
    DB_PATH=/app/data/uni.sqlite \
    FRONTEND_PATH=/app/public \
    SSL_CERT_PATH=/app/certs/cert.pem \
    SSL_KEY_PATH=/app/certs/key.pem

EXPOSE 3000

RUN useradd --system --uid 10001 --create-home --home-dir /app uni \
    && chown -R uni:uni /app
USER uni

CMD ["/app/uni_server"]
