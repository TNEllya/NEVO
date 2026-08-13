#!/bin/bash
# ============================================================
# NEVO Server Docker Entrypoint
# ============================================================
# Responsibilities:
#   - Initialize configuration from example when missing
#   - Override config values with environment variables
#   - Start the bundled web management proxy (external access)
#   - Start nevo_server and forward shutdown signals
#   - Graceful shutdown on SIGTERM/SIGINT
# ============================================================

set -e

CONFIG_FILE="/etc/nevo/server_config.json"
DB_PATH="/var/lib/nevo/nevo_server.db"
WEB_ROOT="/usr/share/nevo/web"
WEB_PID=""
SERVER_PID=""

log() {
    echo "[NEVO Entrypoint] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2
}

# ---------------------------------------------------------------------------
# Configuration initialization
# ---------------------------------------------------------------------------
init_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        log "Config file not found at $CONFIG_FILE, creating from example..."
        if [ -f "/etc/nevo/server_config.example.json" ]; then
            cp /etc/nevo/server_config.example.json "$CONFIG_FILE"
            log "Default config created from example."
        else
            log "ERROR: No config template found."
            exit 1
        fi
    fi
}

# ---------------------------------------------------------------------------
# Build command-line arguments (CLI overrides config file values)
# ---------------------------------------------------------------------------
build_args() {
    local args=()
    if [ -f "$CONFIG_FILE" ]; then
        args+=("--config" "$CONFIG_FILE")
    fi
    args+=("--db" "$DB_PATH")

    [ -n "$NEVO_TCP_PORT" ]    && args+=("--tcp-port" "$NEVO_TCP_PORT")
    [ -n "$NEVO_UDP_PORT" ]    && args+=("--udp-port" "$NEVO_UDP_PORT")
    [ -n "$NEVO_SERVER_NAME" ] && args+=("--server-name" "$NEVO_SERVER_NAME")
    [ -n "$NEVO_MAX_USERS" ]   && args+=("--max-users" "$NEVO_MAX_USERS")
    [ -n "$NEVO_LOG_LEVEL" ]   && args+=("--log-level" "$NEVO_LOG_LEVEL")
    [ -n "$NEVO_THREADS" ]     && args+=("--threads" "$NEVO_THREADS")

    echo "${args[@]}"
}

# ---------------------------------------------------------------------------
# Start the web management proxy so it is reachable from outside the container
# ---------------------------------------------------------------------------
start_web_proxy() {
    if [ ! -f "$WEB_ROOT/server.py" ]; then
        log "WARNING: Web proxy not found at $WEB_ROOT/server.py"
        return 0
    fi

    export NEVO_WEB_HOST="${NEVO_WEB_HOST:-0.0.0.0}"
    export NEVO_WEB_PORT="${NEVO_WEB_PORT:-8090}"
    export NEVO_CONTROL_PORT="${NEVO_CONTROL_PORT:-24433}"
    export NEVO_WEB_ROOT="$WEB_ROOT"

    log "Starting web management proxy on ${NEVO_WEB_HOST}:${NEVO_WEB_PORT}"
    python3 "$WEB_ROOT/server.py" &
    WEB_PID=$!
}

# ---------------------------------------------------------------------------
# Graceful shutdown
# ---------------------------------------------------------------------------
cleanup() {
    log "Received shutdown signal, stopping NEVO services..."

    if [ -n "$SERVER_PID" ]; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    if [ -n "$WEB_PID" ]; then
        kill -TERM "$WEB_PID" 2>/dev/null || true
        wait "$WEB_PID" 2>/dev/null || true
    fi

    log "NEVO server stopped."
    exit 0
}

trap cleanup SIGTERM SIGINT SIGQUIT

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
init_config

SERVER_ARGS=$(build_args)
log "Starting nevo_server with args: $SERVER_ARGS"

# Start web proxy first. nevo_server also tries to start one internally,
# but that instance binds to 127.0.0.1 only. The container needs external
# access, so we provide a separate proxy bound to 0.0.0.0.
start_web_proxy

/usr/local/bin/nevo_server $SERVER_ARGS &
SERVER_PID=$!

log "nevo_server started with PID: $SERVER_PID"

wait "$SERVER_PID"
EXIT_CODE=$?

log "nevo_server exited with code: $EXIT_CODE"
exit $EXIT_CODE
