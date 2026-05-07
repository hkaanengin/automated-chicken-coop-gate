# Automated Chicken Coop Door — System Architecture

---

## Project Status

**Current phase:** Bench-testing, direct point-to-point wiring (no breadboard).

**Completed stages:**
- ✅ **Stage 1:** Arduino + DS3231 RTC — time-keeping verified, coin-cell backup confirmed working across power cycles. See Section 9 → Stage 1 for details.
- ✅ **Stage 2:** Arduino + manual switch — all three lever positions decode correctly. Canonical mapping for this build is **Position I = Manual Close, Position II = Manual Open** (chosen to match as-built wiring; see Section 9 → Stage 2 for rationale).
- ✅ **Stage 3:** Arduino + relay module, signal-side only — both channel LEDs tracked the boot-time exercise sketch correctly (R1/R2 individually, then alternating pattern), no boot-time glitches, JD-VCC jumper removed. Post-exercise Stage 2 behavior (RTC tick, switch decode) unaffected. See Section 9 → Stage 3 for details.
- ✅ **Stage 4:** Battery + P1006 charge controller online, relay coils powered via JD-VCC. Audible clicks on every relay transition during the boot-time exercise, no boot glitches, LOAD output worked without PV connected. See Section 9 → Stage 4 for details.
- ✅ **Stage 5a:** Inline 7.5A blade fuse installed on battery + lead, actuator wired through relays in H-bridge configuration (R1/R2 NO → 12V+ junction, R1/R2 NC → GND junction, actuator leads → R1/R2 COM). See Section 9 → Stage 5a for details.
- ✅ **Stage 5b:** Direction verification with 3-second bursts — R1 extended (close direction), R2 retracted (open direction). Wiring is correct, no lead swap needed. See Section 9 → Stage 5b for details.
- ✅ **Stage 5c:** Full integration test passed — power-on forced-close (27 s extend), scheduled open at `OPEN_TIME` (27 s retract) and scheduled close at `CLOSE_TIME` (27 s extend) both fired on time, all three switch positions (Manual Open, Manual Close, Center) produced correct actuator behavior, and switch-to-center correctly resynced door state to the current schedule. See Section 9 → Stage 5c for details.

**Next up:**
- **Stage 5d** — transition Arduino off USB and onto VIN-from-12V-bus power, verify the system still works end-to-end. Small but necessary bench step before physical install (every stage so far has run on USB).
- **Stage 6** — add deep sleep (PWR_DOWN + watchdog timer + switch interrupts) to extend battery life. Planned for a later session. See Section 9 → Stage 6 for the design.
---

## 1. Project Overview

An automated chicken coop door that opens and closes on a daily schedule, powered by a solar + battery system, with manual override capability. The system operates autonomously based on time of day, but allows manual control when needed.

### Goals
- Open the coop door automatically at a scheduled morning time
- Close the coop door automatically at a scheduled evening time
- Allow manual override (open / pause / close) at any time via a physical switch
- Run off-grid via solar power with battery backup for reliable 24/7 operation
- Resume automated behavior seamlessly after manual intervention

---

## 2. Hardware Components

| # | Component | Model / Spec | Role |
|---|-----------|--------------|------|
| 1 | Microcontroller | Arduino Uno | Main brain — runs control logic |
| 2 | Real-Time Clock | DS3231 (temperature-compensated, battery-backed) | Keeps accurate time |
| 3 | Linear Actuator | IP800, 12V, 800N, 10 mm/s, 250 mm stroke | Opens and closes the door (has built-in limit switches — confirmed) |
| 4 | Relay Module | 2-channel, 12V (Songle SRD-12VDC-SL-C relays, optoisolated, JD-VCC separable) | Reverses polarity to the actuator (H-bridge config) |
| 5 | Solar Panel | Small panel (wattage TBD) | Primary power source, charges battery |
| 6 | Solar Charge Controller | P1006, 12V/24V, 20A, 50V max PV | Manages charging and regulated 12V output |
| 7 | Battery | 12V Orion lead-acid | Secondary power source, runs system at night / cloudy days |
| 8 | Manual Switch | Maroces MR-108, DPDT ON-OFF-ON, 6-pin | Manual override for open / pause / close |
| 9 | (Optional) Step-down converter | Not needed — Arduino Uno's VIN handles 12V input | — |

---

## 3. System Architecture

### Power Flow
```
Solar Panel ──┐
              ├──► Solar Charge Controller ──► 12V Bus ──┬──► Arduino Uno (via VIN pin, regulated internally to 5V)
Battery ──────┘                                          ├──► Relay Module (12V supply)
                                                         └──► Linear Actuator (via relays)
```

### Control Flow
```
RTC Module ──────────┐
                     │
Manual Switch ───────┼──► Arduino Uno ──► Relay Module ──► Linear Actuator
                     │                         │
                     └─────────────────────────┘
                     (reads inputs, decides action)
```

### Actuator Direction Convention

**Important mechanical mapping for this build:**

| Actuator Movement | Door State |
|-------------------|------------|
| **Extend** (rod pushes out) | **Closes** the door |
| **Retract** (rod pulls in) | **Opens** the door |

All relay polarity references in this document and in the eventual Arduino code follow this convention. Getting this mapping wrong would invert the entire system behavior, so it's defined once here and referenced everywhere else.

---

## 4. Operating Modes

The manual switch has three positions that determine system mode:

### Auto Mode (Switch: Center / OFF)
- Default mode — system runs on schedule
- Arduino checks RTC time continuously
- At scheduled **open time** → retracts actuator (opens door)
- At scheduled **close time** → extends actuator (closes door)
- No manual intervention required

### Manual Open (Switch: Position II)
- Arduino immediately opens the door if not already open (retracts actuator)
- Door remains open regardless of scheduled close time
- Automation is overridden as long as switch stays in this position

### Manual Close (Switch: Position I)
- Arduino immediately closes the door if not already closed (extends actuator)
- Door remains closed regardless of scheduled open time
- Automation is overridden as long as switch stays in this position

### Returning to Auto Mode
- When switch is returned to center position, Arduino evaluates current time against schedule
- If current time is within "open hours" → ensures door is open
- If current time is within "closed hours" → ensures door is closed
- This way, the door is always in the correct state for the time of day

---

## 5. Control Logic (Pseudocode)

### Power-On Initialization

On every boot (including after battery disconnect, brownout, or reset), the Arduino cannot know the physical position of the door. To establish a known state, the system runs a **forced close** routine at startup. This drives the actuator to its extended end-of-stroke, where the built-in limit switch cuts motor power. After this routine, `door_state` is guaranteed to equal `CLOSED`.

Closing-as-default is the safer failure mode — if the Arduino resets at an unexpected time, the system converges to "door closed" rather than flinging the door open to predators. The main loop will then open the door on its next iteration if the current time is within open hours.

```
setup():
    initialize pins (relays OFF by default)
    delay(2000 ms)                    # let power rails stabilize
    initialize RTC (DS3231)
    initialize switch input pins

    # Power-on state establishment — force door to known CLOSED position
    set relays to EXTEND polarity     # extend = close door
    wait ACTUATOR_TRAVEL_TIME (27 s)  # IP800 internal limit switch cuts motor at end
    set relays OFF
    door_state = CLOSED

    # Hand off to main loop — it will open the door on the next iteration
    # if current time is within open hours and switch is in Auto mode
```

