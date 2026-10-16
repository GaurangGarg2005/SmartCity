# ─── Stage 1: Builder ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libssl-dev \
    libz-dev \
    libasio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy dependency manifests first for layer caching
COPY backend/CMakeLists.txt ./backend/CMakeLists.txt

# Copy source
COPY backend/ ./backend/
COPY data/     ./data/
COPY frontend/ ./frontend/

# Configure and build (Release, no ASAN, no tests in final image)
RUN cmake -B backend/build \
      -S backend \
      -DCMAKE_BUILD_TYPE=Release \
      -DSMARTCITY_BUILD_TESTS=OFF \
      -DSMARTCITY_BUILD_BENCH=OFF \
    && cmake --build backend/build --parallel $(nproc)

# ─── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libstdc++6 \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Non-root user
RUN useradd -r -u 1001 -s /bin/false smartcity
USER smartcity

WORKDIR /app

# Copy the binary
COPY --from=builder --chown=smartcity:smartcity \
    /build/backend/build/smartcity_server ./smartcity_server

# Copy data files baked into the image
COPY --from=builder --chown=smartcity:smartcity \
    /build/data/ ./data/

# Copy vendored frontend
COPY --from=builder --chown=smartcity:smartcity \
    /build/frontend/ ./frontend/

# Render sets $PORT; default 8080 for local
ENV PORT=8080
EXPOSE ${PORT}

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD curl -f http://localhost:${PORT}/healthz || exit 1

CMD ["./smartcity_server"]
