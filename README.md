# zmk-typeglide

ZMK shield for the TypeGlide — a 22-key wireless split keyboard based on the nice!nano v2.

## Layout (per half)

```
ring  mid  idx
ring  mid  idx
ring  mid  idx
pinky [X]  thumb
```

4×3 matrix per half (1 spare at row 3 col 1), 11 keys per side, 22 total.

## Dev environment (Docker / dev container)

### Prerequisites

- Docker Desktop (WSL2 backend enabled)
- VS Code with the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension

### Setup

```sh
# 1. Clone ZMK
git clone https://github.com/zmkfirmware/zmk
cd zmk

# 2. Open in VS Code — accept "Reopen in Container" when prompted
code .

# 3. Inside the container, init the west workspace
west init -l app/
west update
west zephyr-export
pip3 install --user -r zephyr/scripts/requirements.txt
```

### Build

```sh
# Create Docker volumes first (run in WSL before opening container)
docker volume create --driver local -o o=bind -o type=none \
  -o device="/home/homer/tg/zmk-typeglide" zmk-config
docker volume create --driver local -o o=bind -o type=none \
  -o device="/home/homer/tg" zmk-modules
```

Then inside the container (`cd app` first):

```sh
# Left half (central)
west build -d build/left -b nice_nano_v2//zmk -- \
  -DSHIELD=typeglide_left \
  -DZMK_EXTRA_MODULES="/workspaces/zmk-modules/zmk-typeglide" \
  -DZMK_CONFIG="/workspaces/zmk-config/config"

# Right half (peripheral)
west build -d build/right -b nice_nano_v2//zmk -- \
  -DSHIELD=typeglide_right \
  -DZMK_EXTRA_MODULES="/workspaces/zmk-modules/zmk-typeglide" \
  -DZMK_CONFIG="/workspaces/zmk-config/config"
```

Firmware outputs to `build/left/zephyr/zmk.uf2` and `build/right/zephyr/zmk.uf2`.

## Pin assignment

Edit `typeglide_left.overlay` and `typeglide_right.overlay` to match your PCB routing.
Current placeholders use `&pro_micro` pins 4–10 (D4–D10).

| Signal | Pin |
| ------ | --- |
| Row 0  | D4  |
| Row 1  | D5  |
| Row 2  | D6  |
| Row 3  | D7  |
| Col 0  | D8  |
| Col 1  | D9  |
| Col 2  | D10 |

Diode direction is `col2row`. Change in the overlay if your PCB uses `row2col`.

Left half: col 0 = ring (outer), col 2 = index (inner), row 3 col 0 = pinky, row 3 col 2 = thumb.
Right half: col 0 = index (inner), col 2 = ring (outer), row 3 col 0 = thumb, row 3 col 2 = pinky.
