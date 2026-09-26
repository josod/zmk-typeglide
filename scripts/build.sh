#!/bin/bash
# Run this inside the ZMK Ubuntu/WSL dev container.
#
# Build:
#   ./build.sh left
#   ./build.sh right
#   ./build.sh both
#   ./build.sh left clean
#
# Build + flash:
#   ./build.sh left -left-target /mnt/d
#   ./build.sh right -right-target /mnt/d
#
# Flash existing firmware:
#   ./build.sh flash left
#   ./build.sh flash right

set -e

BOARD="nice_nano//zmk"
EXTRA_MODULES="/workspaces/zmk-modules/zmk-typeglide;/workspaces/zmk-modules/zmk-pmw3610-driver;/workspaces/zmk-modules/zmk-input-processor-rotate-plane"
CONFIG="/workspaces/zmk-config/config"

SIDE=""
CLEAN=""
LEFT_TARGET=""
RIGHT_TARGET=""
TARGET=""

# Suppress git safe-directory warning
git config --global --add safe.directory /workspaces/zmk 2>/dev/null || true

cd /workspaces/zmk/app


usage() {
    echo "Usage:"
    echo ""
    echo "  Build:"
    echo "    $0 [left|right|both] [clean]"
    echo ""
    echo "  Build + flash:"
    echo "    $0 left  [-left-target PATH]"
    echo "    $0 right [-right-target PATH]"
    echo ""
    echo "  Flash existing firmware:"
    echo "    $0 flash [left|right]"
    echo ""
    echo "Examples:"
    echo "    $0 both"
    echo "    $0 left clean"
    echo "    $0 left -left-target /mnt/d"
    echo "    $0 flash left"
    echo "    $0 flash right"
}


# ----------------------------------------------------------------------
# Find a Nice!Nano UF2 bootloader drive
# ----------------------------------------------------------------------

