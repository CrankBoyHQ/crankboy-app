#!/bin/sh
# Build CrankBoy.pdx inside the crankboy-docker container (amd64 linux).
# Usage: scripts/docker-build.sh [target] [extra make args]
# Targets: device (default), all, clean, fonts, db.

set -e

TARGET="${1:-device}"
case "$TARGET" in
    device | all | clean | fonts | db) ;;
    *)
        echo "ERROR: unsupported target '$TARGET'." >&2
        echo "Supported targets: device (default), all, clean, fonts, db." >&2
        echo "The simulator target needs a host build, not the container." >&2
        exit 1
        ;;
esac
shift 2> /dev/null || true

cd "$(dirname "$0")/.."

if ! command -v docker > /dev/null 2>&1; then
    echo "ERROR: docker not found." >&2
    exit 1
fi

if [ ! -f toolchain/linux/gcc-arm-none-eabi-*.tar.bz2 ]; then
    echo "ERROR: toolchain submodule missing." >&2
    echo "Run: git submodule update --init toolchain" >&2
    exit 1
fi

DOCKER_DIR="${CRANKBOY_DOCKER_DIR:-$PWD/docker}"

if [ ! -f "$DOCKER_DIR/Dockerfile" ]; then
    echo "ERROR: docker submodule not initialized at $DOCKER_DIR" >&2
    echo "Set CRANKBOY_DOCKER_DIR to your crankboy-docker checkout, or:" >&2
    echo "  git submodule update --init docker" >&2
    exit 1
fi

echo "==> building docker image"
docker buildx build --platform linux/amd64 -t crankboy-build --load "$DOCKER_DIR"

mkdir -p .docker-cache

echo "==> running build container"
attempt=0
while :; do
    if docker run --rm --platform linux/amd64 \
        --ulimit core=0 \
        -v "$(pwd)":/work -w /work \
        -v "$(pwd)/.docker-cache":/opt/cache \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        crankboy-build "$TARGET" "$@"; then
        exit 0
    fi
    status=$?
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 2 ]; then
        echo "ERROR: build failed twice, giving up." >&2
        exit "$status"
    fi
    echo "==> build failed (exit $status); likely emulator flakiness, retrying once" >&2
done
