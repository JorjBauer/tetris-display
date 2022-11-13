#ifndef __LEDABSTRACTION_H
#define __LEDABSTRACTION_H

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

#define NUM_COLS 32
#define NUM_ROWS 8
#define NUM_LEDS (NUM_ROWS * NUM_COLS)

class LEDAbstraction {
 public:
  LEDAbstraction();
  ~LEDAbstraction();

  void Init();
  void Update();

  CRGB GetLED(uint8_t x, uint8_t y);
  void SetLED(uint8_t x, uint8_t y, CRGB color);

  void setFadeMode(bool f);
  void stepFader();

  void clear(bool suppressRedraw = false);
  void clearByScrolling();

  void setBrightness(uint8_t b);

  void stepColorWheel();

 private:
  CRGB leds[NUM_LEDS]; // actual current state
  CRGB targetLEDs[NUM_LEDS]; // expected final state
  uint16_t blendPoint;
  
  bool isFadeMode;
};

#endif
