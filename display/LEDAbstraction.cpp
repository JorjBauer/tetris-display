#include "LEDAbstraction.h"

#define LED_PIN 3 // must be pin 3 for DMA. And must be a constant here. Harrumph.

LEDAbstraction::LEDAbstraction()
{
}

LEDAbstraction::~LEDAbstraction()
{
}

void LEDAbstraction::clear(bool suppressRedraw)
{
  for (int i=0; i<NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  if (!suppressRedraw)
    FastLED.show();
}

// disables fademode
void LEDAbstraction::clearByScrolling()
{
  isFadeMode = false;

  for (int count=0; count<32; count++) {
    for (int y=31; y>=1; y--) {
      for (int x=0; x<8; x++) {
	SetLED(x,y,GetLED(x,y-1));
      }
    }
    for (int x=0; x<8; x++) {
      SetLED(x, 0, CRGB::Black);
    }
    Update();
  }
}

void LEDAbstraction::Init()
{
  isFadeMode = false;

  FastLED.addLeds<WS2812,LED_PIN,GRB>(leds,NUM_LEDS);
  FastLED.setBrightness(40);
}

void LEDAbstraction::Update()
{
  stepFader();
  FastLED.show();
}

void LEDAbstraction::SetLED(uint8_t x, uint8_t y, CRGB color)
{
  uint16_t targetPixel = (y&1) ? (8*(y) + x) : (8 * (y+1)-x-1);
  if (isFadeMode) {
    targetLEDs[targetPixel] = color;
    blendPoint = 0;
  } else {
    leds[targetPixel] = color;
  }
}

CRGB LEDAbstraction::GetLED(uint8_t x, uint8_t y)
{
  uint16_t targetPixel = (y&1) ? (8*(y) + x) : (8 * (y+1)-x-1);
  if (isFadeMode) {
    //    return leds[targetPixel];
    return targetLEDs[targetPixel];
  } else {
    return leds[targetPixel];
  }
}

void LEDAbstraction::setFadeMode(bool f)
{
  isFadeMode = f;
  if (f) {
    // set fade state from the current state when we turn on fading mode
    for (uint16_t i=0; i<NUM_LEDS; i++) {
      targetLEDs[i] = leds[i];
    }
    blendPoint = 0;
  } else {
    for (uint16_t i=0; i<NUM_LEDS; i++) {
      leds[i] = targetLEDs[i];
    }
  }
}

// Helper function that blends one uint8_t toward another by a given amount
static void nblendU8TowardU8( uint8_t& cur, const uint8_t target, uint8_t amount)
{
  if( cur == target) return;
  
  if( cur < target ) {
    uint8_t delta = target - cur;
    delta = scale8_video( delta, amount);
    cur += delta;
  } else {
    uint8_t delta = cur - target;
    delta = scale8_video( delta, amount);
    cur -= delta;
  }
}
static CRGB fadeTowardColor( CRGB& cur, const CRGB& target, uint8_t amount)
{
  nblendU8TowardU8( cur.red,   target.red,   amount);
  nblendU8TowardU8( cur.green, target.green, amount);
  nblendU8TowardU8( cur.blue,  target.blue,  amount);
  return cur;
}

extern void WLOG(uint8_t x);

void LEDAbstraction::stepFader()
{
  if (!isFadeMode)
    return;

  WLOG(200);
  if (blendPoint < 255) {
    blendPoint += 75;
    if (blendPoint > 255) {
      blendPoint = 255;
    }

  WLOG(201);
    nblend(leds, targetLEDs, NUM_LEDS, blendPoint);
  WLOG(202);
  }
  WLOG(203);
}

void LEDAbstraction::setBrightness(uint8_t b)
{
  FastLED.setBrightness(b);
}

void LEDAbstraction::stepColorWheel()
{
  static uint8_t counter = 0;
  counter++;
  if (counter == 0) counter++; // skip black
  CRGB newColor = CHSV(counter,255,255);

  for (int i=0; i<NUM_LEDS; i++) {
    if (isFadeMode) {
      if (targetLEDs[i].raw[0] != 0 || targetLEDs[i].raw[1] != 0 || targetLEDs[i].raw[2] != 0)
	targetLEDs[i] = newColor;
    } else {
      if (leds[i].raw[0] != 0 || leds[i].raw[1] != 0 || leds[i].raw[2] != 0)
	leds[i] = newColor;
    }
  }
  FastLED.show();
}
