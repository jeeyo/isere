#!/bin/sh
# Build native_sim in a Linux Docker container (required on macOS —
# native_sim uses Linux-only POSIX arch and cannot build directly on Darwin).
#
# Prerequisites:
#   - Docker (colima or Docker Desktop)
#   - ~/zephyrproject workspace initialised (west init + west update done)
#
# Usage:
#   ./scripts/build-sim.sh          # build only
#   ./scripts/build-sim.sh run      # build then run

set -e

WORKSPACE=~/zephyrproject
COMPOSE="$WORKSPACE/docker-compose.yml"

# Build the dev image if not present
docker compose -f "$COMPOSE" build build

# Seed full Zephyr Python requirements on first run
docker compose -f "$COMPOSE" run --rm build \
    pip3 install --break-system-packages -r /workspace/zephyr/scripts/requirements.txt 2>/dev/null || true

# Build native_sim
docker compose -f "$COMPOSE" run --rm build

if [ "$1" = "run" ]; then
    echo ""
    echo "Starting native_sim... HTTP server at 192.0.2.1:80 inside container."
    echo "Use 'docker compose -f $COMPOSE run --rm run' to restart."
    docker compose -f "$COMPOSE" run --rm run
fi
