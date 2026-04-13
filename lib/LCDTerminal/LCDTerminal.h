#ifndef LCD_TERMINAL_H
#define LCD_TERMINAL_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

class LCDTerminal {
public:
    LCDTerminal(uint8_t address); // assumes 20x4 LCD

    void begin();
    void refresh();
    void scrollUp();
    void clear();

    void print(const String &text);
    void println(const String &text);

    // Lightweight overloads
    void print(const char *text);
    void println(const char *text);

    void print(int value);
    void println(int value);

    void print(long value);
    void println(long value);

    void print(float value);
    void println(float value);

    void print(double value);
    void println(double value);

    // Expose internal LCD instance
    LiquidCrystal_I2C& getLCD() { return lcd; }

private:
    LiquidCrystal_I2C lcd;
    String buffer[4];
    uint8_t row;
    uint8_t col;

    void writeChar(char c);
};

#endif
