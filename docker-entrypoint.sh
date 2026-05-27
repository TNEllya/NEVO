#!/bin/bash
# ============================================================
# NEVO Server Docker Entrypoint
# ============================================================
# Handles config initialization, signal forwarding, and
# graceful shutdown.
# ============================================================

set -e

CONFIG_FILE="/etc/nevo/server_config.json"
DB_PATH="/var/lib/nevo/nevo_server.db"

log() {
    echo "[NEVO Entrypoint] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2
}

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

    if [ -n "$NEVO_TCP_PORT" ]; then
        log "Overriding TCP port from env: $NEVO_TCP_PORT"
    fi
    if [ -n "$NEVO_UDP_PORT" ]; then
        log "Overriding UDP port from env: $NEVO_UDP_PORT"
    fi
    if [ -n "$NEVO_SERVER_NAME" ]; then
        log "Overriding server name from env: $NEVO_SERVER_NAME"
    fi
    if [ -n "$NEVO_MAX_USERS" ]; then
        log "Overriding max users from env: $NEVO_MAX_USERS"
    fi
    if [ -n "$NEVO_LOG_LEVEL" ]; then
        log "Overriding log level from env: $NEVO_LOG_LEVEL"
    fi
    if [ -n "$NEVO_THREADS" ]; then
        log "Overriding threads from env: $NEVO_THREADS"
    fi

    log "Configuration initialized."
}

build_args() {
    local args=()
    if [ -f "$CONFIG_FILE" ]; then
        args+=("--config" "$CONFIG_FILE")
    fi
    args+=("--db" "$DB_PATH")
    if [ -n "$NEVO_TCP_PORT" ]; then
        args+=("--tcp-port" "$NEVO_TCP_PORT")
    fi
    if [ -n "$NEVO_UDP_PORT" ]; then
        args+=("--udp-port" "$NEVO_UDP_PORT")
    fi
    if [ -n "$NEVO_SERVER_NAME" ]; then
        args+=("--server-name" "$NEVO_SERVER_NAME")
    fi
    if [ -n "$NEVO_MAX_USERS" ]; then
        args+=("--max-users" "$NEVO_MAX_USERS")
    fi
    if [ -n "$NEVO_LOG_LEVEL" ]; then
        args+=("--log-level" "$NEVO_LOG_LEVEL")
    fi
    if [ -n "$NEVO_THREADS" ]; then
        args+=("--threads" "$NEVO_THREADS")
    fi
    echo "${args[@]}"
}

cleanup() {
    log "Received shutdown signal, stopping NEVO server..."
    if [ -n "$SERVER_PID" ]; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    log "NEVO server stopped."
    exit 0
}

trap cleanup SIGTERM SIGINT SIGQUIT

init_config

SERVER_ARGS=$(build_args)
log "Starting NEVO server with args: $SERVER_ARGS"

/usr/local/bin/nevo_server $SERVER_ARGS &
SERVER_PID=$!

log "NEVO server started with PID: $SERVER_PID"

wait "$SERVER_PID"
EXIT_CODE=$?

log "NEVO server exited with code: $EXIT_CODE"
exit $EXIT_CODE