import argparse
import datetime as dt
import os
import sys
import time

import serial
from serial.tools import list_ports


def timestamp():
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def open_serial(port, baud):
    while True:
        try:
            print(f"Connecting to {port}...")
            ser = serial.Serial(
                port=port,
                baudrate=baud,
                timeout=1,
            )
            print(f"Connected to {port}")
            return ser
        except serial.SerialException as e:
            print(f"Waiting for {port}: {e}")
            time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description="Typeglide raw serial logger")
    parser.add_argument("port", help="Serial port, e.g. COM7")
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-o", "--output",
        default=None,
        help="Output log file",
    )
    args = parser.parse_args()

    if args.output is None:
        filename = f"tg-raw-{dt.datetime.now():%Y%m%d-%H%M%S}.log"
    else:
        filename = args.output

    print(f"Logging to: {os.path.abspath(filename)}")
    print("Press Ctrl+C to stop.\n")

    with open(filename, "a", encoding="utf-8", buffering=1) as log:
        while True:
            ser = open_serial(args.port, args.baud)

            try:
                while True:
                    raw = ser.readline()

                    if not raw:
                        continue

                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                    if not line:
                        continue

                    line = f"[{timestamp()}] {line}"

                    # Everything goes to the file.
                    log.write(line + "\n")

                    # Only useful diagnostic lines to console.
                    interesting = (
                        "input",
                        "Input",
                        "INPUT",
                        "CPF",
                        "CCC",
                        "subscribe",
                        "Subscribe",
                        "reg",
                        "peripheral",
                    )

                    if any(x in line for x in interesting):
                        print(line, flush=True)

            except serial.SerialException as e:
                print(f"\nSerial connection lost: {e}")
                try:
                    ser.close()
                except Exception:
                    pass

                print("Reconnecting...\n")
                time.sleep(1)

            except KeyboardInterrupt:
                print("\nStopped.")
                try:
                    ser.close()
                except Exception:
                    pass
                return


if __name__ == "__main__":
    main()
