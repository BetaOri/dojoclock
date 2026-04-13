#include <Arduino.h>
#include "LCDTerminal.h"

LCDTerminal terminal(0x27);

void setup() {
    terminal.begin();

    terminal.println("Booting...");
    terminal.println("System Ready");

    // Direct access to LCD
    auto& lcd = terminal.getLCD();
    lcd.setCursor(0, 3);
    lcd.print("Direct LCD access");
}

void loop() {
    terminal.print("Hello ");
    delay(500);
}
