

import re
import sys
import time
import serial
import statistics

if len(sys.argv) < 2:
    print("Usage: python tg-raw.py COM4")
    sys.exit(1)

port = sys.argv[1]

ser = serial.Serial(
    port=port,
    baudrate=115200,
    timeout=0.05,
)

XY_RE = re.compile(r"x=(-?\d+)\s+y=(-?\d+)")

# 0.5 s settling time + 1.5 s actual measurement
SETTLE_TIME = 0.5
MEASURE_TIME = 1.5

stages = [
    ("CENTER",  "Leave joystick centered"),
    ("DOWN",    "Hold joystick DOWN"),
    ("CENTER",  "Return to center"),
    ("UP",      "Hold joystick UP"),
    ("CENTER",  "Return to center"),
    ("FORWARD", "Hold joystick FORWARD"),
    ("CENTER",  "Return to center"),
    ("BACK",    "Hold joystick BACK"),
    ("CENTER",  "Return to center"),
]

results = []


def capture_stage(name, instruction):
    input(f"{name:8s} - {instruction}. Press ENTER...")

    # Discard anything already sitting in the serial buffer.
    ser.reset_input_buffer()

    print("  Settling...", end="", flush=True)

    settle_end = time.monotonic() + SETTLE_TIME

    while time.monotonic() < settle_end:
        ser.readline()

    print(" measuring...", flush=True)

    samples = []
    measure_end = time.monotonic() + MEASURE_TIME

    while time.monotonic() < measure_end:
        line = ser.readline().decode("utf-8", errors="replace")

        match = XY_RE.search(line)

        if match:
            x = int(match.group(1))
            y = int(match.group(2))
            samples.append((x, y))

    if not samples:
        print("  WARNING: no samples captured")
        return

    xs = [x for x, y in samples]
    ys = [y for x, y in samples]

    x_median = statistics.median(xs)
    y_median = statistics.median(ys)

    results.append({
        "name": name,
        "samples": len(samples),
        "xmin": min(xs),
        "xmax": max(xs),
        "ymin": min(ys),
        "ymax": max(ys),
        "xmedian": x_median,
        "ymedian": y_median,
    })

    print(
        f"  {len(samples):4d} samples  "
        f"X={min(xs):4d}..{max(xs):4d}  "
        f"Y={min(ys):4d}..{max(ys):4d}  "
        f"median=({x_median:.1f}, {y_median:.1f})"
    )


try:
    print("=" * 64)
    print("Typeglide clean joystick capture")
    print(f"Connected: {port} @ 115200")
    print("=" * 64)
    print()
    print("Each stage:")
    print("  - press ENTER")
    print("  - wait 0.5 seconds")
    print("  - capture for 1.5 seconds")
    print()
    print("IMPORTANT: Hold the requested direction during measurement.")
    print()

    for name, instruction in stages:
        capture_stage(name, instruction)
        print()

finally:
    ser.close()


print()
print("=" * 64)
print("FINAL SUMMARY")
print("=" * 64)

print()
print(
    f"{'STAGE':10s}"
    f"{'X RANGE':18s}"
    f"{'Y RANGE':18s}"
    f"{'MEDIAN'}"
)
print("-" * 64)

for r in results:
    print(
        f"{r['name']:10s}"
        f"{r['xmin']:4d} .. {r['xmax']:<4d}      "
        f"{r['ymin']:4d} .. {r['ymax']:<4d}      "
        f"({r['xmedian']:.1f}, {r['ymedian']:.1f})"
    )

print()
print("Capture complete.")
