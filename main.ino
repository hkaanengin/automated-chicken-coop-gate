// =============================================================================
// Automated Chicken Coop Door — Stage 5c Full Integration Test
// =============================================================================
// Implements Section 5 control logic from the architecture doc.
// Bench test version: OPEN_TIME / CLOSE_TIME are placeholders set ~2 min apart
// at upload time. Edit the two constants below before each upload.
//
// Hardware assumptions (all verified through Stages 1-5b):
//   - DS3231 RTC on I2C (A4/A5), time already set
//   - Switch on D2/D3 with INPUT_PULLUP, mapping: I=Close, II=Open, Center=Auto
//   - Relay module active-LOW on D7 (R1, extend/close) and D8 (R2, retract/open)
//   - Actuator wired through relays in H-bridge per Stage 5a
//   - R1 extends (closes), R2 retracts (opens) — verified Stage 5b
// =============================================================================

#include <Wire.h>
#include <RTClib.h>

// ---------- Pin assignments (Section 8) --------------------------------------
const uint8_t PIN_RELAY_CLOSE = 7;   // R1: extend = close door
const uint8_t PIN_RELAY_OPEN  = 8;   // R2: retract = open door
const uint8_t PIN_SWITCH_A    = 2;   // 1a: LOW when lever in Position II (Open)
const uint8_t PIN_SWITCH_B    = 3;   // 2b: LOW when lever in Position I  (Close)

// ---------- Configuration parameters (Section 7) -----------------------------
// >>> EDIT THESE TWO LINES BEFORE EACH UPLOAD <<<
// Set them ~2 minutes apart, anchored to the current RTC time.
// Format: hour, minute (24-hour clock).
const uint8_t OPEN_HOUR   = 19;
const uint8_t OPEN_MINUTE = 45;
const uint8_t CLOSE_HOUR  = 19;
const uint8_t CLOSE_MINUTE = 47;

const unsigned long ACTUATOR_TRAVEL_MS  = 27000;  // 25s travel + 2s buffer
const unsigned long POWER_ON_SETTLE_MS  = 2000;
const unsigned long LOOP_DELAY_MS       = 500;

// ---------- Relay logical states (active-LOW module) -------------------------
const uint8_t RELAY_ON  = LOW;
const uint8_t RELAY_OFF = HIGH;

// ---------- Door / mode state ------------------------------------------------
enum DoorState { DOOR_OPEN, DOOR_CLOSED };
enum Mode { MODE_AUTO, MODE_MANUAL_OPEN, MODE_MANUAL_CLOSE, MODE_INVALID };

DoorState door_state = DOOR_CLOSED;  // set authoritatively in setup()
RTC_DS3231 rtc;

// =============================================================================
// Helpers
// =============================================================================

// Both relays OFF — safe idle state, also the H-bridge "hold" state.
void relays_off() {
    digitalWrite(PIN_RELAY_CLOSE, RELAY_OFF);
    digitalWrite(PIN_RELAY_OPEN,  RELAY_OFF);
}

// Drive close direction. Always de-energize the other relay FIRST to prevent
// the both-relays-ON short circuit (see Stage 5a truth table).
void drive_close() {
    digitalWrite(PIN_RELAY_OPEN,  RELAY_OFF);  // safety first
    digitalWrite(PIN_RELAY_CLOSE, RELAY_ON);
}

void drive_open() {
    digitalWrite(PIN_RELAY_CLOSE, RELAY_OFF);  // safety first
    digitalWrite(PIN_RELAY_OPEN,  RELAY_ON);
}

void close_door() {
    Serial.println(F("[ACTION] Closing door (extend, 27s)..."));
    drive_close();
    delay(ACTUATOR_TRAVEL_MS);
    relays_off();
    door_state = DOOR_CLOSED;
    Serial.println(F("[STATE] door_state = CLOSED"));
}

void open_door() {
    Serial.println(F("[ACTION] Opening door (retract, 27s)..."));
    drive_open();
    delay(ACTUATOR_TRAVEL_MS);
    relays_off();
    door_state = DOOR_OPEN;
    Serial.println(F("[STATE] door_state = OPEN"));
}

// Decode switch position from D2/D3 (INPUT_PULLUP, LOW = engaged).
Mode read_mode() {
    int a = digitalRead(PIN_SWITCH_A);  // LOW => Position II (Open)
    int b = digitalRead(PIN_SWITCH_B);  // LOW => Position I  (Close)
    if (a == HIGH && b == HIGH) return MODE_AUTO;
    if (a == LOW  && b == HIGH) return MODE_MANUAL_OPEN;
    if (a == HIGH && b == LOW ) return MODE_MANUAL_CLOSE;
    return MODE_INVALID;  // both LOW — should never occur on DPDT ON-OFF-ON
}

