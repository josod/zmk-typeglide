
import sys
import re
import time
import serial
from serial.tools import list_ports


BAUD_RATE = 115200
RECONNECT_DELAY = 1.0

DIRECTIONS = {
    0: "NONE",
    1: "RIGHT",
    2: "LEFT",
    3: "DOWN",
    4: "UP",
}

#:
# joystick_4way_handle_event:
# 4WAY: x=56 y=126 -> direction=0
XY_DIRECTION_RE = re.compile(
    r"joystick_4way_handle_event:.*?"
    r"x=(-?\d+)\s+y=(-?\d+).*?"
    r"direction=(\d+)"
)

#:
# PRESS direction=4
PRESS_RE = re.compile(
    r"PRESS direction=(\d+)"
)

#:
# RELEASE direction=4
RELEASE_RE = re.compile(
    r"RELEASE direction=(\d+)"
)

# Zephyr timestamp:
# [00:07:14.852,447]
TIME_RE = re.compile(
    r"\[(\d+:\d+:\d+\.\d+,\d+)\]"
)


def timestamp(line):
    match = TIME_RE.search(line)

    if match:
        return match.group(1)

    return time.strftime("%H:%M:%S")


def direction_name(number):
    return DIRECTIONS.get(
        number,
        f"UNKNOWN({number})"
    )


def open_serial(port):
    while True:
        try:
            ser = serial.Serial(
                port=port,
                baudrate=BAUD_RATE,
                timeout=0.2,
            )

            print()
            print("=" * 64)
            print("Typeglide 4-way monitor")
            print(f"Connected: {port} @ {BAUD_RATE}")
            print("=" * 64)
            print()

            return ser

        except (serial.SerialException, OSError) as e:
            print(
                f"\rWaiting for {port}... "
                f"({e})",
                end="",
                flush=True,
            )

            time.sleep(RECONNECT_DELAY)


def print_state(ts, direction, x, y):
    print(
        f"[{ts}] "
        f"STATE   "
        f"{direction_name(direction):<5} "
        f"X={x:3d} "
        f"Y={y:3d}",
        flush=True,
    )


def print_event(ts, event, direction):
    print(
        f"[{ts}] "
        f"{event:<7} "
        f"{direction_name(direction)}",
        flush=True,
    )


def main():

    if len(sys.argv) != 2:
        print("Usage:")
        print()
        print("  python tg-4way.py COM4")
        print()
        sys.exit(1)

    port = sys.argv[1]

    last_direction = None

    while True:

        ser = open_serial(port)

        try:

            while True:

                raw = ser.readline()

                if not raw:
                    continue

                line = raw.decode(
                    "utf-8",
                    errors="replace",
                )

                ts = timestamp(line)

                # -------------------------------------------------
                # Explicit PRESS
                # -------------------------------------------------

                match = PRESS_RE.search(line)

                if match:

                    direction = int(
                        match.group(1)
                    )

                    print_event(
                        ts,
                        "PRESS",
                        direction,
                    )

                    continue

                # -------------------------------------------------
                # Explicit RELEASE
                # -------------------------------------------------

                match = RELEASE_RE.search(line)

                if match:

                    direction = int(
                        match.group(1)
                    )

                    print_event(
                        ts,
                        "RELEASE",
                        direction,
                    )

                    continue

                # -------------------------------------------------
                # Direction + X/Y
                # -------------------------------------------------

                match = XY_DIRECTION_RE.search(line)

                if match:

                    x = int(
                        match.group(1)
                    )

                    y = int(
                        match.group(2)
                    )

                    direction = int(
                        match.group(3)
                    )

                    # Only print when direction changes.
                    if direction != last_direction:

                        print_state(
                            ts,
                            direction,
                            x,
                            y,
                        )

                        last_direction = direction

        except KeyboardInterrupt:

            print()
            print("Stopped.")

            try:
                ser.close()
            except Exception:
                pass

            return

        except (
            serial.SerialException,
            OSError,
        ) as e:

            print()
            print(
                f"Serial connection lost: {e}"
            )
            print("Reconnecting...")

            try:
                ser.close()
            except Exception:
                pass

            time.sleep(RECONNECT_DELAY)


if __name__ == "__main__":
    main()
