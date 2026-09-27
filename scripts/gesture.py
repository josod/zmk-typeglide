import serial
import sys
from datetime import datetime

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
LOGFILE = sys.argv[2] if len(sys.argv) > 2 else "zmk_serial.log"
BAUD = 115200

KEYWORDS = (
    "tg_gesture",
    "keycode",
    "caps",
    "shift",
    "input",
    "split",
)
DISPLAY_KEYWORDS = (
    "tg_gesture:",
)
print(f"Listening on {PORT}")
print(f"Logging everything to {LOGFILE}")
print(f"Showing: {', '.join(KEYWORDS)}")
print("Press Ctrl-C to stop.\n")

with serial.Serial(PORT, BAUD, timeout=1) as ser, \
     open(LOGFILE, "a", encoding="utf-8", buffering=1) as log:

    log.write(
        f"\n\n===== SESSION {datetime.now().isoformat()} =====\n"
    )

    while True:
        try:
            raw = ser.readline()

            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").rstrip()

            # Always save the complete raw log.
            log.write(line + "\n")

            # Only display interesting lines.
            lower = line.lower()

            if any(k in lower for k in DISPLAY_KEYWORDS):
                print(">>> " + line, flush=True)


        except KeyboardInterrupt:
            print(f"\nStopped. Complete log saved to {LOGFILE}")
            break

        except serial.SerialException as e:
            print(f"\nSerial error: {e}")
            break
