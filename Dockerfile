# ============================================================
# NEVO VoIP Server - Multi-stage Docker Build
# ============================================================

# --- Stage 1: Build ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libboost-system-dev \
    libboost-lockfree-dev \
    libboost-endian-dev \
    libopus-dev \
    libsodium-dev \
    libsqlite3-dev \
    libssl-dev \
    qt6-base-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . .

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build --parallel "$(nproc)"

# --- Stage 2: Runtime ---
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.74.0 \
    libopus0 \
    libsodium23 \
    libsqlite3-0 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN groupadd -r nevo && useradd -r -g nevo -d /var/lib/nevo -s /sbin/nologin nevo

# Copy binary from builder
COPY --from=builder /build/bin/nevo_server /usr/local/bin/nevo_server

# Create data directories
RUN mkdir -p /var/lib/nevo /etc/nevo && chown -R nevo:nevo /var/lib/nevo /etc/nevo

# Copy example config
COPY --from=builder /build/server_config.example.json /etc/nevo/server_config.example.json

WORKDIR /var/lib/nevo

# TCP and UDP ports
EXPOSE 24430/tcp 24431/udp

# Volume for database and config
VOLUME ["/var/lib/nevo", "/etc/nevo"]

USER nevo

ENTRYPOINT ["/usr/local/bin/nevo_server"]
CMD ["--config", "/etc/nevo/server_config.json"]