find_uf2_target() {
    local target
    local drive
    local mountpoint

    # ---------------------------------------------------------------
    # 1. First look at drives already mounted by WSL
    # ---------------------------------------------------------------

    for target in /mnt/*; do
        [[ -d "$target" ]] || continue

        if [[ -f "$target/INFO_UF2.TXT" ]]; then
            echo "$target"
            return 0
        fi
    done

    # ---------------------------------------------------------------
    # 2. If not found, try mounting Windows drives
    #
    # Skip C because it is normally already mounted by WSL.
    # Start at D since your Nice!Nano has appeared there.
    # ---------------------------------------------------------------

    for drive in {d..z}; do
        mountpoint="/mnt/$drive"

        # Create mount point if necessary.
        sudo mkdir -p "$mountpoint" 2>/dev/null || continue

        # Mount Windows drive if it isn't already mounted.
        if ! mountpoint -q "$mountpoint" 2>/dev/null; then
            sudo mount -t drvfs "${drive^^}:" "$mountpoint" 2>/dev/null || continue
        fi

        # UF2 bootloader identification.
        if [[ -f "$mountpoint/INFO_UF2.TXT" ]]; then
            echo "$mountpoint"
            return 0
        fi
    done

    return 1
}


# ----------------------------------------------------------------------
# Copy firmware to UF2 target
# ----------------------------------------------------------------------

copy_firmware() {
    local side="$1"
    local target="$2"
    local firmware="build/$side/zephyr/zmk.uf2"

    if [[ -z "$target" ]]; then
        return
    fi

    if [[ ! -f "$firmware" ]]; then
        echo "ERROR: Firmware does not exist:"
        echo "       $firmware"
        echo ""
        echo "Build it first."
        exit 1
    fi

    if [[ ! -d "$target" ]]; then
        echo "ERROR: Target does not exist:"
        echo "       $target"
        exit 1
    fi

    if [[ ! -f "$target/INFO_UF2.TXT" ]]; then
        echo "ERROR: Target does not look like a Nice!Nano UF2 bootloader:"
        echo "       $target"
        exit 1
    fi

    echo ">>> Flashing $side -> $target"

    cp "$firmware" "$target/zmk.uf2"

    echo ">>> UF2 copied successfully."
    echo ">>> Nice!Nano is rebooting."
    echo ">>> The bootloader drive will disappear."
}


# ----------------------------------------------------------------------
# Flash an existing firmware build
# ----------------------------------------------------------------------

flash_existing() {
    local side="$1"
    local target

    if [[ "$side" != "left" && "$side" != "right" ]]; then
        echo "ERROR: flash requires 'left' or 'right'"
        usage
        exit 1
    fi

    target=$(find_uf2_target) || {
        echo ""
        echo "ERROR: No Nice!Nano UF2 bootloader found."
        echo ""
        echo "Put the Nice!Nano into bootloader mode and try again."
        exit 1
    }

    echo ">>> Found Nice!Nano UF2 bootloader: $target"

    copy_firmware "$side" "$target"
}


# ----------------------------------------------------------------------
# Build left
# ----------------------------------------------------------------------

build_left() {
    echo ">>> Building left half..."

    local pristine=""

    if [[ "$CLEAN" == "clean" ]]; then
        pristine="-p"
    fi

    west build $pristine \
        -d build/left \
        -b "$BOARD" \
        -S zmk-usb-logging \
        -- \
        -DSHIELD=typeglide_left \
        -DZMK_EXTRA_MODULES="$EXTRA_MODULES" \
        -DZMK_CONFIG="$CONFIG"

    echo ">>> Left done: build/left/zephyr/zmk.uf2"

    copy_firmware left "$LEFT_TARGET"
}


# ----------------------------------------------------------------------
# Build right
# ----------------------------------------------------------------------

build_right() {
    echo ">>> Building right half..."

    local pristine=""

    if [[ "$CLEAN" == "clean" ]]; then
        pristine="-p"
    fi

    west build $pristine \
        -d build/right \
        -b "$BOARD" \
        -S zmk-usb-logging \
        -- \
        -DSHIELD=typeglide_right \
        -DZMK_EXTRA_MODULES="$EXTRA_MODULES" \
        -DZMK_CONFIG="$CONFIG"

    echo ">>> Right done: build/right/zephyr/zmk.uf2"

    copy_firmware right "$RIGHT_TARGET"
}


# ----------------------------------------------------------------------
# Parse first command
# ----------------------------------------------------------------------

if [[ $# -eq 0 ]]; then
    SIDE="both"
else
    SIDE="$1"
    shift
fi


# ----------------------------------------------------------------------
# Flash-only mode
# ----------------------------------------------------------------------

if [[ "$SIDE" == "flash" ]]; then

    if [[ $# -eq 0 ]]; then
        echo "ERROR: flash requires left or right"
        usage
        exit 1
    fi

    SIDE="$1"
    shift

    if [[ $# -gt 0 ]]; then
        echo "ERROR: flash does not take a target path."
        echo "       The Nice!Nano UF2 drive is detected automatically."
        exit 1
    fi

    flash_existing "$SIDE"
    exit 0
fi


# ----------------------------------------------------------------------
# Build-mode arguments
# ----------------------------------------------------------------------

while [[ $# -gt 0 ]]; do

    case "$1" in

        clean)
            CLEAN="clean"
            shift
            ;;

        -left-target)
            [[ $# -ge 2 ]] || {
                echo "ERROR: -left-target requires a path"
                exit 1
            }

            LEFT_TARGET="$2"
            shift 2
            ;;

        -right-target)
            [[ $# -ge 2 ]] || {
                echo "ERROR: -right-target requires a path"
                exit 1
            }

            RIGHT_TARGET="$2"
            shift 2
            ;;

        *)
            echo "ERROR: Unknown argument: $1"
            usage
            exit 1
            ;;

    esac

done


# ----------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------

case "$SIDE" in

    left)
        build_left
        ;;

    right)
        build_right
        ;;

    both)
        build_left
        build_right
        ;;

    *)
        echo "ERROR: Invalid side: $SIDE"
        usage
        exit 1
        ;;

esac


# ----------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------
echo ""
echo "Firmware output:"

[[ "$SIDE" != "right" ]] && \
    echo "  Left:  build/left/zephyr/zmk.uf2"

[[ "$SIDE" != "left" ]] && \
    echo "  Right: build/right/zephyr/zmk.uf2"

exit 0
