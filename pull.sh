#!/bin/bash
# ============================================================
# NEVO Server - One-Click Pull & Deploy
# ============================================================
# Fetches pre-built image from GitHub Container Registry and
# starts the server. No compilation required.
#
# Usage:
#   bash pull.sh              Pull latest, start server
#   bash pull.sh update       Pull latest, restart server
#   bash pull.sh stop         Stop server
#   bash pull.sh status       Show server status
#   bash pull.sh logs         View server logs
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

COMPOSE_FILE="docker-compose.pull.yml"
ENV_FILE=".env"
CONFIG_FILE="server_config.json"
CONTAINER_NAME="nevo-server"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $(date '+%H:%M:%S') $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $(date '+%H:%M:%S') $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $(date '+%H:%M:%S') $*"; }
log_step()  { echo -e "${BLUE}[STEP]${NC}  $(date '+%H:%M:%S') $*"; }

# ============================================================
# Utility
# ============================================================
check_docker() {
    if ! command -v docker &>/dev/null; then
        log_error "Docker is not installed."
        echo ""
        echo "  Ubuntu/Debian:"
        echo "    curl -fsSL https://get.docker.com | sudo bash"
        echo ""
        echo "  CentOS/RHEL:"
        echo "    sudo dnf install -y docker && sudo systemctl enable --now docker"
        echo ""
        exit 1
    fi
    if ! docker compose version &>/dev/null; then
        log_error "Docker Compose V2 is required. Please upgrade Docker."
        exit 1
    fi
}

check_ghcr_login() {
    local image_ref
    image_ref=$(grep -E '^\s+image:' "$COMPOSE_FILE" | head -1 | awk '{print $2}')
    local registry
    registry=$(echo "$image_ref" | cut -d/ -f1)

    if ! docker pull "$image_ref" &>/dev/null; then
        log_warn "Cannot pull $image_ref"
        log_info "Logging into GitHub Container Registry..."
        echo ""
        if [ -n "$GITHUB_TOKEN" ]; then
            echo "$GITHUB_TOKEN" | docker login "$registry" -u "$GITHUB_USER" --password-stdin
            log_info "Logged in via GITHUB_TOKEN."
        else
            echo "  You need a GitHub Personal Access Token (classic) with 'read:packages' scope."
            echo "  Create one at: https://github.com/settings/tokens"
            echo ""
            read -rp "  GitHub Username: " gh_user
            read -rsp "  GitHub Token: " gh_token
            echo ""
            echo "$gh_token" | docker login "$registry" -u "$gh_user" --password-stdin
            log_info "Logged in as $gh_user."
        fi
    fi
}

init_config() {
    if [ ! -f "$ENV_FILE" ]; then
        cp .env.example "$ENV_FILE" 2>/dev/null || {
            cat > "$ENV_FILE" << 'EOFENV'
TZ=Asia/Shanghai
NEVO_SERVER_NAME=NEVO Server
NEVO_TCP_PORT=24430
NEVO_UDP_PORT=24431
NEVO_VIDEO_UDP_PORT=24432
NEVO_MAX_USERS=100
NEVO_THREADS=4
NEVO_LOG_LEVEL=info
NEVO_CPU_LIMIT=2
NEVO_MEMORY_LIMIT=512M
NEVO_REPO_OWNER=your-username
NEVO_IMAGE_TAG=latest
EOFENV
        }
        log_info ".env created. Edit NEVO_REPO_OWNER to match your GitHub username."
        if grep -q "NEVO_REPO_OWNER=your-username" "$ENV_FILE" 2>/dev/null; then
            echo ""
            log_warn "Set your GitHub username in .env: NEVO_REPO_OWNER=your-username"
        fi
    fi

    if [ ! -f "$CONFIG_FILE" ]; then
        cp server_config.example.json "$CONFIG_FILE" 2>/dev/null || {
            cat > "$CONFIG_FILE" << 'EOFJSON'
{
    "tcp_port": 24430,
    "udp_port": 24431,
    "db_path": "/var/lib/nevo/nevo_server.db",
    "threads": 4,
    "log_level": "info",
    "server_name": "NEVO Server",
    "max_users": 100,
    "welcome_message": "Welcome to NEVO!"
}
EOFJSON
        }
        log_info "server_config.json created from defaults."
    fi

    set -a
    source "$ENV_FILE" 2>/dev/null || true
    set +a
}

