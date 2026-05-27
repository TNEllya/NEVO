# ============================================================
# NEVO VoIP Server - Production Docker Image
# ============================================================
# Multi-stage build for minimal image size and security.
#
# Build:
#   docker build -t nevo-server:latest .
#
# Run:
#   docker run -d --name nevo-server \
#     -p 24430:24430/tcp -p 24431:24431/udp -p 24432:24432/udp \
#     -v nevo-data:/var/lib/nevo \
#     nevo-server:latest
# ============================================================

# ============================================================
# Stage 1: Build
# ============================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
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
    && cmake --build build --parallel "$(nproc)" \
    && strip build/bin/nevo_server

# ============================================================
# Stage 2: Runtime
# ============================================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libboost-system1.74.0 \
    libopus0 \
    libsodium23 \
    libsqlite3-0 \
    libssl3 \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r nevo -g 10001 && \
    useradd -r -g nevo -u 10001 -d /var/lib/nevo -s /sbin/nologin nevo

COPY --from=builder /build/build/bin/nevo_server /usr/local/bin/nevo_server

RUN chmod 755 /usr/local/bin/nevo_server && \
    chown root:root /usr/local/bin/nevo_server

RUN mkdir -p /var/lib/nevo /etc/nevo && \
    chown -R nevo:nevo /var/lib/nevo /etc/nevo

COPY --from=builder /build/server_config.example.json /etc/nevo/server_config.example.json

COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh

EXPOSE 24430/tcp 24431/udp 24432/udp

VOLUME ["/var/lib/nevo"]
VOLUME ["/etc/nevo"]

HEALTHCHECK --interval=15s --timeout=5s --start-period=10s --retries=3 \
    CMD nc -z -w3 localhost 24430 || exit 1

USER nevo

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["--config", "/etc/nevo/server_config.json", \
     "--db", "/var/lib/nevo/nevo_server.db"]