#!/bin/bash
# Run this from WSL to build inside the ZMK dev container.
# Usage: ./scripts/run.sh [left|right|both] [clean]
#
# The ZMK dev container must already be running:
#   devcontainer up --workspace-folder ~/tg/zmk

set -e

SIDE=${1:-both}
CLEAN=${2:-}

# Find the running ZMK dev container by its devcontainer label
CONTAINER=$(docker ps \
    --filter "label=devcontainer.local_folder=/home/homer/tg/zmk" \
    --format "{{.ID}}" | head -1)

if [ -z "$CONTAINER" ]; then
    echo "ZMK container is not running. Start it with:"
    echo "  devcontainer up --workspace-folder ~/tg/zmk"
    exit 1
fi

echo "Using container: $CONTAINER"
docker exec -it "$CONTAINER" \
    bash /workspaces/zmk-config/scripts/build.sh "$SIDE" "$CLEAN"
