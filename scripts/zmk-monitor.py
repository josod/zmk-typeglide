#!/usr/bin/env python3
"""
Typeglide ZMK Serial Monitor

Usage:
    py zmk-monitor.py COM7

Optional:
    py zmk-monitor.py COM7 --baud 115200
    py zmk-monitor.py COM7 --retry 0.25

The COM port is explicit because Typeglide will eventually have
separate serial monitors for the left and right halves.

Features:
- Automatically reconnects when the board resets/disconnects.
- Shows every received serial line; nothing is filtered out.
- Saves the complete session to a timestamped log.
- Marks connection/disconnection events.
- Adds >>> to lines useful for analog-stick debugging.
"""

import argparse
import datetime as dt
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is not installed.")
    print()
    print("Install it with:")
    print("  py -m pip install pyserial")
    sys.exit(1)


FOCUS_TERMS = (
    "analog_stick",
    "analog stick",
    "scan coordinator",
    "auto-center",
    "raw x=",
    "raw y=",
    "input_listener",
    "input-split",
    "adc",
    "split",
    "bluetooth",
    "ble",
    "connected",
    "disconnected",
    "assert",
    "fault",
    "reset",
)

JOYSTICK_TERMS = (
    "analog_stick",
    "analog stick",
    "scan coordinator",
    "auto-center",
    "raw x=",
    "raw y=",
    "input_listener",
    "input-split",
    "adc",
)

ERROR_TERMS = (
    "<err>",
    "<wrn>",
    "error",
    "warning",
    "assert",
    "fault",
    "panic",
)


def timestamp():
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def make_log_name(port):
    safe_port = port.replace(":", "_").replace("\\", "_").replace("/", "_")
    return f"zmk-{safe_port}-{dt.datetime.now().strftime('%Y%m%d-%H%M%S')}.log"


def matches_mode(line, mode):
    if mode == "all":
        return True

    lower = line.lower()

    if mode == "focus":
        return any(term in lower for term in FOCUS_TERMS)

    if mode == "joystick":
        return any(term in lower for term in JOYSTICK_TERMS)

    if mode == "errors":
        return any(term in lower for term in ERROR_TERMS)

    return True


def write_and_print(log_file, line, highlight=False):
    record = f"[{timestamp()}] {line}"
    log_file.write(record + "\n")
    log_file.flush()

    if highlight:
        print(">>> " + record, flush=True)
    else:
        print(record, flush=True)


def main():
    parser = argparse.ArgumentParser(
        description="Auto-reconnecting Typeglide ZMK serial monitor"
    )
    parser.add_argument(
        "port",
        help="Serial COM port, for example COM7",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate (default: 115200)",
    )
    parser.add_argument(
        "--retry",
        type=float,
        default=0.25,
        help="Seconds between reconnect attempts (default: 0.25)",
    )
    parser.add_argument(
        "--focus",
        action="store_true",
        help="Show useful ZMK/split/analog-stick lines on screen.",
    )
    parser.add_argument(
        "--joystick",
        action="store_true",
        help="Show only analog-stick/ADC related lines on screen.",
    )
    parser.add_argument(
        "--errors",
        action="store_true",
        help="Show only errors, warnings and fault-related lines on screen.",
    )

    args = parser.parse_args()

    selected_modes = sum([args.focus, args.joystick, args.errors])
    if selected_modes > 1:
        parser.error("--focus, --joystick and --errors are mutually exclusive")

    if args.focus:
        display_mode = "focus"
    elif args.joystick:
        display_mode = "joystick"
    elif args.errors:
        display_mode = "errors"
    else:
        display_mode = "all"

    port = args.port.upper()
    log_name = make_log_name(port)

    print("=" * 60)
    print(" Typeglide ZMK Serial Monitor")
    print("=" * 60)
    print(f"Port:      {port}")
    print(f"Baud:      {args.baud}")
    print(f"Log file:  {log_name}")
    print()
    print(f"Display mode: {display_mode}")
    print("The log file ALWAYS contains the complete unfiltered serial output.")
    if display_mode == "all":
        print("All received serial output is displayed.")
    elif display_mode == "focus":
        print("Showing useful ZMK/joystick/split/connection lines.")
    elif display_mode == "joystick":
        print("Showing analog-stick/ADC lines only.")
    else:
        print("Showing errors/warnings/faults only.")
    print(">>> marks highlighted analog-stick lines.")
    print("The monitor automatically reconnects after reset.")
    print("Press Ctrl+C to stop.")
    print("=" * 60)
    print()

    with open(log_name, "a", encoding="utf-8", errors="replace") as log_file:
        while True:
            try:
                print(
                    f"[{timestamp()}] Waiting for {port}...",
                    flush=True,
                )

                while True:
                    try:
                        ser = serial.Serial(
                            port=port,
                            baudrate=args.baud,
                            timeout=0.10,
                            write_timeout=0.10,
                        )
                        break
                    except (serial.SerialException, OSError):
                        time.sleep(args.retry)

                print(
                    f"[{timestamp()}] CONNECTED: {port}",
                    flush=True,
                )
                write_and_print(
                    log_file,
                    f"--- CONNECTED {port} ---",
                )

                buffer = bytearray()

                try:
                    while True:
                        data = ser.read(ser.in_waiting or 1)

                        if not data:
                            continue

                        buffer.extend(data)

                        while b"\n" in buffer:
                            raw_line, _, buffer = buffer.partition(b"\n")

                            line = raw_line.rstrip(b"\r").decode(
                                "utf-8",
                                errors="replace",
                            )

                            # Always save the complete line.
                            # Only apply the display filter to the console.
                            if matches_mode(line, display_mode):
                                write_and_print(
                                    log_file,
                                    line,
                                    "analog_stick" in line.lower()
                                    or "analog stick" in line.lower()
                                    or "scan coordinator" in line.lower()
                                    or "raw x=" in line.lower()
                                    or "raw y=" in line.lower()
                                    or "auto-center" in line.lower(),
                                )
                            else:
                                record = f"[{timestamp()}] {line}"
                                log_file.write(record + "\n")
                                log_file.flush()

                except (serial.SerialException, OSError) as exc:
                    write_and_print(
                        log_file,
                        f"--- DISCONNECTED {port}: {exc} ---",
                    )

                finally:
                    try:
                        ser.close()
                    except Exception:
                        pass

                print(
                    f"[{timestamp()}] Board disappeared. "
                    f"Reconnecting...",
                    flush=True,
                )

                time.sleep(args.retry)

            except KeyboardInterrupt:
                print()
                print(f"[{timestamp()}] Stopped.")
                print(f"Log saved to: {log_name}")
                break

            except Exception as exc:
                print(
                    f"[{timestamp()}] Monitor error: {exc}",
                    flush=True,
                )
                time.sleep(args.retry)


if __name__ == "__main__":
    main()
