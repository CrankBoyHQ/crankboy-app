#!/bin/sh
# Build CrankBoy.pdx inside the crankboy-docker container (amd64 linux).
# Thin wrapper around docker/build-in-docker.sh with a CrankBoy-specific
# target allowlist. The simulator target needs a host build.
# Usage: scripts/docker-build.sh [target] [extra make args...]
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

DOCKER_DIR="${CRANKBOY_DOCKER_DIR:-$PWD/docker}"

if [ ! -f "$DOCKER_DIR/build-in-docker.sh" ]; then
    echo "ERROR: docker submodule not initialized at $DOCKER_DIR" >&2
    echo "Set CRANKBOY_DOCKER_DIR to your crankboy-docker checkout, or:" >&2
    echo "  git submodule update --init docker" >&2
    exit 1
fi

exec "$DOCKER_DIR/build-in-docker.sh" "$TARGET" "$@"
