# TypeGlide Handwire Guide

## Physical layout (per half — right is mirror of left)

```
outer  mid   inner
outer  mid   inner
outer  mid   inner  pinky   ← pinky flush with bottom finger row
thumb                       ← inner side, further away, long wire
```

Both halves are wired identically. When placed on the keyboard the right half
is flipped, so outer/inner swap sides — that's the only difference.

---

## Matrix wiring (identical for both halves)

```
         COL0/D8      COL1/D9      COL2/D10
            │            │            │
           ┌┴┐          ┌┴┐          ┌┴┐
ROW0/D4 ───┤ ├─►|───────┤ ├─►|───────┤ ├─►|───
           └┬┘  col0-0 └┬┘  col1-0 └┬┘  col2-0
           ┌┴┐          ┌┴┐          ┌┴┐
ROW1/D5 ───┤ ├─►|───────┤ ├─►|───────┤ ├─►|───
           └┬┘  col0-1 └┬┘  col1-1 └┬┘  col2-1
           ┌┴┐          ┌┴┐          ┌┴┐
ROW2/D6 ───┤ ├─►|───────┤ ├─►|───────┤ ├─►|───
           └┬┘  col0-2 └┬┘  col1-2 └┬┘  col2-2
           ┌┴┐                      ┌┴┐
ROW3/D7 ───┤ ├─►|──── (spare) ──────┤ ├─►|───
           └┬┘                      └┬┘
         thumb/pinky              pinky/thumb
         (see table below)
```

| ROW3, COL0               | ROW3, COL2   |
| ------------------------ | ------------ |
| Left: pinky              | Left: thumb  |
| Right: thumb (long wire) | Right: pinky |

---

## Per-key wiring

```
COL ──── switch ──── diode ►| ──── ROW
                    anode  cathode
                           (stripe end faces ROW)
```

- ROW wire connects to one switch terminal
- Other switch terminal connects to diode anode
- Diode cathode (stripe) connects to COL wire
- Diode direction matches firmware setting: `row2col`

### Thumb (long wire)

Mount the diode close to the MCU or keywell end of the wire, not at the switch.
This keeps the unprotected wire segment short and reduces noise risk.

---

## Pin assignments (same GPIO pins on both halves)

| Signal | nice!nano pin |
| ------ | ------------- |
| ROW0   | D4            |
| ROW1   | D5            |
| ROW2   | D6            |
| ROW3   | D7            |
| COL0   | D8            |
| COL1   | D9            |
| COL2   | D10           |

---

## Key positions

| Matrix pos | Left half | Right half |
| ---------- | --------- | ---------- |
| ROW0, COL0 | ring top  | idx top    |
| ROW0, COL1 | mid top   | mid top    |
| ROW0, COL2 | idx top   | ring top   |
| ROW1, COL0 | ring mid  | idx mid    |
| ROW1, COL1 | mid mid   | mid mid    |
| ROW1, COL2 | idx mid   | ring mid   |
| ROW2, COL0 | ring bot  | idx bot    |
| ROW2, COL1 | mid bot   | mid bot    |
| ROW2, COL2 | idx bot   | ring bot   |
| ROW3, COL0 | pinky     | thumb      |
| ROW3, COL1 | (spare)   | (spare)    |
| ROW3, COL2 | thumb     | pinky      |

---

## Notes

- 11 diodes per half, 22 total
- Row wires run **horizontally**
- Column wires run **vertically**
- Spare position (ROW3/COL1) — leave unwired
- Both halves use identical GPIO pin numbers but mirrored column meaning
- Right half: pinky sits flush with the bottom finger row physically,
  but is wired to ROW3/COL2 (bottom of ring column)
