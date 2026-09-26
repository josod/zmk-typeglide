#!/bin/bash
# Run this from WSL to build inside the ZMK dev container.
# Usage: ./scripts/run.sh [left|right|both] [clean]
#
# The ZMK dev container must already be running:
#   devcontainer up --workspace-folder ~/tg/zmk

set -e

COMMAND=${1:-build}
SIDE=${2:-both}
CLEAN=${3:-}

if [ "$COMMAND" != "build" ] && \
   [ "$COMMAND" != "flash" ] && \
   [ "$COMMAND" != "both" ]; then
    echo "ERROR: Unknown command: $COMMAND"
    echo "Usage: $0 {build|flash|both} {left|right|both}"
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

UF2_DRIVE=""

for letter in {D..Z}; do
    result=$(cmd.exe /c "if exist ${letter}:\\INFO_UF2.TXT echo ${letter}:" 2>/dev/null | tr -d '\r')

    if [ "$result" = "$letter:" ]; then
        mountpoint="/mnt/$(echo "$letter" | tr '[:upper:]' '[:lower:]')"

        if ! mountpoint -q "$mountpoint"; then
            sudo mkdir -p "$mountpoint"
            sudo mount -t drvfs "${letter}:" "$mountpoint"
        fi

        if grep -q "Board-ID: nRF52840-nicenano-v2" "$mountpoint/INFO_UF2.TXT" 2>/dev/null; then
            UF2_DRIVE="$mountpoint"
            break
        fi
    fi
done
if [ "$COMMAND" = "build" ]; then
    echo "Building $SIDE..."
    docker exec -it "$CONTAINER" \
        bash /workspaces/zmk-config/scripts/build.sh "$SIDE"
    echo "Build complete."
    exit 0
fi


if [ "$COMMAND" = "flash" ]; then
    if [ "$SIDE" = "both" ]; then
        for side in left right; do
            echo
            echo "Put the $side Nice!Nano into bootloader mode."
            read -r -p "Press Enter when ready..." </dev/tty

            if ! find_uf2_drive; then
                echo "ERROR: No Nice!Nano UF2 drive found."
                exit 1
            fi

            echo "Flashing $side..."
            docker cp \
                "$CONTAINER:/workspaces/zmk/app/build/$SIDE/zephyr/zmk.uf2" \
                "$UF2_DRIVE/CURRENT.UF2" || true

            echo "Flash $side complete."
        done

        exit 0
    fi

    if [ "$SIDE" != "left" ] && [ "$SIDE" != "right" ]; then
        echo "ERROR: flash requires left, right, or both."
        exit 1
    fi

    echo "Put the $SIDE Nice!Nano into bootloader mode."
    read -r -p "Press Enter when ready..." </dev/tty

    if ! find_uf2_drive; then
        echo "ERROR: No Nice!Nano UF2 drive found."
        exit 1
    fi

    echo "Flashing $SIDE..."
    docker cp \
        "$CONTAINER:/workspaces/zmk/app/build/$SIDE/zephyr/zmk.uf2" \
        "$UF2_DRIVE/CURRENT.UF2"

    echo "Flash complete."
    exit 0
fi

if [ "$COMMAND" = "both" ]; then
    if [ "$SIDE" != "left" ] && [ "$SIDE" != "right" ] && [ "$SIDE" != "both" ]; then
        echo "ERROR: both requires left, right, or both."
        exit 1
    fi

    if [ "$SIDE" = "both" ]; then
        for side in left right; do
            echo "Building $side..."
            docker exec -it "$CONTAINER" \
                bash /workspaces/zmk-config/scripts/build.sh "$side"

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
                "$UF2_DRIVE/CURRENT.UF2" || true

            echo "Flash $side complete."
        done

        exit 0
    fi

    echo "Building $SIDE..."
    docker exec -it "$CONTAINER" \
        bash /workspaces/zmk-config/scripts/build.sh "$SIDE"

    echo
    echo "Put the $SIDE Nice!Nano into bootloader mode."
    read -r -p "Press Enter when ready..." </dev/tty

    if ! find_uf2_drive; then
        echo "ERROR: No Nice!Nano UF2 drive found."
        exit 1
    fi

    echo "Flashing $SIDE..."
    docker cp \
        "$CONTAINER:/workspaces/zmk/app/build/$SIDE/zephyr/zmk.uf2" \
        "$UF2_DRIVE/CURRENT.UF2" || true

    echo "Flash $side complete."


    exit 0
fi
echo "ERROR: Unknown command: $COMMAND"
exit 1
