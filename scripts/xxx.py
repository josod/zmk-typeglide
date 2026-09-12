
import argparse
import re
import serial
import time
import sys
from datetime import datetime


parser = argparse.ArgumentParser(description="ZMK reconnecting serial monitor")

parser.add_argument("port")
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--reconnect", type=float, default=1.0)
parser.add_argument("--log", default="zmk-serial.log")

parser.add_argument(
    "--mode",
    choices=["all", "focus", "joystick", "4way", "joystick-capture"],
    default="all",
)

parser.add_argument("--4way", action="store_true")
parser.add_argument("--joystick", action="store_true")
parser.add_argument("--joystick-capture", action="store_true")

args = parser.parse_args()

mode = args.mode

if args.__dict__["4way"]:
    mode = "4way"

if args.joystick:
    mode = "joystick"

if args.joystick_capture:
    mode = "joystick-capture"


# ------------------------------------------------------------
# Patterns
# ------------------------------------------------------------

xy_pattern = re.compile(
    r"4WAY:\s*x=(-?\d+)\s+y=(-?\d+)"
)


# ------------------------------------------------------------
# Calibration state
# ------------------------------------------------------------

states = {
    "CENTER": {"x": [], "y": []},
    "LEFT":   {"x": [], "y": []},
    "RIGHT":  {"x": [], "y": []},
    "UP":     {"x": [], "y": []},
    "DOWN":   {"x": [], "y": []},
}

last_state = None
last_summary = 0

# This is ONLY for PC-side classification.
# It does not affect firmware.
CLASSIFY_THRESHOLD = 20

SUMMARY_INTERVAL = 2.0


def classify(x, y):
    """
    Classify the joystick for calibration purposes.

    Center is around x=60, y=130.
    These numbers are only a rough PC-side classifier.
    """

    center_x = 62
    center_y = 134

    dx = x - center_x
    dy = y - center_y

    ax = abs(dx)
    ay = abs(dy)

    if ax < CLASSIFY_THRESHOLD and ay < CLASSIFY_THRESHOLD:
        return "CENTER"

    if ax >= ay:
        return "RIGHT" if dx > 0 else "LEFT"

    return "DOWN" if dy > 0 else "UP"


def add_sample(state, x, y):
    states[state]["x"].append(x)
    states[state]["y"].append(y)


def range_text(values):
    if not values:
        return "no samples"

    return (
        f"{min(values):6d}..{max(values):6d}"
    )


def avg_text(values):
    if not values:
        return "n/a"

    return f"{sum(values) / len(values):7.1f}"


def print_summary():

    print()
    print("JOYSTICK CALIBRATION")
    print("--------------------")

    print(
        f"{'STATE':<8}"
        f"{'X RANGE':>17}"
        f"{'Y RANGE':>17}"
        f"{'X AVG':>10}"
        f"{'Y AVG':>10}"
        f"{'N':>8}"
    )

    print("-" * 70)

    for state in [
        "CENTER",
        "LEFT",
        "RIGHT",
        "UP",
        "DOWN",
    ]:

        xs = states[state]["x"]
        ys = states[state]["y"]

        print(
            f"{state:<8}"
            f"{range_text(xs):>17}"
            f"{range_text(ys):>17}"
            f"{avg_text(xs):>10}"
            f"{avg_text(ys):>10}"
            f"{len(xs):>8}"
        )

    print()


def handle_capture(x, y):

    global last_state
    global last_summary

    state = classify(x, y)

    add_sample(state, x, y)

    now = time.monotonic()

    # Only show transitions.
    if state != last_state:

        timestamp = datetime.now().strftime("%H:%M:%S")

        print(
            f"[{timestamp}] "
            f"{state:<6} "
            f"x={x:6d} "
            f"y={y:6d}"
        )

        last_state = state
        last_summary = now

    # Periodic progress.
    elif now - last_summary >= SUMMARY_INTERVAL:

        xs = states[state]["x"]
        ys = states[state]["y"]

        print(
            f"  {state:<6} "
            f"n={len(xs):4d} "
            f"X={min(xs):4d}..{max(xs):4d} "
            f"Y={min(ys):4d}..{max(ys):4d}"
        )

        last_summary = now


# ------------------------------------------------------------
# Normal filtering
# ------------------------------------------------------------

def show_line(line):

    lower = line.lower()

    if mode == "all":
        return True

    if mode == "focus":
        keywords = [
            "4way",
            "joystick",
            "direction=",
            "press",
            "release",
            "error",
            "err",
            "warn",
        ]

        return any(k in lower for k in keywords)

    if mode == "joystick":
        keywords = [
            "joystick",
            "4way",
            "direction=",
            "x=",
            "y=",
        ]

        return any(k in lower for k in keywords)

    if mode == "4way":
        keywords = [
            "4way",
            "direction=",
            "press",
            "release",
        ]

        return any(k in lower for k in keywords)

    return True


# ------------------------------------------------------------
# Serial monitor
# ------------------------------------------------------------

print()
print("ZMK SERIAL MONITOR")
print("------------------")
print(f"Port:       {args.port}")
print(f"Baud:       {args.baud}")
print(f"Log:        {args.log}")
print(f"Mode:       {mode}")
print()

if mode == "joystick-capture":
    print("Joystick calibration mode")
    print("-------------------------")
    print("Keep the joystick centered first.")
    print("Then move LEFT, RIGHT, UP and DOWN.")
    print("Raw events are saved but not displayed.")
    print()


while True:

    ser = None

    try:

        print(
            f"[{datetime.now().strftime('%H:%M:%S')}] "
            f"Connecting to {args.port}..."
        )

        ser = serial.Serial(
            args.port,
            args.baud,
            timeout=0.2,
        )

        print(
            f"[{datetime.now().strftime('%H:%M:%S')}] "
            f"Connected."
        )

        while True:

            raw = ser.readline()

            if not raw:
                continue

            line = raw.decode(
                "utf-8",
                errors="replace"
            ).rstrip()

            # ------------------------------------------------
            # Always save complete raw log
            # ------------------------------------------------

            timestamp = datetime.now().strftime(
                "%Y-%m-%d %H:%M:%S.%f"
            )[:-3]

            with open(
                args.log,
                "a",
                encoding="utf-8"
            ) as logfile:

                logfile.write(
                    f"[{timestamp}] {line}\n"
                )

            # ------------------------------------------------
            # Smart joystick capture
            # ------------------------------------------------

            if mode == "joystick-capture":

                match = xy_pattern.search(line)

                if match:

                    x = int(match.group(1))
                    y = int(match.group(2))

                    handle_capture(x, y)

                continue

            # ------------------------------------------------
            # Normal modes
            # ------------------------------------------------

            if show_line(line):
                print(line)

    except KeyboardInterrupt:

        print()
        print("Stopping monitor...")

        if ser:
            try:
                ser.close()
            except Exception:
                pass

        if mode == "joystick-capture":
            print_summary()

        sys.exit(0)

    except Exception as e:

        print(
            f"[{datetime.now().strftime('%H:%M:%S')}] "
            f"Disconnected: {e}"
        )

        if ser:
            try:
                ser.close()
            except Exception:
                pass

        print(
            f"Reconnecting in {args.reconnect:.1f}s..."
        )

        time.sleep(args.reconnect)
