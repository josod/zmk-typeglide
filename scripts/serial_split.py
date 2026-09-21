import serial
import sys
from datetime import datetime

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
LOGFILE = sys.argv[2] if len(sys.argv) > 2 else "zmk_serial.log"
BAUD = 115200

KEYWORDS = (
    "split",
    "input",
    "cpf",
    "register",
    "subscription",
    "connected",
)

print(f"Listening on {PORT}")
print(f"Logging everything to {LOGFILE}")
print("Press Ctrl-C to stop.\n")

with serial.Serial(PORT, BAUD, timeout=1) as ser, \
     open(LOGFILE, "a", encoding="utf-8", buffering=1) as log:

    log.write(f"\n\n===== SESSION {datetime.now().isoformat()} =====\n")

    while True:
        try:
            raw = ser.readline()

            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").rstrip()

            # Save EVERYTHING
            log.write(line + "\n")

            # Display interesting lines
            if any(k in line.lower() for k in KEYWORDS):
                print(">>> " + line, flush=True)

        except KeyboardInterrupt:
            print(f"\nStopped. Complete log saved to {LOGFILE}")
            break
