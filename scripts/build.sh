#!/bin/bash
# Run this inside the ZMK dev container.
# Usage: bash /workspaces/zmk-config/scripts/build.sh [left|right|both] [clean]
#
# Examples:
#   bash /workspaces/zmk-config/scripts/build.sh both
#   bash /workspaces/zmk-config/scripts/build.sh left clean
#   bash /workspaces/zmk-config/scripts/build.sh right

set -e

SIDE=${1:-both}
CLEAN=${2:-}

BOARD="nice_nano//zmk"
EXTRA_MODULES="/workspaces/zmk-modules/zmk-typeglide"
CONFIG="/workspaces/zmk-config/config"
PRISTINE=""

# Suppress git safe directory warning
git config --global --add safe.directory /workspaces/zmk 2>/dev/null || true

if [ "$CLEAN" = "clean" ]; then
    PRISTINE="-p"
fi

cd /workspaces/zmk/app

build_left() {
    echo ">>> Building left half..."
    west build $PRISTINE -d build/left -b $BOARD -- \
        -DSHIELD=typeglide_left \
        -DZMK_EXTRA_MODULES="$EXTRA_MODULES" \
        -DZMK_CONFIG="$CONFIG"
    echo ">>> Left done: build/left/zephyr/zmk.uf2"
}

build_right() {
    echo ">>> Building right half..."
    west build $PRISTINE -d build/right -b $BOARD -- \
        -DSHIELD=typeglide_right \
        -DZMK_EXTRA_MODULES="$EXTRA_MODULES" \
        -DZMK_CONFIG="$CONFIG"
    echo ">>> Right done: build/right/zephyr/zmk.uf2"
}

case $SIDE in
    left)  build_left ;;
    right) build_right ;;
    both)  build_left; build_right ;;
    *)
        echo "Usage: $0 [left|right|both] [clean]"
        exit 1
        ;;
esac

echo ""
echo "Firmware output:"
[[ "$SIDE" != "right" ]] && echo "  Left:  ~/tg/zmk/app/build/left/zephyr/zmk.uf2"
[[ "$SIDE" != "left"  ]] && echo "  Right: ~/tg/zmk/app/build/right/zephyr/zmk.uf2"
