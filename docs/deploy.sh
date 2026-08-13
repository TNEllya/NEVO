#!/bin/bash
# ============================================================
# NEVO Server - Docker Deployment Script
# ============================================================
# Usage:
#   ./deploy.sh build        Build the Docker image
#   ./deploy.sh start        Start the server
#   ./deploy.sh stop         Stop the server
#   ./deploy.sh restart      Restart the server
#   ./deploy.sh status       Show server status
#   ./deploy.sh logs         View server logs
#   ./deploy.sh clean        Remove all data and containers
#   ./deploy.sh update       Update and restart
#   ./deploy.sh shell        Open a shell in the container
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

COMPOSE_FILE="docker-compose.yml"
COMPOSE_PULL_FILE="docker-compose.pull.yml"
ENV_FILE=".env"
CONFIG_FILE="server_config.json"
CONTAINER_NAME="nevo-server"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $(date '+%H:%M:%S') $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $(date '+%H:%M:%S') $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $(date '+%H:%M:%S') $*"; }
log_step()  { echo -e "${BLUE}[STEP]${NC}  $(date '+%H:%M:%S') $*"; }

check_docker() {
    if ! command -v docker &>/dev/null; then
        log_error "Docker is not installed. Please install Docker first."
        exit 1
    fi
    if ! docker compose version &>/dev/null; then
        log_error "Docker Compose V2 is required. Please upgrade Docker."
        exit 1
    fi
}

check_ports() {
    local tcp_port="${NEVO_TCP_PORT:-24430}"
    local udp_port="${NEVO_UDP_PORT:-24431}"
    local video_port="${NEVO_VIDEO_UDP_PORT:-24432}"

    for port in "$tcp_port" "$udp_port" "$video_port"; do
        if ss -tuln 2>/dev/null | grep -q ":$port " || netstat -tuln 2>/dev/null | grep -q ":$port "; then
            log_warn "Port $port appears to be in use."
        fi
    done
}

init_config() {
    if [ ! -f "$ENV_FILE" ]; then
        if [ -f ".env.example" ]; then
            cp .env.example "$ENV_FILE"
            log_info "Created .env from .env.example. Edit it to customize settings."
        else
            log_warn "No .env.example found. Using defaults."
        fi
    fi

    if [ ! -f "$CONFIG_FILE" ]; then
        if [ -f "server_config.example.json" ]; then
            cp server_config.example.json "$CONFIG_FILE"
            log_info "Created server_config.json from example."
        fi
    fi

    source "$ENV_FILE" 2>/dev/null || true
}

pull_and_start() {
    log_step "Pulling NEVO server from registry..."
    check_docker
    init_config

    local image_ref
    image_ref="ghcr.io/${NEVO_REPO_OWNER:-your-username}/nevo-server:${NEVO_IMAGE_TAG:-latest}"
    log_info "Image: $image_ref"

    log_info "Logging into GHCR..."
    echo "${GITHUB_TOKEN:-}" | docker login ghcr.io -u "${GITHUB_USER:-}" --password-stdin 2>/dev/null || true

    log_info "Pulling image..."
    docker compose -f "$COMPOSE_PULL_FILE" pull

    log_info "Starting server..."
    docker compose -f "$COMPOSE_PULL_FILE" up -d --wait

    log_info "NEVO server started from registry image!"
    echo ""
    echo "  Image:  $image_ref"
    echo "  Health: $(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo 'N/A')"
    echo ""
}

build_image() {
    log_step "Building NEVO server Docker image..."
    check_docker
    init_config

    docker compose -f "$COMPOSE_FILE" build \
        --build-arg BUILDKIT_INLINE_CACHE=1 \
        --no-cache

    log_info "Docker image built successfully."
}

start_server() {
    log_step "Starting NEVO server..."
    check_docker
    init_config
    check_ports

    docker compose -f "$COMPOSE_FILE" up -d --wait

    if [ $? -eq 0 ]; then
        log_info "NEVO server started successfully!"
        echo ""
        echo "  TCP Control:  $(docker port "$CONTAINER_NAME" 24430/tcp 2>/dev/null || echo "N/A")"
        echo "  UDP Voice:    $(docker port "$CONTAINER_NAME" 24431/udp 2>/dev/null || echo "N/A")"
        echo "  UDP Video:    $(docker port "$CONTAINER_NAME" 24432/udp 2>/dev/null || echo "N/A")"
        echo "  Status:       $(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo "N/A")"
        echo ""
    else
        log_error "Failed to start NEVO server."
        exit 1
    fi
}