// Convert h:m to minutes-since-midnight for easy comparison.
uint16_t time_to_minutes(uint8_t h, uint8_t m) {
    return (uint16_t)h * 60 + m;
}

// Is `now` within the open window [OPEN, CLOSE)?
// Note: assumes OPEN < CLOSE on the same day. Fine for chickens (e.g. 07:00 - 20:00).
bool is_open_hours(const DateTime& now) {
    uint16_t cur   = time_to_minutes(now.hour(), now.minute());
    uint16_t open  = time_to_minutes(OPEN_HOUR,  OPEN_MINUTE);
    uint16_t close_ = time_to_minutes(CLOSE_HOUR, CLOSE_MINUTE);
    return (cur >= open) && (cur < close_);
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    // --- Step 1: pre-set relay pins HIGH BEFORE making them outputs ---------
    // (Section 8, "Relay Active-Low Clarification" — prevents boot-time glitch)
    pinMode(PIN_RELAY_CLOSE, INPUT);
    pinMode(PIN_RELAY_OPEN,  INPUT);
    digitalWrite(PIN_RELAY_CLOSE, HIGH);
    digitalWrite(PIN_RELAY_OPEN,  HIGH);
    pinMode(PIN_RELAY_CLOSE, OUTPUT);
    pinMode(PIN_RELAY_OPEN,  OUTPUT);

    // --- Step 2: switch inputs ----------------------------------------------
    pinMode(PIN_SWITCH_A, INPUT_PULLUP);
    pinMode(PIN_SWITCH_B, INPUT_PULLUP);

    // --- Step 3: serial + power-rail settle ---------------------------------
    Serial.begin(9600);
    delay(POWER_ON_SETTLE_MS);
    Serial.println();
    Serial.println(F("=== Coop Door Stage 5c: Full Integration ==="));
    Serial.print(F("Schedule: OPEN at "));
    Serial.print(OPEN_HOUR);  Serial.print(':');
    if (OPEN_MINUTE < 10) Serial.print('0'); Serial.print(OPEN_MINUTE);
    Serial.print(F(", CLOSE at "));
    Serial.print(CLOSE_HOUR); Serial.print(':');
    if (CLOSE_MINUTE < 10) Serial.print('0'); Serial.println(CLOSE_MINUTE);

    // --- Step 4: RTC --------------------------------------------------------
    if (!rtc.begin()) {
        Serial.println(F("[FATAL] RTC not found. Halting."));
        while (true) { /* stop here */ }
    }
    if (rtc.lostPower()) {
        Serial.println(F("[WARN] RTC lost power — setting to compile time"));
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    DateTime now = rtc.now();
    Serial.print(F("[RTC] Current time: "));
    Serial.print(now.hour()); Serial.print(':');
    if (now.minute() < 10) Serial.print('0'); Serial.print(now.minute()); Serial.print(':');
    if (now.second() < 10) Serial.print('0'); Serial.println(now.second());

    // --- Step 5: power-on forced close to establish known state -------------
    Serial.println(F("[INIT] Power-on forced close..."));
    close_door();   // sets door_state = CLOSED
    Serial.println(F("[INIT] Handing off to main loop."));
}

// =============================================================================
// Main loop
// =============================================================================
void loop() {
    DateTime now = rtc.now();
    Mode mode = read_mode();

    // Periodic status print (every loop) — useful for bench observation
    Serial.print('[');
    if (now.hour()   < 10) Serial.print('0'); Serial.print(now.hour());   Serial.print(':');
    if (now.minute() < 10) Serial.print('0'); Serial.print(now.minute()); Serial.print(':');
    if (now.second() < 10) Serial.print('0'); Serial.print(now.second());
    Serial.print(F("] mode="));
    switch (mode) {
        case MODE_AUTO:         Serial.print(F("AUTO        ")); break;
        case MODE_MANUAL_OPEN:  Serial.print(F("MANUAL_OPEN ")); break;
        case MODE_MANUAL_CLOSE: Serial.print(F("MANUAL_CLOSE")); break;
        case MODE_INVALID:      Serial.print(F("INVALID     ")); break;
    }
    Serial.print(F(" door="));
    Serial.println(door_state == DOOR_OPEN ? F("OPEN") : F("CLOSED"));

    switch (mode) {
        case MODE_MANUAL_OPEN:
            if (door_state != DOOR_OPEN) open_door();
            break;

        case MODE_MANUAL_CLOSE:
            if (door_state != DOOR_CLOSED) close_door();
            break;

        case MODE_AUTO:
            if (is_open_hours(now)) {
                if (door_state != DOOR_OPEN) open_door();
            } else {
                if (door_state != DOOR_CLOSED) close_door();
            }
            break;

        case MODE_INVALID:
            // Treat as fault — keep relays off, do nothing this iteration.
            // Will recover automatically when switch reaches a valid position.
            relays_off();
            break;
    }

    delay(LOOP_DELAY_MS);
}
