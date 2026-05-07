# Wiring Diagram

Block-level wiring for the automated chicken coop door. All connections in
this diagram are as-built and verified through Stages 1–5c.

## Power and Signal Overview

```
                          ┌──────────────────┐
   ┌─── PV+ ──────────────┤                  │
   │    PV− ──────────────┤   P1006 Solar    │
   │    (PV optional      │   Charge         │
   │     for bench)       │   Controller     │
   │                      │                  │
   │    BAT+ ─────────────┤  12V/24V auto    │
   │    BAT− ─────────────┤                  │
   │       │              │   LOAD+ ─── (A) ─┼─── 12V+ JUNCTION
   │       │              │   LOAD− ─── (B) ─┼─── GND JUNCTION
   │       │              └──────────────────┘
   │       │
   │       │  ┌────[ 7.5A FUSE ]────┐
   │       └──┤                     ├── BAT+
   │          └─────────────────────┘
   │
   │       ┌─────────────┐
   └───────┤ 12V Battery │
           │   (Orion    │── BAT−
           │   lead-acid)│
           └─────────────┘


    12V+ JUNCTION (5 wires)              GND JUNCTION (6 wires)
    ─────────────────────────            ─────────────────────────
    • P1006 LOAD+                        • P1006 LOAD−
    • Relay JD-VCC (3-pin hdr, left)     • Relay GND (3-pin hdr, right)
    • Relay 1 NO                         • Relay 1 NC
    • Relay 2 NO                         • Relay 2 NC
    • Arduino VIN  ◄── added in 5d       • Arduino GND  (already tied via
                                            Stage 4 path; see note below)
```

> **Ground note:** Arduino's logic GND is connected to the GND junction
> through the relay module's 4-pin header GND wire (Stage 3) and a
> dedicated Arduino GND → GND-junction wire (Stage 4). Both Arduino
> GND pins are internally tied, so this is electrically a single
> common ground.

## Arduino Signal Connections

```
                ┌──────────────────────────┐
                │      Arduino Uno         │
                │                          │
   12V+ ────────┤ VIN                      │
   GND  ────────┤ GND  (× 2: RTC/switch    │
                │        and junction)     │
                │                          │
                │ 5V  ─── VCC ──► DS3231 RTC + Relay 4-pin VCC
                │                          │
                │ A4  ─── SDA ──► DS3231 SDA
                │ A5  ─── SCL ──► DS3231 SCL
                │                          │
                │ D2  ─── (INPUT_PULLUP) ─◄── Switch pin 1a
                │ D3  ─── (INPUT_PULLUP) ─◄── Switch pin 2b
                │                          │
                │ D7  ─── (active LOW) ──► Relay IN1  (R1 = close)
                │ D8  ─── (active LOW) ──► Relay IN2  (R2 = open)
                │                          │
                └──────────────────────────┘
```

## Relay Module — H-Bridge to Actuator

```
   12V+ JUNCTION ──────┬───────────────┐
                       │               │
                  ┌────┴────┐     ┌────┴────┐
                  │ R1 NO   │     │ R2 NO   │
                  │         │     │         │
       ┌──────────┤ R1 COM  │     │ R2 COM  ├──────────┐
       │          │         │     │         │          │
       │          │ R1 NC   │     │ R2 NC   │          │
       │          └────┬────┘     └────┬────┘          │
       │               │               │               │
       │   GND JUNCTION ──┬────────────┘               │
       │                  │                            │
       │                  │                            │
       │            (both NCs)                         │
       │                                               │
       ▼                                               ▼
   Actuator                                       Actuator
   Lead A                                         Lead B
       │                                               │
       └─────────────► IP800 Linear Actuator ◄─────────┘
                       (12V, 800N, 250mm,
                        internal limit switches)
```

**Relay states:**

| D7 (R1) | D8 (R2) | Lead A | Lead B | Result                   |
|---------|---------|--------|--------|--------------------------|
| HIGH    | HIGH    | GND    | GND    | Hold (no current)        |
| LOW     | HIGH    | +12V   | GND    | Extend → close door      |
| HIGH    | LOW     | GND    | +12V   | Retract → open door      |
| LOW     | LOW     | +12V   | +12V   | **SHORT — never allow**  |

## Switch — DPDT MR-108

```
   ┌─────────────────────────────┐
   │  MR-108 (DPDT, ON-OFF-ON)   │
   │                             │
   │   1a  ───────────────► D2   │  (Position II → Manual Open)
   │   1   ───────────────► GND  │  (Pole 1 COM)
   │   1b  ───── (unused)        │
   │                             │
   │   2a  ───── (unused)        │
   │   2   ───────────────► GND  │  (Pole 2 COM)
   │   2b  ───────────────► D3   │  (Position I  → Manual Close)
   │                             │
   └─────────────────────────────┘

   Lever position decoding (with INPUT_PULLUP):
     Position I    → D2 HIGH, D3 LOW   → Manual Close
     Center (off)  → D2 HIGH, D3 HIGH  → Auto
     Position II   → D2 LOW,  D3 HIGH  → Manual Open
```

## DS3231 RTC

```
   ┌──────────────────┐
   │  DS3231 module   │
   │                  │
   │  VCC ────► Arduino 5V
   │  GND ────► Arduino GND
   │  SDA ────► Arduino A4
   │  SCL ────► Arduino A5
   │  SQW ──── (unused)
   │  32K ──── (unused)
   │                  │
   │  [CR2032 backup] │
   └──────────────────┘
```