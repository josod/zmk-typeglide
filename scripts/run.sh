#!/bin/bash
# Run this from WSL to build/flash inside the ZMK dev container.
#
# Usage:
#   ./scripts/run.sh build [left|right|both] [build-args...]
#   ./scripts/run.sh flash [left|right|both]
#   ./scripts/run.sh both [left|right|both] [build-args...]
#
# Examples:
#   ./scripts/run.sh build left
#   ./scripts/run.sh build left clean
#   ./scripts/run.sh build both clean
#   ./scripts/run.sh both both clean
#
# The ZMK dev container must already be running:
#   devcontainer up --workspace-folder ~/tg/zmk

set -e

COMMAND=${1:-build}
SIDE=${2:-both}

# Everything after COMMAND and SIDE is passed to build.sh
shift 2 || true
BUILD_ARGS=("$@")

if [ "$COMMAND" != "build" ] && \
   [ "$COMMAND" != "flash" ] && \
   [ "$COMMAND" != "both" ]; then
    echo "ERROR: Unknown command: $COMMAND"
    echo "Usage: $0 {build|flash|both} {left|right|both} [build-args...]"
    exit 1
fi

if [ "$SIDE" != "left" ] && \
   [ "$SIDE" != "right" ] && \
   [ "$SIDE" != "both" ]; then
    echo "ERROR: Unknown side: $SIDE"
    echo "Usage: $0 {build|flash|both} {left|right|both} [build-args...]"
    exit 1
fi


find_uf2_drive() {
    UF2_DRIVE=""

    for letter in {D..Z}; do
        result=$(cmd.exe /c "if exist ${letter}:\\INFO_UF2.TXT echo ${letter}:" 2>/dev/null | tr -d '\r')

        if [ "$result" = "$letter:" ]; then
            mountpoint="/mnt/$(echo "$letter" | tr '[:upper:]' '[:lower:]')"

            if ! mountpoint -q "$mountpoint"; then
                sudo mkdir -p "$mountpoint"
                sudo mount -t drvfs "${letter}:" "$mountpoint"
            fi

            if grep -q "Board-ID: nRF52840-nicenano-v2" \
                "$mountpoint/INFO_UF2.TXT" 2>/dev/null; then
                UF2_DRIVE="$mountpoint"
                return 0
            fi
        fi
    done

    return 1
}


# Find the running ZMK dev container by its devcontainer label
CONTAINER=$(docker ps \
    --filter "label=devcontainer.local_folder=/home/joppe/tg/zmk" \
    --format "{{.ID}}" | head -1)

if [ -z "$CONTAINER" ]; then
    echo "ZMK container is not running. Start it with:"
    echo "  devcontainer up --workspace-folder ~/tg/zmk"
    exit 1
fi

echo "Using container: $CONTAINER"


build() {
    local side="$1"

    echo "Building $side..."

    docker exec -it "$CONTAINER" \
        bash /workspaces/zmk-config/scripts/build.sh \
        "$side" "${BUILD_ARGS[@]}"

    echo "Build $side complete."
}


flash() {
    local side="$1"

    echo
    echo "Put the $side Nice!Nano into bootloader mode."
    read -r -p "Press Enter when ready..." </dev/tty

    if ! find_uf2_drive; then
        echo "ERROR: No Nice!Nano UF2 drive found."
        exit 1
    fi

    echo "Flashing $side..."

    docker cp \
        "$CONTAINER:/workspaces/zmk/app/build/$side/zephyr/zmk.uf2" \
        "$UF2_DRIVE/CURRENT.UF2"

    echo "Flash $side complete."
}


if [ "$COMMAND" = "build" ]; then
    if [ "$SIDE" = "both" ]; then
        build left
        build right
    else
        build "$SIDE"
    fi

    exit 0
fi


if [ "$COMMAND" = "flash" ]; then
    if [ "$SIDE" = "both" ]; then
        flash left
        flash right
    else
        flash "$SIDE"
    fi

    exit 0
fi


if [ "$COMMAND" = "both" ]; then
    if [ "$SIDE" = "both" ]; then
        build left
        flash left

        build right
        flash right
    else
        build "$SIDE"
        flash "$SIDE"
    fi

    exit 0
fi


echo "ERROR: Unknown command: $COMMAND"
exit 1
