#!/bin/bash
# ============================================================
# NEVO Server - Native Linux Installation Script
# ============================================================
# Supports: Ubuntu 20.04+, CentOS 8+, Debian 11+
#
# Usage:
#   sudo bash install.sh
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${BLUE}[STEP]${NC}  $*"; }

INSTALL_DIR="/opt/nevo"
DATA_DIR="/var/lib/nevo"
CONFIG_DIR="/etc/nevo"
SERVICE_FILE="/etc/systemd/system/nevo-server.service"
BUILD_DIR="/tmp/nevo-build"

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        log_error "This script must be run as root. Use: sudo bash install.sh"
        exit 1
    fi
}

detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
        OS_VERSION=$VERSION_ID
    elif [ -f /etc/redhat-release ]; then
        OS="centos"
        OS_VERSION=$(cat /etc/redhat-release | grep -oP '\d+' | head -1)
    else
        log_error "Cannot detect OS. Supported: Ubuntu 20.04+, CentOS 8+, Debian 11+"
        exit 1
    fi
    log_info "Detected OS: $OS $OS_VERSION"
}

install_deps_ubuntu() {
    log_step "Installing dependencies (Ubuntu/Debian)..."
    apt-get update
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        libboost-system-dev \
        libboost-lockfree-dev \
        libboost-endian-dev \
        libopus-dev \
        libsodium-dev \
        libsqlite3-dev \
        libssl-dev
    log_info "Dependencies installed."
}

install_deps_centos() {
    log_step "Installing dependencies (CentOS/RHEL)..."
    dnf install -y epel-release
    dnf groupinstall -y "Development Tools"
    dnf install -y \
        cmake \
        git \
        pkgconfig \
        boost-devel \
        opus-devel \
        libsodium-devel \
        sqlite-devel \
        openssl-devel
    log_info "Dependencies installed."
}

build_server() {
    log_step "Building NEVO server..."

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
    cp -r ./* "$BUILD_DIR/"

    cd "$BUILD_DIR"

    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

    cmake --build build --parallel "$(nproc)"

    log_info "Build complete."
}

install_server() {
    log_step "Installing NEVO server..."

    mkdir -p "$INSTALL_DIR/bin"
    mkdir -p "$DATA_DIR"
    mkdir -p "$CONFIG_DIR"

    cp "$BUILD_DIR/build/bin/nevo_server" "$INSTALL_DIR/bin/nevo_server"
    chmod 755 "$INSTALL_DIR/bin/nevo_server"

    if [ ! -f "$CONFIG_DIR/server_config.json" ]; then
        cp server_config.example.json "$CONFIG_DIR/server_config.json"
        log_info "Created default config at $CONFIG_DIR/server_config.json"
    fi

    sed -i 's|"db_path": "nevo_server.db"|"db_path": "/var/lib/nevo/nevo_server.db"|' \
        "$CONFIG_DIR/server_config.json" 2>/dev/null || true

    useradd -r -d "$DATA_DIR" -s /sbin/nologin nevo 2>/dev/null || true
    chown -R nevo:nevo "$DATA_DIR" "$CONFIG_DIR"

    log_info "Server installed to $INSTALL_DIR"
}

setup_systemd() {
    log_step "Setting up systemd service..."

    cat > "$SERVICE_FILE" << EOF
[Unit]
Description=NEVO VoIP Server
After=network.target

[Service]
Type=simple
User=nevo
Group=nevo
WorkingDirectory=$DATA_DIR
ExecStart=$INSTALL_DIR/bin/nevo_server --config $CONFIG_DIR/server_config.json --db $DATA_DIR/nevo_server.db
ExecStop=/bin/kill -TERM \$MAINPID
Restart=on-failure
RestartSec=10
LimitNOFILE=65536
StandardOutput=journal
StandardError=journal
SyslogIdentifier=nevo-server
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=$DATA_DIR $CONFIG_DIR

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    log_info "systemd service created at $SERVICE_FILE"
}

enable_service() {
    log_step "Enabling and starting service..."

    systemctl enable nevo-server
    systemctl start nevo-server

    sleep 2
    if systemctl is-active --quiet nevo-server; then
        log_info "NEVO server is running!"
        systemctl status nevo-server --no-pager
    else
        log_error "NEVO server failed to start. Check logs with: journalctl -u nevo-server -f"
        exit 1
    fi
}

cleanup() {
    log_step "Cleaning up build artifacts..."
    rm -rf "$BUILD_DIR"
}

show_final_info() {
    echo ""
    echo "============================================"
    echo "  NEVO Server Installation Complete!"
    echo "============================================"
    echo ""
    echo "  Binary:     $INSTALL_DIR/bin/nevo_server"
    echo "  Config:     $CONFIG_DIR/server_config.json"
    echo "  Data:       $DATA_DIR/"
    echo "  TCP Port:   24430"
    echo "  UDP Port:   24431"
    echo "  Video Port: 24432"
    echo ""
    echo "  Service Management:"
    echo "    systemctl start   nevo-server    Start server"
    echo "    systemctl stop    nevo-server    Stop server"
    echo "    systemctl restart nevo-server    Restart server"
    echo "    systemctl status  nevo-server    Check status"
    echo "    journalctl -u nevo-server -f     View logs"
    echo ""
    echo "============================================"
    echo ""
}

main() {
    check_root
    detect_os

    case "$OS" in
        ubuntu|debian)
            install_deps_ubuntu
            ;;
        centos|rhel|fedora|rocky|almalinux)
            install_deps_centos
            ;;
        *)
            log_error "Unsupported OS: $OS. Please install dependencies manually."
            exit 1
            ;;
    esac

    build_server
    install_server
    setup_systemd
    enable_service
    cleanup
    show_final_info
}

main