#!/bin/bash
# Run this from WSL to start the ZMK dev container.
# Usage: ./scripts/start.sh
#
# Prerequisites:
#   - Docker Desktop running
#   - ZMK cloned at ~/tg/zmk
#   - devcontainer CLI installed: npm install -g @devcontainers/cli

set -e

ZMK_DIR="$HOME/tg/zmk"
TYPEGLIDE_DIR="$HOME/tg/zmk-typeglide"
TG_DIR="$HOME/tg"

# Check ZMK is cloned
if [ ! -d "$ZMK_DIR" ]; then
    echo "ZMK not found at $ZMK_DIR. Cloning..."
    git clone https://github.com/zmkfirmware/zmk "$ZMK_DIR"
fi

# Patch devcontainer.json to use bind mounts
DEVCONTAINER="$ZMK_DIR/.devcontainer/devcontainer.json"
if grep -q "zmk-config,target" "$DEVCONTAINER" && grep -q "type=volume" "$DEVCONTAINER"; then
    echo "Patching devcontainer.json to use bind mounts..."
    sed -i \
        "s|type=volume,source=zmk-config,target=/workspaces/zmk-config|type=bind,source=$TYPEGLIDE_DIR,target=/workspaces/zmk-config|" \
        "$DEVCONTAINER"
    sed -i \
        "s|type=volume,source=zmk-modules,target=/workspaces/zmk-modules|type=bind,source=$TG_DIR,target=/workspaces/zmk-modules|" \
        "$DEVCONTAINER"
    echo "Patched."
fi

# Check if container is already running
CONTAINER=$(docker ps \
    --filter "label=devcontainer.local_folder=$ZMK_DIR" \
    --format "{{.ID}}" | head -1)

if [ -n "$CONTAINER" ]; then
    echo "Container already running: $CONTAINER"
    exit 0
fi

echo "Starting ZMK dev container..."
devcontainer up --workspace-folder "$ZMK_DIR"

CONTAINER=$(docker ps \
    --filter "label=devcontainer.local_folder=$ZMK_DIR" \
    --format "{{.ID}}" | head -1)

# Run west init/update if not already done
if [ ! -d "$ZMK_DIR/.west" ]; then
    echo "Initializing west workspace..."
    # Pass proxy into container if set on host
    PROXY_CONF=""
    if [ -n "$http_proxy" ]; then
        PROXY_CONF="git config --global http.proxy $http_proxy && git config --global https.proxy $http_proxy && git config --global http.sslVerify false &&"
    fi

    docker exec -i "$CONTAINER" bash -c "
        $PROXY_CONF
        cd /workspaces/zmk && west init -l app/ && west update
    "
    echo "Restarting container after west init..."
    docker stop "$CONTAINER"
    devcontainer up --workspace-folder "$ZMK_DIR"
fi

echo ""
echo "ZMK dev container is ready."
echo "Build with: ~/tg/zmk-typeglide/scripts/run.sh [left|right|both] [clean]"