stop_server() {
    log_step "Stopping NEVO server..."
    check_docker

    docker compose -f "$COMPOSE_FILE" down --timeout 30
    log_info "NEVO server stopped."
}

restart_server() {
    log_step "Restarting NEVO server..."
    stop_server
    sleep 2
    start_server
}

update_server() {
    log_step "Updating NEVO server..."
    check_docker

    log_info "Pulling latest changes..."
    git pull --rebase 2>/dev/null || log_warn "Not a git repository, skipping pull."

    stop_server

    log_info "Rebuilding image..."
    build_image

    start_server
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
        echo "  Name:         $(docker inspect --format='{{.Name}}' "$CONTAINER_NAME" | sed 's|/||')"
        echo "  Status:       $(docker inspect --format='{{.State.Status}}' "$CONTAINER_NAME")"
        echo "  Health:       $(docker inspect --format='{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo 'N/A')"
        echo "  Started:      $(docker inspect --format='{{.State.StartedAt}}' "$CONTAINER_NAME")"
        echo "  CPU:          $(docker stats --no-stream --format '{{.CPUPerc}}' "$CONTAINER_NAME" 2>/dev/null)"
        echo "  Memory:       $(docker stats --no-stream --format '{{.MemUsage}}' "$CONTAINER_NAME" 2>/dev/null)"
        echo ""
        echo "  Ports:"
        docker port "$CONTAINER_NAME" 2>/dev/null | while read line; do
            echo "    $line"
        done
        echo ""
        echo "  Volumes:"
        docker inspect --format='{{range .Mounts}}{{printf "    %s → %s\n" .Source .Destination}}{{end}}' "$CONTAINER_NAME" 2>/dev/null
        echo ""
    else
        echo -e "  Container:    ${RED}Not Running${NC}"
        echo ""
    fi

    echo "========================================"
    echo ""
}

show_logs() {
    check_docker
    docker compose -f "$COMPOSE_FILE" logs --tail=100 -f
}

clean_all() {
    log_step "WARNING: This will remove all NEVO containers, volumes, and data!"
    read -p "Are you sure? (yes/no): " confirm
    if [ "$confirm" != "yes" ]; then
        log_info "Clean aborted."
        return
    fi

    check_docker
    docker compose -f "$COMPOSE_FILE" down -v --remove-orphans --timeout 30
    docker volume rm -f nevo_data nevo_config 2>/dev/null || true

    log_info "All NEVO data cleaned."
}

open_shell() {
    check_docker
    if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        docker exec -it "$CONTAINER_NAME" /bin/bash
    else
        log_error "Container is not running."
        exit 1
    fi
}

# ============================================================
# Main
# ============================================================
case "${1:-}" in
    build)
        build_image
        ;;
    pull|setup)
        pull_and_start
        ;;
    start|up)
        start_server
        ;;
    stop|down)
        stop_server
        ;;
    restart)
        restart_server
        ;;
    status|ps)
        show_status
        ;;
    logs)
        show_logs
        ;;
    update)
        update_server
        ;;
    clean)
        clean_all
        ;;
    shell|bash)
        open_shell
        ;;
    *)
        echo "NEVO Server Deployment Tool"
        echo ""
        echo "Usage: $0 {build|pull|start|stop|restart|status|logs|update|clean|shell}"
        echo ""
        echo "Commands:"
        echo "  build       Build the Docker image locally"
        echo "  pull        Pull image from registry & start (no build)"
        echo "  start       Start the server (docker compose up -d)"
        echo "  stop        Stop the server (docker compose down)"
        echo "  restart     Restart the server"
        echo "  status      Show server status and resource usage"
        echo "  logs        View server logs (tail -f)"
        echo "  update      Git pull + rebuild + restart"
        echo "  clean       Remove all containers and volumes"
        echo "  shell       Open a bash shell in the container"
        echo ""
        exit 0
        ;;
esac