### Main Loop

```
LOOP:
    current_time = read RTC
    switch_state = read switch position

    IF switch_state == UP (Manual Open):
        IF door_state != OPEN:
            open_door()
    
    ELSE IF switch_state == DOWN (Manual Close):
        IF door_state != CLOSED:
            close_door()
    
    ELSE (switch_state == CENTER, Auto Mode):
        IF current_time >= OPEN_TIME AND current_time < CLOSE_TIME:
            IF door_state != OPEN:
                open_door()
        ELSE:
            IF door_state != CLOSED:
                close_door()

    delay(LOOP_DELAY)
```

### Door Movement Functions

```
open_door():
    set relay to RETRACT polarity     # retract = open door
    wait ACTUATOR_TRAVEL_TIME (IP800 limit switch cuts motor at end)
    set relays to OFF
    door_state = OPEN

close_door():
    set relay to EXTEND polarity      # extend = close door
    wait ACTUATOR_TRAVEL_TIME (IP800 limit switch cuts motor at end)
    set relays to OFF
    door_state = CLOSED
```

---

## 6. Key Design Decisions

### Power Architecture
- **Single 12V bus** from the solar charge controller powers everything
- Arduino Uno receives 12V on its VIN pin and regulates internally to 5V — no separate buck converter needed
- Battery provides backup when solar is insufficient (nights, cloudy days)
- Charge controller manages battery charging and prevents overcharge/deep discharge

### Polarity Reversal (H-Bridge with 2 Relays)
- Relay 1 ON + Relay 2 OFF → current flows one direction → actuator **extends → door closes**
- Relay 1 OFF + Relay 2 ON → current flows opposite direction → actuator **retracts → door opens**
- Both relays OFF → no current → actuator holds position
- **Both relays ON must be avoided** (would cause short circuit) — handled in code

### End-of-Stroke Handling
- **Confirmed:** The IP800 actuator has built-in limit switches. When the stroke reaches either end, the internal switch opens and cuts motor power, even while the actuator remains energized from the relays.
- Arduino holds the relay closed for a fixed duration of ~27 seconds (2-second buffer over the 25-second theoretical travel time). The internal limit switches protect the motor from damage if the timer slightly overshoots.
- This is an open-loop timing approach — the Arduino does not sense actual end-of-stroke. It trusts that 27 seconds is enough time for the actuator to complete its travel and for the internal switch to cut the motor.

### Manual Switch Integration
- Switch wires to 2 Arduino digital input pins using `INPUT_PULLUP` (internal pull-ups, no external resistors needed)
- D2 LOW + D3 HIGH = Position II (Manual Open)
- D2 HIGH + D3 LOW = Position I (Manual Close)
- D2 HIGH + D3 HIGH = Center (Auto Mode)
- D2 LOW + D3 LOW = Invalid (should never occur with a DPDT ON-OFF-ON; treated as a fault)
- Only 4 of the 6 DPDT pins are used (both COMs to GND, `1a` → D2, `2b` → D3). The other 2 terminals (`1b`, `2a`) remain unconnected as spare capacity
- This mapping is as-built; see Section 9 → Stage 2 for how it was determined without a multimeter

### Power-On State Recovery
- On boot, door physical position is unknown → a forced-close routine in `setup()` establishes `door_state = CLOSED` as ground truth
- Chosen direction (close) is the safer default for unexpected resets
- Main loop is idempotent with respect to target state, so init only needs to establish *a* known state — the loop will transition to the *correct* state on its first iteration

---

## 7. Configuration Parameters

These values will be defined as constants in the Arduino code:

| Parameter | Value | Notes |
|-----------|-------|-------|
| `OPEN_TIME` | TBD (e.g., 07:00) | Scheduled door open time |
| `CLOSE_TIME` | TBD (e.g., 20:00) | Scheduled door close time |
| `ACTUATOR_TRAVEL_TIME` | 27 seconds | 25 s theoretical travel + 2 s buffer; IP800 limit switch protects motor past 25 s |
| `POWER_ON_SETTLE_DELAY` | 2000 ms | Delay at boot before first actuator movement to let power rails stabilize |
| `LOOP_DELAY` | ~500 ms | Main loop polling interval |
| `SWITCH_DEBOUNCE_TIME` | ~50 ms | Debounce for switch reads |

---

## 8. Pin Assignments

Arduino Uno pin map for all signal connections:

| Signal | Arduino Pin | Direction | Notes |
|--------|-------------|-----------|-------|
| RTC SDA | A4 | I2C | Fixed on Uno — do not reassign |
| RTC SCL | A5 | I2C | Fixed on Uno — do not reassign |
| Relay 1 IN (extend / close door) | D7 | OUTPUT | Active-LOW: digitalWrite(D7, LOW) energizes relay |
| Relay 2 IN (retract / open door) | D8 | OUTPUT | Active-LOW: digitalWrite(D8, LOW) energizes relay |
| Switch input A (Manual Open) | D2 | INPUT_PULLUP | LOW when switch in UP position, HIGH otherwise |
| Switch input B (Manual Close) | D3 | INPUT_PULLUP | LOW when switch in DOWN position, HIGH otherwise |
| Power (VIN) | VIN | Power | 12V from charge controller output |
| Ground | GND | Power | Common ground — tied to 12V system GND |

### Switch Position Decoding

Using `INPUT_PULLUP` means each pin reads HIGH by default and LOW when the switch connects it to ground.

The mapping below reflects the as-built wiring of this unit (see Stage 2 in Section 9 for how this was determined). D2 and D3 readings correspond to lever positions as follows:

| Switch Position | D2 Reading | D3 Reading | Mode |
|-----------------|------------|------------|------|
| Position II | LOW | HIGH | Manual Open |
| CENTER (OFF) | HIGH | HIGH | Auto Mode |
| Position I | HIGH | LOW | Manual Close |
| (Both LOW) | LOW | LOW | Invalid — should never occur with DPDT ON-OFF-ON |

The lever markings (I, II) are the physical labels on the MR-108. "Position I = Manual Close" and "Position II = Manual Open" is the canonical mapping for this build and will be stickered onto the switch housing when mounted on the coop.

### Relay Active-Low Clarification

The Songle-based relay module is active-LOW — the relay energizes when the input pin is pulled LOW, and de-energizes when HIGH. This inverts the intuition slightly:

- `digitalWrite(RELAY_1, LOW)` → Relay 1 **ON** (contacts closed)
- `digitalWrite(RELAY_1, HIGH)` → Relay 1 **OFF** (contacts open, default state)

On Arduino boot, all digital pins default to INPUT (high-impedance), which can cause the relays to briefly trigger during startup. To prevent this, set pins HIGH *before* setting them as OUTPUT in `setup()`:

```
pinMode(RELAY_1, INPUT);         // stays high-impedance
digitalWrite(RELAY_1, HIGH);     // pre-set HIGH (relay OFF)
pinMode(RELAY_1, OUTPUT);        // now drive HIGH actively
```

---

## 9. Wiring Plan

**Wiring method:** The user chose point-to-point wiring using female Dupont jumpers directly to module headers, rather than a breadboard. Junction points (especially the common GND) are made by twisted-splice + electrical tape, or WAGO lever-nuts when available. This is a pragmatic choice for a one-off build where the final system will be permanently wired anyway — a breadboard would add an intermediate assembly step without providing much debugging value.

