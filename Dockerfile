# =============================================================================
# Poker Engine — Multi-stage Docker Build
# =============================================================================
# Builder: compile C++ server with all dependencies
# Runtime: minimal image with only the binary and runtime deps

# ---- Builder Stage ----
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libssl-dev \
    libsqlite3-dev \
    libpq-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy vendored dependencies first (better cache hits)
COPY third_party/ third_party/
COPY CMakeLists.txt .

# Pre-configure to cache dependency discovery
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF

# Copy source code
COPY core/ core/
COPY equity/ equity/
COPY cli/ cli/
COPY tests/ tests/
COPY infra/ infra/
COPY phase*/ phase*/
COPY scripts/ scripts/

# Build
RUN cmake --build build --parallel $(nproc) && \
    cmake --build build --target poker_tests

# ---- Runtime Stage ----
FROM ubuntu:22.04 AS production

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libsqlite3-0 \
    libpq5 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN groupadd -r poker && useradd -r -g poker -d /app -s /sbin/nologin poker

WORKDIR /app

# Copy built binary
COPY --from=builder /src/build/cli/poker_ws_server /app/poker_ws_server

# Copy frontend build if it exists
COPY --from=builder /src/frontend/dist/ /app/frontend/ 2>/dev/null || true

# Ensure binary is executable
RUN chmod +x /app/poker_ws_server

# Create data directory
RUN mkdir -p /data /models && chown -R poker:poker /app /data /models

USER poker

# Health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD curl -f http://localhost:9001/health || exit 1

EXPOSE 9000 9001 9002

# Default: run the unified server
ENV POKER_PRODUCTION=1
ENTRYPOINT ["/app/poker_ws_server"]
CMD ["9001"]
