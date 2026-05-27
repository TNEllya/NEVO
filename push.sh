#!/bin/bash
# ============================================================
# NEVO Server - Push Docker Image to GHCR
# ============================================================
# Manually build and push the Docker image to GitHub Container
# Registry. For automated publishing, use the GitHub Actions
# workflow at .github/workflows/docker-publish.yml
#
# Usage:
#   bash push.sh                    Push with :latest tag
#   bash push.sh v0.1.0             Push with :v0.1.0 tag
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

REGISTRY="ghcr.io"
ENV_FILE=".env"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

if [ -f "$ENV_FILE" ]; then
    set -a
    source "$ENV_FILE" 2>/dev/null || true
    set +a
fi

REPO_OWNER="${NEVO_REPO_OWNER:-your-username}"
IMAGE_NAME="${REGISTRY}/${REPO_OWNER}/nevo-server"
TAG="${1:-latest}"

check_docker() {
    if ! command -v docker &>/dev/null; then
        log_error "Docker is not installed."
        exit 1
    fi
}

check_login() {
    if ! docker pull "${IMAGE_NAME}:${TAG}" &>/dev/null 2>&1; then
        log_info "Logging into $REGISTRY..."
        echo "${GITHUB_TOKEN}" | docker login "$REGISTRY" -u "${GITHUB_USER}" --password-stdin 2>/dev/null || {
            read -rp "  GitHub Username: " gh_user
            read -rsp "  GitHub Token: " gh_token
            echo ""
            echo "$gh_token" | docker login "$REGISTRY" -u "$gh_user" --password-stdin
        }
    fi
}

push_image() {
    log_info "Building image: ${IMAGE_NAME}:${TAG}"
    docker build -t "${IMAGE_NAME}:${TAG}" .

    if [ "$TAG" != "latest" ]; then
        docker tag "${IMAGE_NAME}:${TAG}" "${IMAGE_NAME}:latest"
        log_info "Also tagged as: ${IMAGE_NAME}:latest"
    fi

    log_info "Pushing ${IMAGE_NAME}:${TAG}..."
    docker push "${IMAGE_NAME}:${TAG}"

    if [ "$TAG" != "latest" ]; then
        docker push "${IMAGE_NAME}:latest"
    fi

    echo ""
    log_info "Image published successfully!"
    echo ""
    echo "  ${IMAGE_NAME}:${TAG}"
    if [ "$TAG" != "latest" ]; then
        echo "  ${IMAGE_NAME}:latest"
    fi
    echo ""
    echo "  Pull with:"
    echo "    docker pull ${IMAGE_NAME}:${TAG}"
    echo ""
}

check_docker
check_login
push_image