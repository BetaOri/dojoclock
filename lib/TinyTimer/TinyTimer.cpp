#include <TinyTimer.h>

TinyTimer::TinyTimer() {
  reset();
};

void TinyTimer::reset() {
  duration = -1;
};

void TinyTimer::set(uint32_t newDuration) {
  start = millis();
  duration = newDuration;
};

bool TinyTimer::ended() {
  uint32_t now = millis();
  uint32_t end = start + duration;

  // normal case
  if (end > start) return now > end || now < start;
  // overflow case
  if (end < start) return now > end && now < start;
  // duration 0 means it's always tripped, maximum means it's not set
  return duration == 0;
};