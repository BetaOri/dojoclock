#include <Arduino.h>
#include <TinyTimer.h>

TinyTimer shortTimer;
TinyTimer longTimer;

void setup() {}

void loop() {
  if (longTimer.ended()) {
    longTimer.set(6000);
  }

  if (shortTimer.ended()) {
    shortTimer.set(2000);
  }
}