# ============================================================
# Commands
# ============================================================
pull_and_start() {
    log_step "Pulling latest NEVO server image..."
    check_docker
    init_config

    local image_ref
    image_ref="ghcr.io/${NEVO_REPO_OWNER:-your-username}/nevo-server:${NEVO_IMAGE_TAG:-latest}"

    echo ""
    echo -e "${CYAN}  Image: ${image_ref}${NC}"
    echo ""

    log_info "Logging into GitHub Container Registry..."
    echo "$GITHUB_TOKEN" | docker login ghcr.io -u "${GITHUB_USER:-}" --password-stdin 2>/dev/null || true

    log_info "Pulling image..."
    docker compose -f "$COMPOSE_FILE" pull

    log_info "Starting server..."
    docker compose -f "$COMPOSE_FILE" up -d --wait

    echo ""
    echo "============================================"
    echo -e "  ${GREEN}NEVO Server is running!${NC}"
    echo "============================================"
    echo ""
    echo "  Image:       $image_ref"
    echo "  TCP Control: $(docker port "$CONTAINER_NAME" 24430/tcp 2>/dev/null || echo 'N/A')"
    echo "  UDP Voice:   $(docker port "$CONTAINER_NAME" 24431/udp 2>/dev/null || echo 'N/A')"
    echo "  UDP Video:   $(docker port "$CONTAINER_NAME" 24432/udp 2>/dev/null || echo 'N/A')"
    echo "  Health:      $(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo 'N/A')"
    echo ""
    echo "  Client connect → $(hostname -I 2>/dev/null | awk '{print $1}' || echo '<server-ip>'):${NEVO_TCP_PORT:-24430}"
    echo ""
    echo "============================================"
    echo ""
}

update_server() {
    log_step "Updating NEVO server image..."
    check_docker
    init_config

    log_info "Stopping current server..."
    docker compose -f "$COMPOSE_FILE" down --timeout 30 2>/dev/null || true

    log_info "Pulling latest image..."
    docker compose -f "$COMPOSE_FILE" pull

    log_info "Starting updated server..."
    docker compose -f "$COMPOSE_FILE" up -d --wait

    log_info "Update complete."
}

stop_server() {
    log_step "Stopping NEVO server..."
    check_docker
    docker compose -f "$COMPOSE_FILE" down --timeout 30
    log_info "Server stopped."
}

show_status() {
    check_docker

    echo ""
    echo "========================================"
    echo "  NEVO Server Status"
    echo "========================================"
    echo ""

    if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        echo -e "  Container:    ${GREEN}Running${NC}"
        echo "  Status:       $(docker inspect --format='{{.State.Status}}' "$CONTAINER_NAME")"
        echo "  Health:       $(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo 'N/A')"
        echo "  Image:        $(docker inspect --format='{{.Config.Image}}' "$CONTAINER_NAME")"
        echo "  Started:      $(docker inspect --format='{{.State.StartedAt}}' "$CONTAINER_NAME")"
        echo "  CPU:          $(docker stats --no-stream --format '{{.CPUPerc}}' "$CONTAINER_NAME" 2>/dev/null)"
        echo "  Memory:       $(docker stats --no-stream --format '{{.MemUsage}}' "$CONTAINER_NAME" 2>/dev/null)"
        echo ""
        echo "  Ports:"
        docker port "$CONTAINER_NAME" | while read -r line; do
            echo "    $line"
        done
    else
        echo -e "  Container:    ${RED}Not Running${NC}"
    fi

    echo ""
    echo "========================================"
    echo ""
}

show_logs() {
    check_docker
    docker compose -f "$COMPOSE_FILE" logs --tail=100 -f
}

# ============================================================
# Main
# ============================================================
case "${1:-start}" in
    start|up|run)
        pull_and_start
        ;;
    update|pull)
        update_server
        ;;
    stop|down)
        stop_server
        ;;
    status|ps|info)
        show_status
        ;;
    logs)
        show_logs
        ;;
    *)
        echo "NEVO Server - One-Click Pull & Deploy"
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  start    Pull latest image and start server (default)"
        echo "  update   Pull latest image and restart server"
        echo "  stop     Stop server"
        echo "  status   Show server running status"
        echo "  logs     View server logs"
        echo ""
        echo "First time setup:"
        echo "  1. Edit .env → set NEVO_REPO_OWNER to your GitHub username"
        echo "  2. bash pull.sh start"
        echo ""
        exit 0
        ;;
esac