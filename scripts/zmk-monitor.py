
import argparse
import re
import serial
import time
import sys
from datetime import datetime


# ------------------------------------------------------------
# Arguments
# ------------------------------------------------------------

parser = argparse.ArgumentParser(description="ZMK reconnecting serial monitor")

parser.add_argument("port", help="Serial COM port, e.g. COM7")
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--reconnect", type=float, default=1.0)
parser.add_argument("--log", default="zmk-serial.log")

parser.add_argument(
    "--mode",
    choices=["all", "focus", "joystick", "4way", "joystick-capture"],
    default="all",
)

parser.add_argument(
    "--4way",
    action="store_true",
    help="Show only 4-way related messages",
)

parser.add_argument(
    "--joystick",
    action="store_true",
    help="Show joystick related messages",
)

parser.add_argument(
    "--joystick-capture",
    action="store_true",
    help="Smart joystick calibration/capture mode",
)

args = parser.parse_args()


# ------------------------------------------------------------
# Mode
# ------------------------------------------------------------

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

direction_pattern = re.compile(
    r"direction=(-?\d+)"
)


# ------------------------------------------------------------
# Joystick capture state
# ------------------------------------------------------------

capture = {
    "x": [],
    "y": [],
}

last_displayed_state = None
last_summary_time = 0

# How much the value must change before we consider it
# interesting enough to print.
DISPLAY_CHANGE = 100

# Print a periodic summary even if nothing changes.
SUMMARY_INTERVAL = 2.0


def joystick_state(x, y):
    """
    Rough classification only.

    This is NOT the firmware's actual direction logic.
    It is only used to make the PC monitor readable.
    """

    ax = abs(x)
    ay = abs(y)

    # Large enough to avoid calling tiny noise movement.
    threshold = 300

    if ax < threshold and ay < threshold:
        return "CENTER"

    if ax >= ay:
        if x > 0:
            return "RIGHT"
        return "LEFT"

    if y > 0:
        return "DOWN"

    return "UP"


def print_capture_summary():
    if not capture["x"]:
        return

    xs = capture["x"]
    ys = capture["y"]

    print()
    print("JOYSTICK CAPTURE SUMMARY")
    print("------------------------")
    print(
        f"X: min={min(xs):6d} "
        f"max={max(xs):6d} "
        f"avg={sum(xs)/len(xs):7.1f}"
    )
    print(
        f"Y: min={min(ys):6d} "
        f"max={max(ys):6d} "
        f"avg={sum(ys)/len(ys):7.1f}"
    )
    print(f"samples: {len(xs)}")
    print()


def handle_capture(x, y):
    global last_displayed_state
    global last_summary_time

    capture["x"].append(x)
    capture["y"].append(y)

    state = joystick_state(x, y)

    now = time.monotonic()

    # Only print when the logical state changes.
    if state != last_displayed_state:
        timestamp = datetime.now().strftime("%H:%M:%S")

        print(
            f"[{timestamp}] "
            f"{state:<6} "
            f"x={x:6d} y={y:6d}"
        )

        last_displayed_state = state
        last_summary_time = now

    # Periodic summary while capturing.
    elif now - last_summary_time >= SUMMARY_INTERVAL:
        xs = capture["x"]
        ys = capture["y"]

        print(
            f"  samples={len(xs):5d} "
            f"X={min(xs):6d}..{max(xs):6d} "
            f"Y={min(ys):6d}..{max(ys):6d}"
        )

        last_summary_time = now


# ------------------------------------------------------------
# Filtering
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
# Main serial loop
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
    print("Joystick capture mode")
    print("---------------------")
    print("Move the joystick through CENTER / LEFT / RIGHT / UP / DOWN.")
    print("The monitor will suppress the high-rate raw event stream.")
    print("Full raw data is still saved to the log file.")
    print()


while True:

    ser = None

    try:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Connecting to {args.port}...")

        ser = serial.Serial(
            args.port,
            args.baud,
            timeout=0.2,
        )

        print(
            f"[{datetime.now().strftime('%H:%M:%S')}] Connected."
        )

        while True:

            raw = ser.readline()

            if not raw:
                continue

            try:
                line = raw.decode(
                    "utf-8",
                    errors="replace"
                ).rstrip()
            except Exception:
                continue

            # ------------------------------------------------
            # Always save the complete raw stream.
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

                # Don't print normal high-rate ZMK messages.
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
            print_capture_summary()

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
