import re
import sys
import time
import serial
from serial.tools import list_ports

if len(sys.argv) < 2:
    print("Usage: python tg-adc.py COM4")
    sys.exit(1)

port = sys.argv[1]
baudrate = 115200
reconnect_delay = 1.0

ADC_RE = re.compile(r"ADC_RAW\s+X=(-?\d+)\s+Y=(-?\d+)")

print("=" * 60)
print("Typeglide ADC monitor")
print(f"Port: {port} @ {baudrate}")
print("Waiting for ADC_RAW...")
print("Ctrl+C to exit")
print("=" * 60)

while True:
    try:
        print(f"\nConnecting to {port}...")

        with serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=0.2,
        ) as ser:

            print("Connected.")

            while True:
                line = ser.readline().decode(
                    "utf-8", errors="replace"
                )

                match = ADC_RE.search(line)

                if match:
                    x = int(match.group(1))
                    y = int(match.group(2))

                    print(
                        f"ADC  X={x:4d}  Y={y:4d}",
                        flush=True,
                    )

    except KeyboardInterrupt:
        print("\nStopped.")
        break

    except (serial.SerialException, OSError) as e:
        print(f"Disconnected: {e}")
        print(f"Retrying in {reconnect_delay:.1f}s...")
        time.sleep(reconnect_delay)
