#ifndef _TETRIS_CLOCK_H
#define _TETRIS_CLOCK_H

#include <stdint.h>
#include "LEDAbstraction.h"
#include "tetris.h"

// Size of the backing piece queue: max of 4 pieces per number, plus 2 for the colon
#define QUEUESIZE 18

typedef struct _pieceElement {
  uint8_t id;
  CRGB color;
  offset position;
  uint8_t rotation;
} pieceElement;

class TetrisClock {
 public:
  TetrisClock(LEDAbstraction *p);
  ~TetrisClock();

  uint32_t setTime(uint8_t h, uint8_t m, uint8_t s, uint8_t curMon, uint8_t curDay);

  void drawDigitAt(uint8_t d, uint8_t hpos, uint8_t vpos);
  void drawTwoDigitsAt(uint8_t leftnum, uint8_t rightnum,
		       uint8_t hpos1, uint8_t vpos1, 
		       uint8_t hpos2, uint8_t vpos2);

  unsigned long step();

  void queuePieceToDrop(uint8_t idx,
			CRGB color,
			uint8_t xctr,
			uint8_t yctr,
			uint8_t rot);
  pieceElement pop();

  void startDisplay();

  uint32_t curTime();

  void loop();

 private:
  void queueTreePieces();


  LEDAbstraction *ledPanel;

  // While dropping a piece, here's the state:
  offset currentPosition;
  uint8_t currentRotation;
  pieceElement currentPieceDropping;

  pieceElement dropQueue[QUEUESIZE];
  uint8_t queueTailPos;
  uint8_t queueHeadPos;
  uint8_t queueSize;

  uint8_t hourCounter;
  uint8_t minuteCounter;
  uint8_t secondCounter;
  uint8_t currentDay;
  uint8_t currentMonth;

  bool isInTreeMode;

};

#endif
