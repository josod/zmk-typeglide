# TypeGlide Handwire Guide

## Matrix (per half, identical for left and right)

```
         COL0/D8     COL1/D9     COL2/D10
            │           │           │
           ┌┴┐         ┌┴┐         ┌┴┐
ROW0/D4 ───┤ ├─►|──────┤ ├─►|──────┤ ├─►|───
     ring  └┬┘ ring   └┬┘ mid    └┬┘ idx
     top    │   top     │  top     │  top
           ┌┴┐         ┌┴┐         ┌┴┐
ROW1/D5 ───┤ ├─►|──────┤ ├─►|──────┤ ├─►|───
           └┬┘         └┬┘         └┬┘
           ┌┴┐         ┌┴┐         ┌┴┐
ROW2/D6 ───┤ ├─►|──────┤ ├─►|──────┤ ├─►|───
           └┬┘         └┬┘         └┬┘
           ┌┴┐                     ┌┴┐
ROW3/D7 ───┤ ├─►|─── (spare) ──────┤ ├─►|───
           └┬┘                     └┬┘
           pinky                  thumb
```

## Per-key wiring

```
COL ──── switch ──── diode ►| ──── ROW
                    anode  cathode
                           (stripe end faces ROW)
```

- Diode anode connects to the switch
- Diode cathode (stripe) connects to the row wire
- Diode direction matches firmware setting: `col2row`

## Pin assignments (both halves identical)

| Signal | nice!nano pin |
|--------|---------------|
| ROW0   | D4            |
| ROW1   | D5            |
| ROW2   | D6            |
| ROW3   | D7            |
| COL0 — ring   | D8   |
| COL1 — middle | D9   |
| COL2 — index  | D10  |

## Key positions

| Position     | Left half | Right half |
|--------------|-----------|------------|
| ROW0, COL0   | ring top  | ring top   |
| ROW0, COL1   | mid top   | mid top    |
| ROW0, COL2   | idx top   | idx top    |
| ROW1, COL0   | ring mid  | ring mid   |
| ROW1, COL1   | mid mid   | mid mid    |
| ROW1, COL2   | idx mid   | idx mid    |
| ROW2, COL0   | ring bot  | ring bot   |
| ROW2, COL1   | mid bot   | mid bot    |
| ROW2, COL2   | idx bot   | idx bot    |
| ROW3, COL0   | pinky     | thumb      |
| ROW3, COL1   | (spare)   | (spare)    |
| ROW3, COL2   | thumb     | pinky      |

## Notes

- 11 diodes per half, 22 total
- Row wires run **horizontally**
- Column wires run **vertically**
- Spare position (ROW3/COL1) — leave unwired
- Right half is a physical mirror of the left but uses the same GPIO pins
