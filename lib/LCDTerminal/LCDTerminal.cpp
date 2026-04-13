#include "LCDTerminal.h"

LCDTerminal::LCDTerminal(uint8_t address)
: lcd(address, 20, 4), row(0), col(0)
{
    for (int i = 0; i < 4; i++) {
        buffer[i] = "                    ";  // 20 spaces
    }
}

void LCDTerminal::begin() {
    lcd.begin();
    lcd.backlight();
    lcd.clear();
    refresh();
}


void LCDTerminal::refresh() {
    for (int i = 0; i < 4; i++) {
        lcd.setCursor(0, i);
        String line = buffer[i];

        // Pad manually (ESP32-safe)
        while (line.length() < 20) {
            line += ' ';
        }
        if (line.length() > 20) {
            line = line.substring(0, 20);
        }

        lcd.print(line);
    }
}

void LCDTerminal::scrollUp() {
    buffer[0] = buffer[1];
    buffer[1] = buffer[2];
    buffer[2] = buffer[3];
    buffer[3] = "                    ";  // 20 spaces

    row = 3;
    col = 0;

    refresh();
}

void LCDTerminal::clear() {
    for (int i = 0; i < 4; i++) {
        buffer[i] = "                    ";  // reset line
    }
    row = 0;
    col = 0;
    lcd.clear();
}

void LCDTerminal::writeChar(char c) {
    if (c == '\n') {
        row++;
        col = 0;
        if (row > 3) scrollUp();
        return;
    }

    buffer[row].setCharAt(col, c);
    col++;

    if (col >= 20) {
        col = 0;
        row++;
        if (row > 3) scrollUp();
    }
}

void LCDTerminal::print(const String &text) {
    for (char c : text) {
        writeChar(c);
    }
    refresh();
}

void LCDTerminal::println(const String &text) {
    print(text);
    writeChar('\n');
    refresh();
}

// Overloads
void LCDTerminal::print(const char *text) { print(String(text)); }
void LCDTerminal::println(const char *text) { println(String(text)); }

void LCDTerminal::print(int value) { print(String(value)); }
void LCDTerminal::println(int value) { println(String(value)); }

void LCDTerminal::print(long value) { print(String(value)); }
void LCDTerminal::println(long value) { println(String(value)); }

void LCDTerminal::print(float value) { print(String(value)); }
void LCDTerminal::println(float value) { println(String(value)); }

void LCDTerminal::print(double value) { print(String(value)); }
void LCDTerminal::println(double value) { println(String(value)); }
