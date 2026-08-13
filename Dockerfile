# ============================================================
# NEVO VoIP Server - Production Docker Image
# ============================================================
# Multi-stage build based on Ubuntu 24.04 LTS slim.
# Produces a minimal, hardened runtime image for server deployment.
#
# Quick build:
#   docker build -t nevo-server:latest .
#
# Quick run:
#   docker run -d --name nevo-server \
#     -p 24430:24430/tcp -p 24431:24431/udp -p 24433:24433/tcp -p 8090:8090/tcp \
#     -v nevo-data:/var/lib/nevo \
#     nevo-server:latest
# ============================================================

# ============================================================
# Stage 1: Builder
# ============================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# Install build dependencies.
# Qt6 is intentionally omitted: the headless server core (nevo_server) does not link Qt.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    pkg-config \
    wget \
    python3 \
    libopus-dev \
    libsodium-dev \
    libsqlite3-dev \
    libssl-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libargon2-dev \
    && rm -rf /var/lib/apt/lists/*

# Build a recent Boost (1.86) for C++20 Asio features (cancel_after, as_tuple, etc.)
# that are not available in the Ubuntu 24.04 packaged Boost (1.83).
# Only the system library is compiled; all headers are installed.
RUN cd /tmp && \
    wget -q https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz && \
    tar -xzf boost_1_86_0.tar.gz && \
    cd boost_1_86_0 && \
    ./bootstrap.sh --with-libraries=system --prefix=/opt/boost && \
    ./b2 variant=release link=static cxxstd=20 -j"$(nproc)" install && \
    rm -rf /tmp/boost_1_86_0 /tmp/boost_1_86_0.tar.gz

WORKDIR /build

# Leverage Docker cache for dependency-heavy rebuilds: copy build scripts first.
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY 3rdparty ./3rdparty
COPY proto ./proto
COPY src ./src
COPY web ./web
COPY server_config.example.json ./

# Regenerate protobuf C++ sources with the container's protoc so the generated
# code exactly matches the libprotobuf version installed in this image.
RUN rm -f proto/generated/*.pb.h proto/generated/*.pb.cc && \
    mkdir -p proto/generated && \
    protoc --cpp_out=proto/generated --proto_path=proto proto/*.proto

# Build only the headless server target. Keep symbols for debugging until strip.
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_TESTING=OFF \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBoost_ROOT=/opt/boost \
    && cmake --build build --target nevo_server --parallel "$(nproc)" \
    && strip --strip-unneeded build/bin/nevo_server

# ============================================================
# Stage 2: Runtime
# ============================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TZ=UTC \
    NEVO_WEB_HOST=0.0.0.0 \
    NEVO_WEB_PORT=8090 \
    NEVO_CONTROL_PORT=24433

# Install runtime dependencies. python3 is required by the bundled web management proxy.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libopus0 \
    libsodium23 \
    libsqlite3-0 \
    libssl3 \
    libprotobuf32 \
    libargon2-1 \
    python3 \
    python3-psutil \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Create a non-privileged service user.
RUN groupadd -r nevo -g 10001 && \
    useradd -r -g nevo -u 10001 -d /var/lib/nevo -s /usr/sbin/nologin nevo

# Copy the server binary from the builder stage.
COPY --from=builder /build/build/bin/nevo_server /usr/local/bin/nevo_server
RUN chmod 755 /usr/local/bin/nevo_server && \
    chown root:root /usr/local/bin/nevo_server

# Copy the bundled web management UI and example configuration.
COPY --from=builder /build/web /usr/share/nevo/web
COPY --from=builder /build/server_config.example.json /etc/nevo/server_config.example.json

# Prepare writable directories.
RUN mkdir -p /var/lib/nevo /etc/nevo /var/log/nevo && \
    chown -R nevo:nevo /var/lib/nevo /etc/nevo /var/log/nevo

# Entrypoint handles config initialization, signal forwarding and web proxy startup.
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh

# Expose service ports.
# 24430/tcp - client control/voice signalling
# 24431/udp - voice media
# 24432/udp - video/screen-share media (auto-assigned as udp_port + 1)
# 24433/tcp - management control server (JSON-over-TCP)
# 8090/tcp  - web management UI
EXPOSE 24430/tcp 24431/udp 24432/udp 24433/tcp 8090/tcp

# Persistent data volumes.
VOLUME ["/var/lib/nevo"]
VOLUME ["/etc/nevo"]

# Health check against the client TCP signalling port.
HEALTHCHECK --interval=15s --timeout=5s --start-period=20s --retries=3 \
    CMD nc -z -w3 127.0.0.1 24430 || exit 1

# Drop privileges for the runtime process.
USER nevo

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["--config", "/etc/nevo/server_config.json", \
     "--db", "/var/lib/nevo/nevo_server.db"]
