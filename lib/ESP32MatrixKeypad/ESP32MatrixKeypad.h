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

#pragma once
#include <Arduino.h>

class ESP32MatrixKeypad {

public:
    ESP32MatrixKeypad(
        const char* keymap,
        const uint8_t* rowPins,
        const uint8_t* colPins,
        uint8_t rows,
        uint8_t cols,
        uint16_t debounceMs = 30,
        uint16_t scanIntervalMs = 20
    );

    void begin();
    char getKey();

private:
    void scanTask();
    static void scanTaskThunk(void* arg);

    void setAllRowsInput();
    void setAllColsInput();

    // Pure analog hygiene drain phase (safe even if a key is pressed)
    void drainPhase_v9();

    const char* _keymap;
    const uint8_t* _rowPins;
    const uint8_t* _colPins;

    uint8_t _rows;
    uint8_t _cols;

    uint16_t _debounceMs;
    uint16_t _scanIntervalMs;

    TaskHandle_t _taskHandle = nullptr;

    volatile char _rawKey = 0;
    volatile char _stableKey = 0;
    volatile char _pendingKey = 0;

    volatile bool _waitingRelease = false;
    volatile bool _hasPendingKey = false;

    volatile unsigned long _lastChange = 0;

    // Last time the drain phase ran (ms)
    uint32_t _lastDrainTime = 0;
};