**Stage restructuring:** The original plan broke post-switch work into Stages 3 through 7. During Stage 3 planning, the user consolidated these into 3 stages that each introduce exactly one new subsystem, in the order: signal → coil power → load + charge controller. The original sub-topics (H-bridge wiring, common ground verification, full integration test, solar integration) are all still covered — they've been regrouped under the three new stage headings below.

### Stage 1: Arduino + RTC (DS3231) ✅ COMPLETE

**Goal:** Verify Arduino powers up, RTC communicates, and time can be set and read back accurately.

**Connections:**
| From (DS3231) | To (Arduino) | 
|---------------|--------------|
| VCC | 5V |
| GND | GND |
| SDA | A4 |
| SCL | A5 |

SQW and 32K pins on the DS3231 module were left unconnected. The CR2032 coin cell was verified seated in the module's battery holder.

**Steps taken:**
1. Installed `RTClib` by Adafruit via Library Manager.
2. Wired DS3231 to Arduino per the table above on breadboard.
3. Uploaded sketch that calls `rtc.begin()`, handles `rtc.lostPower()` by setting time to compile time via `rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))`, and prints `rtc.now()` to Serial Monitor once per second at 9600 baud.
4. Observed first boot: `RTC lost power — setting time to compile time` message (expected on first run with fresh coin cell), followed by `RTC initialized.` and incrementing timestamps.
5. Backup battery test: unplugged USB, waited ~2 minutes 20 seconds, replugged.

**Evidence observed:**
- Time advanced correctly, one second per line, on first boot.
- On re-plug after 2:20 offline, Serial Monitor showed `RTC initialized.` (no "lost power" message) and time resumed at a value consistent with wall-clock elapsed time (e.g., went from `20:16:51` at power-off to `20:19:11` at power-on — ~2:20 elapsed, matching real time).
- Confirms: I2C communication works, RTClib library works, DS3231 temperature-compensated oscillator runs, and CR2032 backup battery keeps the time across power loss.

**Known residual offset:** RTC is set to compile time of the init sketch, which lags actual wall-clock time by a few seconds due to compile+upload delay. Acceptable for bench testing. Will be re-set precisely (via `rtc.adjust()` with a known value, or with a one-shot sketch that reads from serial input) before field deployment.

**Note on permanent wiring:** The RTC connections are stable and low-risk to migrate to a prototyping shield at any point. A dedicated DS3231 shield is not needed — the existing module works fine on a prototyping shield with 4 wires. Keep connections modular (header sockets or screw terminals) so the RTC can still be isolated during later debugging.

### Stage 2: Arduino + Manual Switch ✅ COMPLETE

**Goal:** Verify the DPDT switch correctly reports all three positions to the Arduino.

**Switch pin labeling (MR-108 actual labels):** The MR-108 is labeled `1a / 1 / 1b` for pole 1 and `2a / 2 / 2b` for pole 2 (the middle number is COM for that pole; `a` and `b` are the two switched positions). The physical lever uses Roman numerals `I` and `II` for the two active positions. No multimeter was available, so pin-to-position correspondence was determined empirically by observing Arduino Serial output and matching it to lever direction.

**Design decision — lever-to-mode mapping:** The architecture doc originally suggested `Position I = Manual Open, Position II = Manual Close`, but after the empirical test, the inverse was chosen as the canonical mapping for this build:

- **Position I → Manual Close**
- **Position II → Manual Open**
- **Center → Auto**

The choice was cosmetic (either mapping works correctly with the sketch), and keeping it matches how this unit was physically wired, avoiding a rework. The switch housing will be labeled accordingly when mounted on the coop.

**Connections used:**

| From (Switch) | To (Arduino) |
|---------------|--------------|
| Pin 1 (Pole 1 COM) | GND |
| Pin 1a | D2 |
| Pin 1b | (unconnected) |
| Pin 2 (Pole 2 COM) | GND |
| Pin 2a | (unconnected) |
| Pin 2b | D3 |

On this unit, `1a` turned out to correspond to position II (and `2b` to position I), which produced the I = Close / II = Open mapping above. If another build uses a different MR-108 unit with reversed `a`/`b` internal convention, the wiring-to-mapping correspondence may differ — verify empirically on each unit.

**Steps taken:**
1. Kept Stage 1 RTC wiring in place on breadboard.
2. Wired switch to Arduino per the table above (pins 1 and 2 to GND; 1a to D2; 2b to D3; 1b and 2a unconnected).
3. Extended Stage 1 sketch with `pinMode(D2, INPUT_PULLUP)` and `pinMode(D3, INPUT_PULLUP)`, and added decode logic that maps `(D2, D3)` readings to `MANUAL_OPEN` / `AUTO` / `MANUAL_CLOSE` / `INVALID`.
4. Uploaded and opened Serial Monitor at 9600 baud.
5. Held lever in each position for several seconds and observed Serial output.

**Evidence observed:**
| Lever position | D2 reading | D3 reading | Decoded mode |
|---|---|---|---|
| I | 1 (HIGH) | 0 (LOW) | MANUAL_CLOSE ✓ |
| Center | 1 (HIGH) | 1 (HIGH) | AUTO ✓ |
| II | 0 (LOW) | 1 (HIGH) | MANUAL_OPEN ✓ |

Transitions between positions were immediate; `INVALID` was never observed; RTC timestamps continued to advance correctly throughout the test, confirming the two subsystems coexist without interference.

**Known minor quirk:** Serial Monitor occasionally prints the same line twice with identical timestamps (likely a display-layer artifact, not a logic issue). Does not affect functionality; ignored for now.

