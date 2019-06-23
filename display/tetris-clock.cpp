#include "tetris-clock.h"
#include "tetris.h"

#define MAX(x,y) ((x) > (y) ? (x) : (y))

extern tetTemplate tetromino[NUMPIECES];

enum {
  P_I = 0,
  P_O = 1,
  P_S = 2,
  P_Z = 3,
  P_L = 4,
  P_J = 5,
  P_T = 6,
  P_NONE = 7
};

typedef struct _clockNumTemplate {
  uint8_t pieceIndex[4];
  uint8_t rotation[4];
  offset position[4];
} clockNumTemplate;

// The origin of each of these numbers is the upper-left corner. The 
// "position" values are relative to that point, leading to the origin
// of each of the pieces (when properly placed). It's important that 
// each of the pieces is enumerated bottom-to-top, so we can "drop"
// them in place in order.
//
// Each of these pieces is aligned on a 5x5 grid. If there's a space,
// it's on the left side.

#define LEFTOFFSET 4

clockNumTemplate numberPieces[10] = {
  { { P_L, P_I, P_L, P_NONE },   { 1,1,2,0 }, { {0,3},{2,2},{1,0},{0,0} } }, // 0
  { { P_T, P_L, P_NONE, P_NONE}, { 0,3,0,0 }, { {1,4},{1,1},{0,0},{0,0} } }, // 1
  { { P_J, P_L, P_J, P_NONE },   { 0,2,2,0 }, { {1,5},{1,2},{1,0},{0,0} } }, // 2
  { { P_L, P_J, P_J, P_NONE },   { 0,2,2,0 }, { {1,5},{1,2},{1,0},{0,0} } }, // 3
  { { P_I, P_L, P_NONE, P_NONE}, { 1,1,0,0 }, { {2,2},{0,1},{0,0},{0,0} } }, // 4
  { { P_L, P_Z, P_L, P_NONE},    { 0,0,2,0 }, { {1,5},{1,3},{1,0},{0,0} } }, // 5
  { { P_L, P_L, P_L, P_NONE},    { 0,2,2,0 }, { {1,4},{1,2},{1,0},{0,0} } }, // 6
  { { P_I, P_J, P_NONE, P_NONE}, { 1,2,0,0 }, { {2,3},{1,0},{0,0},{0,0} } }, // 7
  { { P_L, P_I, P_S, P_J },      { 1,1,1,2 }, { {0,4},{2,3},{1,2},{1,0} } }, // 8
  { { P_L, P_L, P_L, P_NONE},    { 0,0,2,0 }, { {1,4},{1,2},{1,0},{0,0} } }, // 9
};

uint8_t numberHeights[10] = { 
  5, // 0
  5, // 1
  6, // 2
  6, // 3
  5, // 4
  6, // 5
  5, // 6
  6, // 7
  6, // 8
  5 // 9
};
const uint8_t interNumberGap = 1;
const uint8_t colonGap = 3;
const uint8_t colonHeight = 2;
const uint8_t colonXpos[2] = {2,5};

TetrisClock::TetrisClock(LEDAbstraction *p)
{
  ledPanel = p;

  queueTailPos = queueHeadPos = 0;
  queueSize = 0;

  currentPieceDropping.id = P_NONE;

  hourCounter = 0;
  minuteCounter = 0;
  secondCounter = 0;
}

TetrisClock::~TetrisClock()
{
}

static CRGB colorOfPiece(uint8_t idx)
{
  CRGB outColor;
  outColor = CHSV(HUE_GREEN,255,255);
  return outColor; // testing single-color

  switch (idx) {
  case P_I:
    outColor = CRGB::White;
    break;
  case P_O:
    outColor = CHSV(HUE_ORANGE,255,255);
    break;
  case P_S:
    outColor = CHSV(HUE_YELLOW,255,255);
    break;
  case P_Z:
    outColor = CHSV(HUE_AQUA,255,255);
    break;
  case P_L:
    outColor = CHSV(HUE_BLUE,255,255);
    break;
  case P_J:
    outColor = CHSV(HUE_PURPLE,255,255);
    break;
  case P_T:
    outColor = CHSV(HUE_PINK,255,255);
    break;
  }
  return outColor;
}

