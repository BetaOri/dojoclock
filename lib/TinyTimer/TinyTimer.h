#ifndef TinyTimer_h
#define TinyTimer_h

#include <Arduino.h>

class TinyTimer {
 private:
  uint32_t start;
  uint32_t duration;

 public:
  TinyTimer();
  void set(uint32_t);
  void reset();
  bool ended();
};

#endif