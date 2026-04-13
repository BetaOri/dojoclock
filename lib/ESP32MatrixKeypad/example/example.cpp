#include <ESP32MatrixKeypad.h>

const char keymap[16] = {
  '1','2','3','A',
  '4','5','6','B',
  '7','8','9','C',
  'G','0','S','D'
};

uint8_t rowPins[4] = {12, 11, 10, 9};
uint8_t colPins[4] = {8, 7, 6, 5};

ESP32MatrixKeypad keypad(keymap, rowPins, colPins, 4, 4);

void setup() {
    Serial.begin(115200);
    keypad.begin();
}

void loop() {
    char key = keypad.getKey();
    if (key) {
        Serial.print("Key pressed: ");
        Serial.println(key);
    }
}
