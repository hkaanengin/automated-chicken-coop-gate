# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Single Arduino Uno sketch (`main.ino`) controlling a solar-powered chicken coop door via a linear actuator. Currently bench-testing — Stages 1 through 5b are complete (RTC, switch, relay signal, coil power, H-bridge wiring, direction verification). The current sketch is **Stage 5c**: full control logic, run on the bench with `OPEN_TIME`/`CLOSE_TIME` set ~2 minutes apart for end-to-end verification before deployment.

`architecture.md` is the authoritative spec — Sections 5 (control logic), 8 (pin assignments), and 9 (per-stage build log) are referenced from code comments and should be kept in sync when behavior changes.

## Build and Upload

Arduino IDE (or `arduino-cli`) targeting Arduino Uno at 9600 baud serial. The only external library is **RTClib by Adafruit** (Library Manager). No test suite — verification is via Serial Monitor while observing relay LEDs / clicks / actuator motion.

**Before every upload during bench testing**, edit the four schedule constants at `main.ino:29-32` (`OPEN_HOUR`, `OPEN_MINUTE`, `CLOSE_HOUR`, `CLOSE_MINUTE`) so that the open and close events fire ~2 minutes apart from the current RTC time. The sketch announces itself on boot ("Coop Door Stage 5c: Full Integration") and prints decoded mode + door state once per loop iteration.

If the RTC has lost power (fresh coin cell, long disconnect), `setup()` reinitializes it to compile time via `rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))` — there will be a few seconds of compile-and-upload lag baked in. Re-upload immediately to minimize drift, or replace with a one-shot serial-input time-set sketch before field deployment.

## Architecture and Invariants

The control loop is straightforward (read RTC + switch → decide → drive relays), but several non-obvious invariants are load-bearing:

**Actuator direction mapping** (`architecture.md` §3): **extend = close**, **retract = open**. R1 (D7) extends, R2 (D8) retracts. Inverting this silently inverts the entire system. All pin/relay names in the code follow this convention.

**Relay module is active-LOW**: `RELAY_ON = LOW`, `RELAY_OFF = HIGH`. To avoid a boot-time glitch where pins float as INPUT and briefly trigger the relays, `setup()` uses the pattern `pinMode(INPUT) → digitalWrite(HIGH) → pinMode(OUTPUT)` *before* any other initialization (`main.ino:117-124`). Do not reorder. Stage 4 confirmed this survives with coils energized.

**Both relays ON = short circuit through the H-bridge.** `drive_open()` and `drive_close()` always de-energize the opposite relay *first* before energizing their own (`main.ino:61-69`). Preserve this ordering in any new direction-driving code.

**Power-on forced close** (`main.ino:158-160`): on every boot the physical door position is unknown, so `setup()` unconditionally runs `close_door()` (27s extend) to establish `door_state = CLOSED` as ground truth. The main loop is idempotent w.r.t. target state, so it will immediately re-open the door on the next iteration if the schedule says so. Closing-as-default is the chosen fail-safe direction (predator safety on unexpected reset).

**Open-loop end-of-stroke**: `ACTUATOR_TRAVEL_MS = 27000` (25s travel + 2s buffer). The IP800 has internal limit switches that cut motor power at end-of-stroke even while relays remain energized — the buffer is harmless. There is no position feedback; do not assume the Arduino "knows" the door reached the end.

**Switch decoding** (`main.ino:90-97`, INPUT_PULLUP, LOW = engaged):
- `(D2=HIGH, D3=HIGH)` → AUTO (center)
- `(D2=LOW,  D3=HIGH)` → MANUAL_OPEN (Position II)
- `(D2=HIGH, D3=LOW)` → MANUAL_CLOSE (Position I)
- `(D2=LOW,  D3=LOW)` → INVALID (impossible on a DPDT ON-OFF-ON; treated as fault → relays off, retry next loop)

The `I = Close, II = Open` mapping is achieved through wiring, not code (see `architecture.md` §9 → Stage 2). Don't try to "fix" the labels by inverting decode logic — the wiring matches the labels physically stickered on the switch housing.

**Schedule comparison assumption** (`is_open_hours`, `main.ino:106-111`): assumes `OPEN_TIME < CLOSE_TIME` on the same calendar day. Fine for chickens (e.g. 07:00–20:00), but a midnight-crossing schedule (e.g. open 22:00, close 06:00) would silently misbehave — guard or refactor if that requirement appears.

## Stage Roadmap (where work goes next)

Stage 5c (current): bench-verify the full sketch end-to-end. Stage 6 will introduce deep sleep (`LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF)`, watchdog timer for periodic time checks, INT0/INT1 on D2/D3 for switch wake) — see `architecture.md` §9 → Stage 6 for the planned control-flow restructure. The actuator-run path stays the same; only idle behavior changes.