**Note on software impact for later stages:** Because the `I = Close, II = Open` mapping was achieved through wiring (not by changing the sketch's decode logic), the sketch itself — including the `MANUAL_OPEN` / `MANUAL_CLOSE` label assignments — remains exactly as written in the Section 5 pseudocode. No code-level inversion is required downstream.

### Stage 3: Arduino + Relay Module — Signal-Side Only (No Battery) ✅ COMPLETE

**Goal:** Verify Arduino can drive both relay inputs (IN1/IN2) and the module's optocoupler/LED side responds correctly. This stage deliberately skips coil power — we're isolating the signal path first.

**What works without battery:** The relay module's VCC (5V) powers the optocoupler input LEDs and the channel indicator LEDs. When Arduino drives IN1 or IN2 LOW, the corresponding channel LED lights up. This proves: pin control, sketch logic, active-LOW behavior, and cable integrity.

**What does NOT work without battery:** The relay coils themselves are unpowered, so no audible click and no contact switching. That's fine — we confirm coil side in Stage 4.

**JD-VCC jumper: REMOVE IT** (even though JD-VCC itself is unpowered in this stage). Removing it now establishes the final configuration; no need to revisit later.

**Connections:**
| From (Relay Module) | To | Notes |
|---------------------|-----|-------|
| 4-pin header VCC | Arduino 5V | Optocoupler input supply |
| 4-pin header GND | Arduino GND | Logic ground reference |
| IN1 | Arduino D7 | Relay 1 trigger (extend / close) |
| IN2 | Arduino D8 | Relay 2 trigger (retract / open) |
| 3-pin header JD-VCC | **Unconnected** | Coil power — comes in Stage 4 |
| 3-pin header VCC | **Unconnected** | Jumper-related pin, unused when JD-VCC is separated |
| 3-pin header GND | **Unconnected** | Coil return — comes in Stage 4 |

**Sketch:** Extend the Stage 2 sketch with a one-shot relay exercise routine in `setup()` (after RTC init). The exercise: D7 LOW → wait 2 s → D7 HIGH → wait 1 s → same for D8 → alternating pattern (R1 on / R2 off, then R1 off / R2 on). All serial-logged with "expect LED on" / "expect LED off" annotations. Pre-set relay pins HIGH *before* `pinMode(OUTPUT)` to prevent boot-time glitches (see Section 8 → Relay Active-Low Clarification).

After the exercise, the loop continues as in Stage 2 — RTC time + switch decode printed once per second. This confirms the relay additions don't disrupt existing subsystems.

**Pass criteria (Stage 3):**
- Every `ON` serial line corresponds to a channel LED lighting up.
- Every `OFF` serial line corresponds to the LED going off.
- No LED activity during boot (pre-OUTPUT HIGH technique works).
- RTC timestamps continue advancing; switch decoding still works during the post-exercise loop.

**No click is expected at this stage.** If any of the above fail, debug before proceeding. Do not add the battery to compensate.

**Evidence observed:**
- Boot-time relay exercise ran cleanly — `R1 ON → R1 OFF → R2 ON → R2 OFF → (alternating R1/R2) → both OFF` — and both channel LEDs tracked the serial log exactly.
- No LED activity during Arduino reset or sketch upload (the `pinMode(INPUT) → digitalWrite(HIGH) → pinMode(OUTPUT)` pre-init pattern works as intended).
- No clicks audible (expected — JD-VCC unpowered).
- Post-exercise loop continued printing RTC timestamp + switch decode at 2 Hz; flipping the switch produced correct `MANUAL_OPEN` / `AUTO` / `MANUAL_CLOSE` transitions.
- JD-VCC jumper physically removed from the relay module and set aside.

**Note on switch ↔ relay independence at this stage:** During testing it was observed that flipping the switch does not cause any relay activity. This is by design — the Stage 3 sketch only *reads* the switch and prints the decoded mode; it never drives the relays from the switch state. The switch → relay linkage is a software behavior that's added in Stage 5d when the final control code (Section 5 pseudocode) is uploaded. Until then, switch and relays operate independently.

### Stage 4: Add Battery + Charge Controller — Coil Power via JD-VCC ✅ COMPLETE

**Goal:** Build the 12V+ and GND junctions, bring the P1006 charge controller and 12V battery online, and energize the relay coils via JD-VCC. Confirm audible click on every trigger, which proves the coil side works end-to-end. Actuator remains out of the circuit at this stage.

**Decision — fuse deferred to Stage 5:** Stage 4 ran unfused because the only load is ~100 mA of relay coils with no mechanical fault surface. A 7.5A inline fuse on the battery + lead is required before Stage 5, where the actuator introduces welded-contact, jam, and stall-current failure modes that wire gauge alone cannot protect against. See Stage 5 prerequisite.

**Ground topology — critical:** The relay module has two ground references once JD-VCC is in use — logic GND (4-pin header, to Arduino) and coil GND (3-pin header, to battery return via LOAD−). Both must meet at the common GND junction. Arduino's multiple GND pins are all tied internally, so using one GND pin for Stage 1/2 returns (RTC + switch) and a different GND pin for the junction is electrically equivalent to star-grounding at the Arduino.

**3-pin header pinout (this unit):** JD-VCC / VCC / GND. The middle **VCC** pin is only used when the factory jumper is installed (shorting JD-VCC to logic VCC). With the jumper removed in Stage 3, middle VCC stays unconnected.

**GND junction collects:**
| Wire | From | To |
|---|---|---|
| Fresh Arduino GND | Arduino GND (unused pin, separate from RTC/switch GND) | GND junction |
| Relay coil return | Relay module 3-pin header GND (rightmost) | GND junction |
| Controller LOAD return | P1006 LOAD− terminal | GND junction |

**12V+ junction collects:**
| Wire | From | To |
|---|---|---|
| Coil supply | Relay module 3-pin header JD-VCC (leftmost) | 12V+ junction |
| Controller LOAD supply | P1006 LOAD+ terminal | 12V+ junction |

**Battery connections (last step, powering up):**
| Wire | From | To |
|---|---|---|
| Battery negative | 12V Orion battery (−) | P1006 BAT− |
| Battery positive | 12V Orion battery (+) | P1006 BAT+ |

PV+ / PV− terminals remain empty — battery alone powers the bench test.

**Connection order (strict):**
1. Build GND junction (unpowered).
2. Build 12V+ junction (unpowered).
3. Land pigtails on P1006 LOAD+ / LOAD− (unpowered — verify polarity against silkscreen).
4. Connect battery BAT− first, then BAT+. System powers on; controller auto-calibrates to 12V.
5. Verify LOAD output live — if controller is in night-mode with no PV, check menu for always-on load mode or momentarily parallel battery across PV to force day-mode.
6. Reconnect Arduino USB. Stage 3 sketch's boot-time relay exercise should now produce audible clicks.

**Battery safety:**
- Remove rings, watches, metal jewelry before touching battery terminals.
- Connect BAT− first, BAT+ last. Disconnect in reverse order.
- Verify polarity twice before tightening — P1006 has no known reverse-polarity protection on BAT.
- Small spark on BAT+ contact (inrush to controller caps) is normal. Sustained arcing means a downstream short — disconnect immediately.
- Keep battery upright, in a ventilated area during testing.

**Evidence observed:**
- P1006 display powered on when battery was connected.
- With Arduino re-plugged to USB, the Stage 3 boot-time relay exercise produced audible clicks from the relay module on every transition — confirming end-to-end coil drive through the full signal → optocoupler → transistor → coil → contact path.
- No clicks during Arduino boot or sketch upload (the pre-init HIGH technique continues to hold with coils now energized — an important confirmation that Stage 3's boot-glitch prevention survives the addition of coil power).
- LOAD output worked with PV terminals empty — no night-mode workaround needed on this P1006 unit.

**Pass criteria (Stage 4) — all met:**
- ✅ Every `ON` serial line matched by LED on + audible click.
- ✅ Every `OFF` serial line matched by LED off + release click.
- ✅ No clicks during Arduino boot.
- ✅ RTC and switch decoding continue working after the exercise.


### Stage 5: Charge Controller + Actuator + Full Integration

**Why a fuse was added at this stage:** Stage 4 ran unfused because the only load was ~100 mA of relay coils with no mechanical fault surface. Stage 5 introduces the actuator, which brings realistic fault modes the wire gauge alone cannot protect against: welded relay contacts, mechanical jams, stalled motor currents, and inductive kickback. The 7.5A inline blade fuse on the battery + lead protects the charge controller and battery from these failure modes — not the wire — and blows for $0.50 instead of destroying $70 of components. Installation is recorded under 5a below.

**Goal:** Introduce the actuator and connect it through the relays in H-bridge configuration, and run the full control code end-to-end.

**What's new in this stage:**
1. **Solar panel can be connected to PV terminals**, but this is optional at this point — the controller runs on battery alone for bench testing. Full solar integration (Stage 5 with PV) can happen now or be deferred to enclosure mounting time.
2. **Actuator wired through the two relays in H-bridge configuration** so that one relay extends (closes door) and the other retracts (opens door).

This stage replaces the original Stages 4 (H-bridge wiring), 6 (full integration test), and 7 (solar + charge controller) — all three are now done together as a single integration step.

#### 5a: Inline Fuse + H-Bridge Actuator Wiring ✅ COMPLETE

**Note on scope:** Charge controller wiring (battery → P1006 → LOAD → 12V/GND junctions) was completed in Stage 4. The remaining 5a work was therefore (1) installing the inline fuse and (2) wiring the actuator through the relays in H-bridge configuration. Both are covered here.

**Inline fuse installation:**
- 7.5A automotive blade fuse holder installed in series with the battery + lead.
- Wiring: `Battery (+) → fuse holder lead 1 → fuse holder lead 2 → P1006 BAT+`.
- Fuse blade can be pulled to fully de-energize the 12V bus during wiring changes — used in this role throughout 5a actuator wiring (faster and safer than disconnecting BAT+ at the controller terminal).

**H-bridge wiring — actuator through relays:**

The two relay COMs become the two actuator terminals; NC and NO are tied to GND and 12V+ respectively, mirrored across the two relays. This produces polarity reversal: when both relays are at rest, both actuator leads sit on GND (no current); when exactly one relay fires, that lead flips to +12V while the other stays on GND, driving current through the motor in the corresponding direction.

| Relay terminal | Connect to |
|---|---|
| R1 NC | GND junction |
| R1 COM | Actuator lead A |
| R1 NO | 12V+ junction |
| R2 NC | GND junction |
| R2 COM | Actuator lead B |
| R2 NO | 12V+ junction |

**Junction extensions (additions to the Stage 4 junctions):**

| Junction | Stage 4 contents | New in Stage 5a |
|---|---|---|
| 12V+ junction | JD-VCC, P1006 LOAD+ | + R1 NO, R2 NO (4 wires total) |
| GND junction | Arduino GND, relay 3-pin GND, P1006 LOAD− | + R1 NC, R2 NC (5 wires total) |

The actuator leads (A, B) themselves do not land on either junction — they go to R1 COM and R2 COM, the "swing" terminals that toggle between the GND and 12V+ buses depending on relay state.

**H-bridge truth table (design reference):**

| Relay 1 (D7) | Relay 2 (D8) | Terminal A | Terminal B | Result |
|--------------|--------------|------------|------------|--------|
| OFF (HIGH) | OFF (HIGH) | GND (via NC) | GND (via NC) | No movement — holds |
| ON (LOW) | OFF (HIGH) | +12V (via NO) | GND (via NC) | Extends → closes door |
| OFF (HIGH) | ON (LOW) | GND (via NC) | +12V (via NO) | Retracts → opens door |
| ON (LOW) | ON (LOW) | +12V | +12V | **SHORT CIRCUIT — prevent in code** |

**Code-level safety:** Always de-energize the other relay first:
```
digitalWrite(RELAY_2, HIGH);   // ensure other relay OFF first
digitalWrite(RELAY_1, LOW);    // then energize this one
```

**Wiring sequence used (de-energized throughout):**
1. Pulled the fuse to kill 12V bus → P1006 display went dark, confirming bus de-energized.
2. Landed R1 NC and R2 NC into GND junction (now 5 wires).
3. Landed R1 NO and R2 NO into 12V+ junction (now 4 wires).
4. Landed actuator lead A on R1 COM, lead B on R2 COM. Lead-to-relay assignment was arbitrary — direction would be verified empirically in 5b, with lead swap as the corrective action if reversed.
5. Reinserted fuse → P1006 powered up normally.

**Evidence observed:**
- P1006 powered on cleanly when fuse was reinserted, confirming no shorts introduced by the new wiring.
- Boot-time relay exercise (Stage 3/4 sketch still loaded) produced clicks as expected, confirming the addition of the actuator path did not disrupt the coil drive.
- Note that confirmation of the actuator *moving* — and in the correct direction — is captured in 5b below, not here.

**Pass criteria (Stage 5a) — all met:**
- ✅ Fuse installed and verified seated (P1006 powers on with fuse in, dark with fuse out).
- ✅ All H-bridge wiring landed at NC/COM/NO and the existing junctions, no shorts.
- ✅ Battery → fuse → P1006 → LOAD → 12V bus → relays remains functional after additions.

#### 5b: Direction Verification (Short Bursts) ✅ COMPLETE

**Goal:** Before running the full control code, verify the extend=close / retract=open mapping with short manual triggers, so that any direction error is found with 3-second pulses on a free-moving actuator rather than during a 27-second forced-close routine.

**Sketch used:** A minimal direction-test sketch (no RTC, no switch logic) that on boot:
1. Pre-sets D7 and D8 HIGH before `pinMode(OUTPUT)` (preserves the boot-glitch prevention pattern from Stage 3).
2. `delay(2000 ms)` to let power rails settle.
3. Burst 1: D7 LOW for 3 seconds → D7 HIGH. Expect actuator to **extend** (rod pushes out, close direction).
4. 2-second pause.
5. Burst 2: D8 LOW for 3 seconds → D8 HIGH. Expect actuator to **retract** (rod pulls in, open direction).
6. Each burst de-energizes the other relay first (`digitalWrite(other, HIGH)`) before energizing this one — enforces the "never both relays ON" code-level safety rule from 5a.
7. Test runs once in `setup()` and stops; `loop()` is empty. Re-running requires a board reset.

**Setup:** Actuator laid flat on the bench, both ends free, rod positioned in the middle of its stroke so 3 seconds (~30 mm) had room to move in either direction without hitting an end stop.

**Evidence observed:**
- Sketch upload + serial output verified first with USB-only (no battery, no actuator) — signal-side confirmed, IN1 LED lit during burst 1, IN2 LED lit during burst 2, both for the expected 3-second durations.
- Then with battery connected (via fuse) and actuator wired to R1 COM / R2 COM:
  - Burst 1 (R1 ON): rod **extended** ✓ — matches "close door" direction.
  - Burst 2 (R2 ON): rod **retracted** ✓ — matches "open door" direction.
- Wiring is correct as built; no actuator lead swap was needed.

**Pass criteria (Stage 5b) — all met:**
- ✅ R1 produces extension (close direction).
- ✅ R2 produces retraction (open direction).
- ✅ No alarming noises, no fuse trip, no end-stop hits during test.

**Implication for 5c:** Because the wiring matches the design convention, the Section 5 pseudocode can be uploaded as-written — `RELAY_1` (D7) drives close, `RELAY_2` (D8) drives open, no inversions.

#### 5c: Full Integration Test ✅ COMPLETE

**Preparation:**
1. Upload the final control code (Section 5 pseudocode) with temporary `OPEN_TIME` and `CLOSE_TIME` values 2 minutes apart — e.g., open at current time + 1 minute, close at current time + 3 minutes.
2. Ensure actuator is free to move its full stroke (not mounted to the door yet).

**Test sequence:**
1. Power on — verify power-on routine runs (actuator extends for 27 seconds, internal limit switch cuts motor at end).
2. Confirm `door_state = CLOSED` via Serial print.
3. Wait for scheduled `OPEN_TIME` — verify actuator retracts for 27 seconds.
4. Flip switch to Manual Close (Position I) — verify actuator extends.
5. Flip switch to Manual Open (Position II) — verify actuator retracts.
6. Return switch to center — verify Arduino re-evaluates time and drives door to correct state.
7. Wait through a full open/close cycle on schedule — verify everything works without intervention.

**Evidence observed:**
- Power-on forced-close completed cleanly: actuator extended for 27 s, internal limit switch cut the motor at end of stroke, `door_state` initialized to `CLOSED`.
- Scheduled open at `OPEN_TIME`: actuator retracted for 27 s as expected.
- Scheduled close at `CLOSE_TIME`: actuator extended for 27 s as expected.
- Manual Open (Position II) from a closed door: actuator retracted, door opened.
- Manual Close (Position I) from an open door: actuator extended, door closed.
- Switch returned to center while inside the open window: auto mode evaluated the schedule and drove the actuator back to retract (door reopened) — exactly the resync behavior described in Section 4 and noted as open question #2 below.

**Pass criteria (Stage 5) — all met:**
- ✅ Power-on forced-close routine completes successfully.
- ✅ All three switch positions produce correct actuator behavior.
- ✅ Scheduled open/close transitions work without manual intervention.
- ✅ Returning from manual to auto mode correctly resyncs door state with current time.

**Bench test is largely complete.** One bench-side item remains before physical install: Stage 5d below, which transitions Arduino off USB and onto VIN-from-12V-bus power. After that, remaining work is physical: enclosure mounting, door mechanism integration, solar panel installation, and field tuning of `OPEN_TIME` / `CLOSE_TIME` — plus the optional Stage 6 sleep-mode optimization, planned for a later session.

**Open questions from bench testing — to revisit in a later session:**

These came up while running the Stage 5c sketch. The system passes the stated 5c criteria, but two behavioral observations are worth thinking through before (or as part of) Stage 6, since Stage 6's non-blocking refactor will likely affect both.

1. **Switch flip during a movement is ignored until the move completes.** If a 27-second open or close is in progress and the lever is moved (e.g., from Position II back to center, or from Position II directly to Position I), the Arduino does not react until the current `delay(ACTUATOR_TRAVEL_MS)` returns. This is a direct consequence of the blocking-delay design in Section 5's `open_door()` / `close_door()` functions and is consistent with the architecture as written — but it feels unresponsive in practice. Worth deciding whether the switch should be able to interrupt (and possibly reverse) an in-progress move. If yes, this requires moving from blocking delays to a non-blocking state-machine `loop()`, which is the same shift Stage 6 needs for sleep mode anyway. Considerations include: how to track door position when a move is aborted mid-travel (introduce an `UNKNOWN` state? always run full duration on next move?), and whether the switch should be able to *reverse* a move in flight or only *stop* it.

2. **After a manual override, returning the switch to center can immediately trigger the opposite move.** Specifically: if the schedule's open window has already passed and the user manually opens the door via Position II, then flips back to center, auto mode evaluates `is_open_hours()` as false and immediately closes the door. This is the documented "switch-to-center resyncs door state" behavior from Section 4 working exactly as designed — the door always converges to the schedule's expected state once auto mode resumes. It only feels surprising during bench testing because the test windows are short (a few minutes). In normal field operation the schedule will be hours long, so manually opening at noon and flipping back to auto won't trigger a close. No code change needed; this is captured here as a reminder when reviewing observation 1, since any change to switch responsiveness should preserve this resync behavior, not break it.

#### 5d: Transition Arduino to VIN Power (Off USB)

**Goal:** Move Arduino from USB-powered to 12V-bus-powered via the VIN pin, matching the architecture's intended power topology (Section 3, Section 6). Verify the system runs correctly without a USB tether before the unit is mounted in the enclosure.

**Why this is its own stage:** Every bench stage so far (1 through 5c) has run with Arduino on USB — initially because the Serial Monitor was needed for debugging, and later because the wiring just stayed that way. The VIN-from-12V-bus path is in the architecture diagram and pin map but has never been exercised. Catching any VIN-related issue (regulator heating, brownout during actuator inrush, RTC reset on power transition) on the bench is much easier than catching it after the unit is sealed in an enclosure on the coop.

**Connections to add:**
| From | To | Notes |
|---|---|---|
| 12V+ junction | Arduino VIN | New wire — feeds Arduino through its onboard linear regulator |
| GND junction | (already connected via Stage 4) | No new wire needed — Arduino GND is already tied to the common ground from Stage 4 |

The 12V+ junction grows from 4 wires (JD-VCC, P1006 LOAD+, R1 NO, R2 NO) to 5 wires (+ Arduino VIN).

**Steps:**
1. Pull the inline fuse to de-energize the 12V bus.
2. Land a new wire from the 12V+ junction to Arduino VIN.
3. Disconnect Arduino USB.
4. Reinsert the fuse → P1006 should power up, and Arduino should boot from VIN.
5. Verify boot: forced-close routine runs (27 s extend), `door_state = CLOSED`. Onboard power LED on Arduino should be lit; the relay channel LEDs should follow the boot-time exercise as before.
6. **Optional debug aid:** USB *can* be reconnected for Serial Monitor while VIN is also live — the Arduino Uno's onboard circuitry handles dual-source power safely (it auto-selects the higher source, typically VIN at ~12V over USB at ~5V). Useful if you want to watch serial output during the test without removing the VIN wire.
7. With USB disconnected, run a full open/close cycle on schedule (same `OPEN_TIME` / `CLOSE_TIME` window as Stage 5c) to confirm the system behaves identically on VIN power.
8. During each actuator run, watch for any sign of brownout — Arduino reset (would re-trigger the forced-close), RTC time loss, or relay glitches. The actuator's ~1 A inrush is the biggest stress event for the regulator path, so the open/close cycle is the real test.

**Pass criteria (Stage 5d):**
- Arduino boots and runs correctly with only VIN power (USB unplugged).
- Forced-close routine completes on power-up.
- Full scheduled open/close cycle works without USB connected.
- No brownout, reset, or RTC time loss during actuator runs.
- Onboard regulator does not get noticeably hot to the touch after a full cycle (warm is fine; uncomfortably hot suggests it's working too hard and a buck converter should be considered).

**If brownout or regulator heat is a problem:** The Arduino Uno's linear regulator drops 12 V → 5 V by burning the difference as heat (~7 V × ~50 mA ≈ 0.35 W at idle, more during peripheral activity). It's borderline acceptable for this load but not great. A small buck converter (12 V → 5 V or 12 V → 7 V into VIN) would solve it. This is an "if it becomes a problem" addition, not required up front. Note that Section 2's row 9 currently says "no buck converter needed" — if one is added here, update that row accordingly.

### Stage 6: Power Optimization via Sleep Mode

**Goal:** Add deep sleep to the Arduino so the system spends ~99% of its time in a low-power state, dramatically extending battery life. Must be done **after** Stage 5 passes — never optimize before the core system works.

**Motivation:**
With the full control code from Stage 5, the Arduino is awake 24/7 drawing ~40–45 mA even though the actuator only runs twice per day for ~27 seconds. Breaking down the baseline consumption:

| Source | Current | Notes |
|---|---|---|
| ATmega328P (active) | ~15 mA | Main processor |
| Onboard power LED | ~5 mA | Always lit |
| USB-Serial chip (ATmega16U2) | ~15 mA | Active even when USB disconnected |
| Linear voltage regulator (idle) | ~5 mA | 12V → 5V conversion loss |
| DS3231 RTC | ~1 mA | Already efficient |
| Relay module optocouplers (idle) | ~2 mA | LEDs on coil-side optoisolators |
| **Total idle** | **~40–45 mA** | ~0.5 W at 12V |

Daily consumption: ~45 mA × 24 h = **~1.1 Ah/day**

The actuator itself contributes only ~0.015 Ah/day (two 27-second runs at ~1 A). **99% of daily energy use is Arduino idle time.** This is where optimization matters.

#### Sleep Mode Selection

The ATmega328P offers several sleep modes, trading depth for wake-up latency and wake-up source flexibility. All options documented here; the choice for this build is **SLEEP_MODE_PWR_DOWN**.

| Mode | CPU | Peripherals | Current | Wake sources |
|---|---|---|---|---|
| `SLEEP_MODE_IDLE` | Stopped | All run | ~5–7 mA | Any interrupt (Timer, UART, etc.) |
| `SLEEP_MODE_ADC` | Stopped | ADC runs | ~3–4 mA | ADC conversion complete, external interrupt |
| `SLEEP_MODE_PWR_SAVE` | Stopped | Timer2 runs | ~1–2 mA | Timer2 overflow, external interrupt |
| `SLEEP_MODE_STANDBY` | Stopped | Crystal runs | ~0.5 mA | External interrupt (faster wake than PWR_DOWN) |
| **`SLEEP_MODE_PWR_DOWN`** ⭐ | Stopped | All stopped except WDT | **~0.1 mA** | External interrupt, pin change interrupt, watchdog timer |

**Why PWR_DOWN for this build:**
- **Deepest savings** — 99%+ reduction in CPU-side current (15 mA → 0.1 mA)
- **Wake sources match our needs** — we need (a) periodic wake for time checks and (b) wake on switch change. PWR_DOWN supports both via watchdog timer and pin change interrupts respectively.
- **Wake latency is acceptable** — ~6 clock cycles (microseconds). This project has no real-time constraints that would demand a faster mode.
- **Timer2 not needed** — we have a DS3231 for timekeeping, so PWR_SAVE's Timer2 advantage doesn't apply here.
- **Crystal not needed** — no serial communication or sensitive timing during sleep, so STANDBY's crystal running is wasted energy.

PWR_DOWN is the clear choice — no compromise is required given our wake source requirements.

#### Manual Switch Wake Problem

In PWR_DOWN mode the CPU is stopped, so the main loop's switch polling doesn't run. If the user flips the switch while Arduino is asleep, the sketch must be woken up by the pin change itself — otherwise the manual override feels broken ("I flipped the switch and nothing happened").

The switch is wired to D2 (INT0) and D3 (INT1) from Stage 2. These pins support hardware interrupts, which can wake PWR_DOWN sleep. However, a conflict arises if we also want to use DS3231's SQW pin for alarm-based wake: DS3231 SQW is typically routed to D2 (INT0) in low-power designs, which would collide with the switch.

**Three architectural options exist; the choice for this build is Option B.**

**Option A — Pin reorganization for both RTC alarm and switch interrupts**
- DS3231 SQW → D2 (INT0)
- Switch A moves from D2 to another pin (e.g. D4, using pin change interrupt / PCINT)
- Switch B stays on D3 (INT1)
- Two independent wake sources: RTC alarm for scheduled events, switch PCINT for manual override
- **Pros:** Most efficient — wake only on actual events, no periodic polling
- **Cons:** Requires rewiring the switch (breaking Stage 2 as-built), more complex interrupt handling code, DS3231 alarm configuration adds code complexity
- **Daily consumption estimate:** ~0.05 Ah/day

**Option B — Watchdog-based periodic wake + switch interrupts** ⭐
- Keep existing pin assignments (D2/D3 for switch, no SQW connection to DS3231)
- Watchdog timer wakes Arduino periodically (every WAKE_INTERVAL seconds)
- On wake, Arduino checks the RTC; if target time has arrived, runs action; if not, goes back to sleep
- Switch change also wakes Arduino immediately (via INT0/INT1 on D2/D3)
- **Pros:** No hardware changes, no Stage 2 rework, simpler code, easier debugging
- **Cons:** Slightly higher idle current (~2 mA vs ~0.5 mA) due to periodic wake overhead
- **Daily consumption estimate:** ~0.08 Ah/day

**Option C — All-interrupt approach (RTC alarm + switch, both via PCINT multiplexing)**
- Route DS3231 SQW and both switch lines through pin change interrupt registers
- Single interrupt vector decodes which source fired
- Maximum efficiency, minimum wake events
- **Pros:** Lowest possible consumption
- **Cons:** Most complex code, harder to debug interrupt source conflicts, requires careful PCINT mask management
- **Daily consumption estimate:** ~0.05 Ah/day

**Why Option B for this build:**
- **No rewiring** — Stage 2's switch wiring stays intact
- **No new hardware connections** — DS3231 SQW stays unconnected as it is today
- **14× power saving** (~1.1 → 0.08 Ah/day) is already an enormous win
- **Easier to debug** — periodic wake is predictable, easier to instrument
- **Gateway to Option A/C later** — if field use reveals consumption matters more than complexity, migration is straightforward

Option A is a reasonable future upgrade if battery life ever becomes the critical constraint. Option C is overkill for this application.

#### Watchdog Wake Interval

The ATmega328P watchdog timer has a hardware-limited maximum period of **8 seconds**. Selectable values: 15 ms, 30 ms, 60 ms, 120 ms, 250 ms, 500 ms, 1 s, 2 s, 4 s, 8 s. This is an Atmel/Microchip silicon-level constraint; no software workaround exists.

**But 8-second wakes ≠ 8-second polling overhead.** The approach is to use a software counter that tracks how many watchdog cycles have elapsed and only run the actual time-check logic once per target interval.

Example for a 1-minute check interval:
```
volatile uint8_t wdt_counter = 0;
const uint8_t WDT_CYCLES_PER_CHECK = 7;  // 7 × 8s ≈ 56s ≈ 1 minute

ISR(WDT_vect) {
    wdt_counter++;
}

loop():
    sleep_pwr_down()   // sleeps for up to 8 seconds
    // ... wakes here on WDT or switch interrupt ...
    if (wdt_counter >= WDT_CYCLES_PER_CHECK):
        wdt_counter = 0
        check_time_and_act()
    // else: fall through back to sleep immediately
```

The Arduino is awake for **microseconds** per watchdog cycle just to increment and compare the counter. Current during these micro-wakes is negligible.

**Selected check interval: 1 minute** (7 or 8 watchdog cycles). Justification:
- Chickens don't care about 30-second differences in open/close timing
- Battery savings are in the noise vs 30-second intervals
- Predictable and easy to reason about during debugging
- Leaves headroom if future features need finer timing

**Important:** Switch changes wake the Arduino *immediately* via INT0/INT1 interrupts, independent of the watchdog cycle. User experience of manual override is unaffected by the 1-minute poll interval — switch response is effectively instant.

#### Expected Savings (Option B, 1-minute interval)

Daily energy breakdown after optimization:

| State | Duration | Current | Daily energy |
|---|---|---|---|
| Deep sleep | 23h 59m | ~2 mA (Arduino 0.1 + LED 5 + USB chip 15 + reg 5 + relays 2, minus CPU savings — see note below) | 0.048 Ah |
| Wake for time check | ~1440 × ~10 ms/day = 14 s | 45 mA | 0.0002 Ah |
| Wake + actuator run | ~60 s | ~1000 mA | 0.017 Ah |
| **Total** | — | — | **~0.07 Ah/day** |

**Note on the "~2 mA" figure:** This is optimistic and assumes minor Arduino Uno power leaks (power LED, USB-Serial chip, voltage regulator quiescent current) reduce proportionally with CPU sleep. In reality these are constant regardless of CPU sleep state, so the practical idle current is likely **~25 mA**, yielding daily consumption closer to **~0.6 Ah/day**. The Uno's design isn't optimized for battery use — most of the idle current comes from peripherals the CPU can't turn off.

**Revised realistic estimate: ~0.6 Ah/day** (vs 1.1 Ah/day baseline = ~1.8× improvement).

This is still a significant gain but much less dramatic than the raw CPU-side numbers suggest. For larger improvements, migrating to a board without USB-Serial chip and power LED (e.g. Arduino Pro Mini, Nano clones with removable regulator, or a bare ATmega328P on a prototyping board) would be required. Those options are out of scope per user decision — sticking with Arduino Uno for this build.

#### Required Library

`LowPower.h` by Rocket Scream Electronics — standard, well-tested, handles ATmega328P sleep registers correctly. Install via Arduino Library Manager (search "Low-Power", select Rocket Scream's version).

Basic usage:
```cpp
#include <LowPower.h>

LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
// Also disables ADC and brown-out detector for max savings
```

#### Control Flow Changes

Stage 5's loop structure changes from continuous polling to event-driven wake cycles:

```
setup():
    (existing Stage 5 init code)
    initialize watchdog timer for 8-second period
    attach interrupts to D2 and D3 (switch pins) — trigger on CHANGE
    configure ADC, BOD to off for sleep

loop():
    read switch_state
    IF switch_state changed since last check:
        handle_switch_change()

    IF wdt_counter >= WDT_CYCLES_PER_CHECK:
        wdt_counter = 0
        current_time = read RTC
        evaluate_schedule(current_time, switch_state)

    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF)
    // sleeps here; wakes on WDT or switch interrupt
```

Door movement functions (`open_door`, `close_door`) remain identical to Stage 5 — the actuator still runs for 27 seconds with timed relay control. Sleep is only during idle periods, not during active movement.

#### Testing Approach

1. Upload sleep-enabled sketch with `OPEN_TIME` and `CLOSE_TIME` set 3–5 minutes apart
2. Observe serial output on wake-ups (use serial wake-up debugging — print timestamp on each wake)
3. Verify:
   - Scheduled open/close still triggers at correct times (within ±1 minute tolerance)
   - Switch changes wake immediately (no perceptible delay when flipping switch)
   - System returns to sleep correctly after each action
   - No phantom wakes or stuck-awake states
4. Optional: measure current draw with multimeter in series — should see ~25 mA idle instead of ~45 mA (if Arduino Uno leaks are real as predicted above)

#### Pass Criteria (Stage 6)

- Scheduled open/close continues to work correctly at expected times
- Switch override response is perceptually instant (< 1 second from flip to relay click)
- Serial log shows expected wake pattern (watchdog wakes ~every 8 s, switch interrupts on demand)
- No regressions from Stage 5 behavior
- Idle current measurably reduced (exact figure depends on board-specific leaks)

---

## 10. Open Questions / To-Do Before Build

- [x] ~~Test actuator for built-in limit switches~~ — **Confirmed: IP800 has built-in limit switches that cut motor power at end of stroke**
- [x] ~~Confirm relay module trigger voltage~~ — **Confirmed: Songle SRD-12VDC-SL-C on optoisolated 2-channel module; 5V logic from Arduino works, 12V for coils via JD-VCC**
- [ ] **Verify solar panel wattage** is sufficient (should be, given low daily power consumption)
- [ ] **Finalize open/close schedule times** based on local sunrise/sunset or personal preference
- [ ] **Decide on door mounting** — how actuator physically connects to the door (vertical slide, hinged, etc.)
- [ ] **Weatherproofing plan** — enclosure for Arduino, relay, and wiring; outdoor-rated connections

---

## 11. Future Enhancements (Out of Scope for v1)

- Light sensor (LDR) based opening instead of fixed schedule — door opens at dawn / closes at dusk
- Manual push-button for "run the full open/close cycle" regardless of current state
- Status LEDs to indicate door state and mode
- Buzzer for pre-movement warning
- Remote monitoring (WiFi/Bluetooth module)
- Chicken counter (IR sensor at door) to confirm all chickens are inside before closing
- Temperature/humidity sensor for coop monitoring
- Current sensing (ACS712) for closed-loop end-of-stroke detection and stall/jam detection
- **Inline fuse (5–7.5A automotive blade fuse)** on the 12V actuator power line — protects wiring and components if the actuator jams, a relay welds shut, or wiring shorts. Low-cost insurance (~$2). Recommended before permanent coop installation.
- WAG 222-series to make GND and 12V+ connections more secure and easier to manage.

---

## 12. Build Sequence (Current)

1. ~~Test linear actuator with battery~~ (done — limit switches confirmed)
2. ~~Acquire manual switch~~ (done)
3. ~~Wire and test RTC module (DS3231) with Arduino — Stage 1~~ (done, backup battery confirmed)
4. ~~Integrate manual switch with Arduino logic — Stage 2~~ (done, lever mapping empirically verified as I=Close / II=Open)
5. ~~Stage 3 — Relay module signal-side test (no battery, LED observation only)~~ (done, both channels verified via boot-time exercise sketch, no glitches, JD-VCC jumper removed)
6. ~~Stage 4 — Add battery + P1006 charge controller, energize relay coils via JD-VCC~~ (done, audible clicks on every transition, no boot glitches, LOAD active without PV)
7. ~~Stage 5a — Install inline 7.5A fuse, wire actuator through relays in H-bridge~~ (done, fuse seated, junctions extended with R1/R2 NC→GND and R1/R2 NO→12V+, no shorts)
8. ~~Stage 5b — Direction verification with 3-second bursts~~ (done, R1 extends and R2 retracts as designed, no lead swap needed)
9. ~~Stage 5c — Upload full control code, run end-to-end bench test (power-on forced-close, scheduled open/close, all three switch positions, switch-to-auto resync)~~ (done, all four pass criteria met)
10. **Stage 5d — Transition Arduino from USB to VIN-from-12V-bus power, verify full cycle still works without USB tether** ← next
11. Stage 6 — Add deep sleep (PWR_DOWN + watchdog + switch interrupts) to extend battery life (planned for a later session)
12. Mount in enclosure and install on coop
13. Add solar panel to charge controller PV terminals (if deferred from Stage 5)
14. Field test and tune timing