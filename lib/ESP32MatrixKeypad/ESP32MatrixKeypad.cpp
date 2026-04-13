// ============================================================================
// ESP32MatrixKeypad_v9
// Ultra‑Safe Keypad Scanner for:
// - 4.7k series resistors on rows
// - 47k external pullups on columns
// - No internal pullups required
//
// Logic:
// - Idle column = HIGH (47k pullup)
// - Pressed key = LOW (active row drives LOW through key)
// - Rows idle as INPUT (high‑Z)
// - Rows driven LOW only during scan (open‑drain emulation)
// - Columns are never driven during normal scan, only read as inputs
// - Periodic analog hygiene drain phase keeps long cable capacitance neutralized
// ============================================================================

#include "ESP32MatrixKeypad.h"

// Conservative timings, tune down once stable
static const uint16_t T_DISCHARGE_US           = 100;   // all rows high‑Z (50 microS)
static const uint16_t T_ROW_SETTLE_US          = 80;    // after driving row LOW (40)
static const uint16_t T_ROW_RELEASE_US         = 40;    // after releasing row (20)
static const uint32_t DRAIN_PHASE_INTERVAL_MS  = 10000; // time between hygiene drain phases ms (10 seconds)

ESP32MatrixKeypad::ESP32MatrixKeypad(
    const char* keymap,
    const uint8_t* rowPins,
    const uint8_t* colPins,
    uint8_t rows,
    uint8_t cols,
    uint16_t debounceMs,
    uint16_t scanIntervalMs
)
: _keymap(keymap),
  _rowPins(rowPins),
  _colPins(colPins),
  _rows(rows),
  _cols(cols),
  _debounceMs(debounceMs),
  _scanIntervalMs(scanIntervalMs)
{}

void ESP32MatrixKeypad::setAllRowsInput() {
    for (int r = 0; r < _rows; r++) {
        pinMode(_rowPins[r], INPUT); // high‑Z, never driven HIGH
    }
}

void ESP32MatrixKeypad::setAllColsInput() {
    for (int c = 0; c < _cols; c++) {
        pinMode(_colPins[c], INPUT); // external 47k pullups define idle HIGH
    }
}

// ============================================================================
// Pure Analog Hygiene Drain Phase (Safe Even If Key Is Pressed)
// - Columns briefly driven LOW to collapse cable capacitance
// - Rows briefly driven LOW through 4.7k series resistors
// - No pin is ever driven HIGH
// ============================================================================
void ESP32MatrixKeypad::drainPhase_v9() {

    // --- 1. Strong discharge of columns ---
    // Rows remain INPUT (high‑Z) during this step
    for (int c = 0; c < _cols; c++) {
        pinMode(_colPins[c], OUTPUT);
        digitalWrite(_colPins[c], LOW);   // safe: never drive HIGH
    }
    delayMicroseconds(300);               // Tune # 1) tune if needed (Most important) (150! 60-100–300 µs)

    // Release columns back to input (47k pullups restore HIGH)
    for (int c = 0; c < _cols; c++) {
        pinMode(_colPins[c], INPUT);
    }

    // --- 2. Soft discharge of rows ---
    // Columns are now pulled HIGH again via 47k
    for (int r = 0; r < _rows; r++) {
        pinMode(_rowPins[r], OUTPUT);
        digitalWrite(_rowPins[r], LOW);   // through 4.7k series resistors
    }
    delayMicroseconds(160); //Tune #2 (80! 30-50)

    // Return rows to high‑Z
    for (int r = 0; r < _rows; r++) {
        pinMode(_rowPins[r], INPUT);
    }

    // Small settle time
    delayMicroseconds(80); // Tune #3 Least sensitive parameter (40! 10-20)
}

void ESP32MatrixKeypad::begin() {

    // Safe startup: everything high‑Z, columns as inputs
    setAllRowsInput();
    setAllColsInput();

    // Run hygiene immediately at startup to flatten any stored charge
    drainPhase_v9();
    _lastDrainTime = millis();

    xTaskCreate(
        ESP32MatrixKeypad::scanTaskThunk,
        "KeypadScan",
        2048,
        this,
        0,
        &_taskHandle
    );
}

void ESP32MatrixKeypad::scanTaskThunk(void* arg) {
    static_cast<ESP32MatrixKeypad*>(arg)->scanTask();
}

void ESP32MatrixKeypad::scanTask() {

    const TickType_t delayTicks = pdMS_TO_TICKS(_scanIntervalMs);

    for (;;) {

        unsigned long now = millis();
        char newRaw = 0;

        // ------------------------------------------------------------
        // Non‑blocking periodic drain phase
        // ------------------------------------------------------------
        if (now - _lastDrainTime >= DRAIN_PHASE_INTERVAL_MS) {
            drainPhase_v9();
            _lastDrainTime = now;
        }

        // ------------------------------------------------------------
        // Global discharge: all rows high‑Z
        // (columns stay as inputs with external pullups)
        // ------------------------------------------------------------
        setAllRowsInput();
        delayMicroseconds(T_DISCHARGE_US);

        // ------------------------------------------------------------
        // Scan each row (open‑drain: only drive LOW)
        // ------------------------------------------------------------
        for (int r = 0; r < _rows; r++) {

            // Ensure all rows are high‑Z before activating one
            setAllRowsInput();

            // Activate this row: OUTPUT, LOW
            pinMode(_rowPins[r], OUTPUT);
            digitalWrite(_rowPins[r], LOW);
            delayMicroseconds(T_ROW_SETTLE_US);

            // Read columns: LOW = pressed (pulled down through this row)
            for (int c = 0; c < _cols; c++) {
                if (digitalRead(_colPins[c]) == LOW) {
                    newRaw = _keymap[r * _cols + c];
                    // "last key wins" if multiple pressed
                }
            }

            // Deactivate row back to high‑Z
            pinMode(_rowPins[r], INPUT);
            delayMicroseconds(T_ROW_RELEASE_US);
        }

        // ------------------------------------------------------------
        // Debounce (same behavior as your V6)
        // ------------------------------------------------------------
        if (newRaw != _rawKey) {
            _rawKey = newRaw;
            _lastChange = now;
        }

        if ((now - _lastChange) >= _debounceMs) {
            if (_rawKey == 0) {
                _waitingRelease = false;
                _stableKey = 0;
            } else {
                if (!_waitingRelease) {
                    _waitingRelease = true;
                    _stableKey = _rawKey;

                    if (!_hasPendingKey) {
                        _pendingKey = _stableKey;
                        _hasPendingKey = true;
                    }
                }
            }
        }

        vTaskDelay(delayTicks);
    }
}

char ESP32MatrixKeypad::getKey() {
    if (_hasPendingKey) {
        char k = _pendingKey;
        _hasPendingKey = false;
        return k;
    }
    return 0;
}
