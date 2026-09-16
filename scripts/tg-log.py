#!/usr/bin/env python3
"""
Typeglide serial diagnostic logger.

Examples:
    python tg-log.py COM4
    python tg-log.py COM4 --filter mouse
    python tg-log.py COM4 --filter joystick
    python tg-log.py COM4 --filter split
    python tg-log.py COM4 --filter errors
    python tg-log.py COM4 --filter all -o typeglide.log

Requires:
    pip install pyserial
"""

import argparse
import re
import sys
import time
from datetime import datetime

try:
    import serial
    from serial import SerialException
except ImportError:
    print("ERROR: pyserial is not installed.")
    print("Install with: python -m pip install pyserial")
    sys.exit(1)


# Lines we consider interesting for each diagnostic view.
MOUSE_PATTERNS = [
    r"pmw3610",
    r"SPLIT TX reg=0",
    r"BLE INPUT TX reg=0",
    r"BLE INPUT notify.*reg=0",
    r"INPUT RX reg=0",
    r"Got peripheral event for 0",
    r"mouse",
    r"pointing",
]

JOYSTICK_PATTERNS = [
    r"analog_axis",
    r"ADC",
    r"SPLIT TX reg=1",
    r"BLE INPUT TX reg=1",
    r"BLE INPUT notify.*reg=1",
    r"INPUT RX reg=1",
    r"Got peripheral event for 1",
    r"joystick",
    r"4WAY",
    r"direction=",
]

SPLIT_PATTERNS = [
    r"SPLIT TX",
    r"BLE INPUT TX",
    r"BLE INPUT notify",
    r"INPUT RX",
    r"Got peripheral event",
    r"peripheral_input_event_notify_cb",
]

ERROR_PATTERNS = [
    r"\berr\b",
    r"\berror\b",
    r"fail",
    r"failed",
    r"-1\b",
    r"-2\b",
    r"-5\b",
    r"-11\b",
    r"-16\b",
    r"-19\b",
    r"-22\b",
    r"-128\b",
    r"assert",
    r"fault",
]


def matches(line, patterns):
    text = line.lower()
    return any(re.search(p, text, re.IGNORECASE) for p in patterns)


def classify(line):
    if matches(line, ERROR_PATTERNS):
        return "ERR "
    if matches(line, MOUSE_PATTERNS):
        return "MOUSE"
    if matches(line, JOYSTICK_PATTERNS):
        return "JOY "
    if matches(line, SPLIT_PATTERNS):
        return "SPLIT"
    return "    "


def selected(line, filt):
    if filt == "all":
        return True
    if filt == "mouse":
        return matches(line, MOUSE_PATTERNS)
    if filt == "joystick":
        return matches(line, JOYSTICK_PATTERNS)
    if filt == "split":
        return matches(line, SPLIT_PATTERNS)
    if filt == "errors":
        return matches(line, ERROR_PATTERNS)
    return True


def add_host_timestamp(line):
    now = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    return f"{now} | {line.rstrip()}"


def main():
    parser = argparse.ArgumentParser(
        description="Typeglide serial diagnostic logger"
    )
    parser.add_argument("port", help="Serial port, e.g. COM4")
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=9600,
        help="Baud rate (default: 9600)",
    )
    parser.add_argument(
        "-f", "--filter",
        choices=["all", "mouse", "joystick", "split", "errors"],
        default="all",
        help="What to display (default: all)",
    )
    parser.add_argument(
        "-o", "--output",
        help="Also write the displayed output to this file",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Do not add host timestamps/classification",
    )
    parser.add_argument(
        "--no-reconnect",
        action="store_true",
        help="Exit instead of reconnecting if the serial device disappears",
    )

    args = parser.parse_args()

    logfile = None
    if args.output:
        logfile = open(args.output, "a", encoding="utf-8", buffering=1)

    print(f"Typeglide logger")
    print(f"  Port:   {args.port}")
    print(f"  Baud:   {args.baud}")
    print(f"  Filter: {args.filter}")
    if args.output:
        print(f"  Log:    {args.output}")
    print("  Press Ctrl+C to stop.")
    print()

    while True:
        try:
            with serial.Serial(
                args.port,
                args.baud,
                timeout=1,
            ) as ser:
                print(f"[connected {args.port}]")
                if logfile:
                    logfile.write(
                        f"\n--- connected {args.port} "
                        f"{datetime.now().isoformat(timespec='seconds')} ---\n"
                    )

                while True:
                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")

                    if not selected(line, args.filter):
                        continue

                    if args.raw:
                        output = line
                    else:
                        output = f"{classify(line)} | {add_host_timestamp(line)}"

                    print(output, flush=True)

                    if logfile:
                        logfile.write(output + "\n")

        except KeyboardInterrupt:
            print("\nStopped.")
            break

        except (SerialException, OSError) as exc:
            print(f"[serial disconnected: {exc}]")

            if args.no_reconnect:
                break

            print("[retrying in 1 second...]")
            time.sleep(1)

    if logfile:
        logfile.close()


if __name__ == "__main__":
    main()