// Drop the pieces for a digit in to place, Tetris-style.
//
// d is the number to display (made up of tetris pieces)
//
// hpos is the horizontal pixel position (0-7) of the upper-left pixel
// of this digit
//
// vpos is the pixel vertical position of the upper-left pixel of this digit
void TetrisClock::drawDigitAt(uint8_t d, uint8_t hpos, uint8_t vpos)
{
  // Calculate the target position for this number
  uint8_t hPix = hpos;
  uint8_t vPix = vpos;
  
  // Drop each of the pieces that makes up that number in to the right position and rotation
  clockNumTemplate *tpl = &numberPieces[d];
  for (int i=0; i<4; i++) {
    if (tpl->pieceIndex[i] != P_NONE) {
      queuePieceToDrop(tpl->pieceIndex[i],
		       colorOfPiece(tpl->pieceIndex[i]),
		       hPix + tpl->position[i].x,
		       vPix + tpl->position[i].y,
		       tpl->rotation[i]);
    }
  }
}

void TetrisClock::drawTwoDigitsAt(uint8_t leftnum, uint8_t rightnum,
				  uint8_t hpos1, uint8_t vpos1,
				  uint8_t hpos2, uint8_t vpos2)
{
  clockNumTemplate *ln = &numberPieces[leftnum];
  clockNumTemplate *rn = &numberPieces[rightnum];
  for (int i=0; i<4; i++) {
    if (ln->pieceIndex[i] != P_NONE) {
      queuePieceToDrop(ln->pieceIndex[i],
		       colorOfPiece(ln->pieceIndex[i]),
		       hpos1 + ln->position[i].x,
		       vpos1 + ln->position[i].y,
		       ln->rotation[i]);
    }
    if (rn->pieceIndex[i] != P_NONE) {
      queuePieceToDrop(rn->pieceIndex[i],
		       colorOfPiece(rn->pieceIndex[i]),
		       hpos2 + rn->position[i].x,
		       vpos2 + rn->position[i].y,
		       rn->rotation[i]);
    }
  }
}

pieceElement TetrisClock::pop()
{
  if (queueSize) {
    pieceElement ret = dropQueue[queueHeadPos];
    queueHeadPos++;
    queueHeadPos = queueHeadPos % QUEUESIZE;
    queueSize--;
    return ret;
  } else {
    // This is really ugly.
    static pieceElement dummyPiece = { P_NONE };
    return dummyPiece;
  }
}

void TetrisClock::queuePieceToDrop(uint8_t idx,
				   CRGB color,
				   uint8_t xctr,
				   uint8_t yctr,
				   uint8_t rot)
{
  dropQueue[queueTailPos].id = idx;
  dropQueue[queueTailPos].color = color;
  dropQueue[queueTailPos].position.x = xctr;
  dropQueue[queueTailPos].position.y = yctr;
  dropQueue[queueTailPos].rotation = rot;
  queueTailPos++;
  queueTailPos %= QUEUESIZE;
  queueSize++;
}

// Return how many millis until we should call step() again, or 0 when
// we're completely done
unsigned long TetrisClock::step()
{
  unsigned long retDelay = 150; // assume 150ms delay

  // Is there a piece dropping right now? If not, then we need to get one
  if (currentPieceDropping.id == P_NONE) {
    if (queueSize) {
      currentPieceDropping = pop();
      
      currentPosition = { 4, -1 };
      currentRotation = 0;
    } else {
      // Ain't no more pieces; all done!
      return 0;
    }
  }

  // If we reach here, then something is dropping! If it isn't at the very top,
  // then we need to erase whatever we drew last step...
  if (currentPosition.y != -1) {
    for (int j=0; j<4; j++) {
      int8_t x = currentPosition.x + tetromino[currentPieceDropping.id].pixelsInRotation[currentRotation][j].x;
      int8_t y = currentPosition.y + tetromino[currentPieceDropping.id].pixelsInRotation[currentRotation][j].y;
      if (y >= 0 && y < 32 && x >= 0 && x < 8) {
	ledPanel->SetLED(x, y, CRGB::Black);
      }
    }
  }

  // shift its position/rotation (if we've started dropping)
  if (currentPosition.y != -1) {
    if (currentPosition.x > currentPieceDropping.position.x) currentPosition.x--;
    else if (currentPosition.x < currentPieceDropping.position.x) currentPosition.x++;
    else if (currentRotation != currentPieceDropping.rotation) currentRotation++;
    else retDelay = 50;
  }
  
  // Move down one row
  currentPosition.y++;

  // draw the piece
  for (int j=0; j<4; j++) {
    int8_t x = currentPosition.x + tetromino[currentPieceDropping.id].pixelsInRotation[currentRotation][j].x;
    int8_t y = currentPosition.y + tetromino[currentPieceDropping.id].pixelsInRotation[currentRotation][j].y;
    if (y >= 0 && y < 32 && x >= 0 && x < 8) {
      ledPanel->SetLED(x, y, currentPieceDropping.color);
    }
  }

  // Draw the changes
  ledPanel->Update();
  
  if (currentPosition.y == 0) {
    // If it's the first position on the screen, then delay extra,
    // like it's a new game piece while playing actual Tetris
    return 750;
  }
  else if (currentPosition.x == currentPieceDropping.position.x && currentRotation == currentPieceDropping.rotation) {
    // If it's in its final place, we're done with this piece.
    if (currentPosition.y == currentPieceDropping.position.y) {
      currentPieceDropping.id = P_NONE;
    }
  }

  return retDelay;
}

uint32_t TetrisClock::setTime(uint8_t h, uint8_t m, uint8_t s=0)
{
  // return the old time
  uint32_t ret = hourCounter;
  ret <<= 8;
  ret |= minuteCounter;
  ret <<= 8;
  ret |= secondCounter;

  // set the new time
  hourCounter = h;
  minuteCounter = m;
  secondCounter = s;

  // update the displayed time
  startDisplay();

  return ret;
}

void TetrisClock::startDisplay()
{
  // Reset the piece queue
  queueTailPos = queueHeadPos = 0;
  queueSize = 0;
  currentPieceDropping.id = P_NONE;

  // Queue up all of the pieces that need to be drawn. Do it from the
  // bottom to the top. Stick a colon in the middle.
  uint8_t ypos = 31; // count upward to find the upper-left corner of each piece

  uint8_t curH = hourCounter;
  uint8_t curM = minuteCounter;
  uint8_t curS = secondCounter;

  // the height of the minutes is the height of the taller of the numbers
  uint8_t minsHeight = MAX(numberHeights[curM%10], numberHeights[curM/10]);

  uint8_t leftoffset = 0;
  uint8_t rightoffset = 0;
  if (numberHeights[curM/10] != minsHeight)
    leftoffset++;
  if (numberHeights[curM%10] != minsHeight)
    rightoffset++;

  ypos -= (minsHeight+1);
  drawTwoDigitsAt(curM/10, curM%10, 1, ypos+leftoffset, 5, ypos+1+rightoffset);

  ypos -= (colonGap);
  queuePieceToDrop(P_O, colorOfPiece(P_O),
		   colonXpos[0], ypos, 0);
  queuePieceToDrop(P_O, colorOfPiece(P_O),
		   colonXpos[1], ypos, 0);

  uint8_t hrsHeight = MAX(numberHeights[curH%10], numberHeights[curH/10]);
  leftoffset = rightoffset = 0;
  if (numberHeights[curH/10] != hrsHeight)
    leftoffset++;
  if (numberHeights[curH%10] != hrsHeight)
    rightoffset++;


  ypos -= (colonGap + hrsHeight + 1);
  drawTwoDigitsAt(curH/10, curH%10, 0, ypos+leftoffset, 4, ypos+rightoffset+1);
}

uint32_t TetrisClock::curTime()
{
  uint32_t ret = hourCounter;
  ret <<= 8;
  ret |= minuteCounter;
  ret <<= 8;
  ret |= secondCounter;

  return ret;
}

// Called from the main loop() for maintenance
void TetrisClock::loop()
{
  static uint32_t lastLoopTime = 0;
  if (millis() > lastLoopTime) {
    lastLoopTime += 1 * 1000; // advance the loop counter 1
			      // second. (Yes, on startup this will
			      // race a little.)
    if (++secondCounter >= 60) {
      secondCounter = 0;
      if (++minuteCounter >= 60) {
	minuteCounter = 0;
	if (++hourCounter >= 24) {
	  hourCounter = 0;
	}
      }
    }
  }
}
