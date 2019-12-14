#include "tetris.h"

#ifdef UNIX
#include <stdlib.h>
#include <ncurses.h>
#include <string.h> // for memcpy
#else
#include <Arduino.h>
#endif

// The display's upper-left is 0,0 and increases down and to the right.

// It's important that the I piece is first; we need that to determine which 
// superrotation template to use when rotating I-pieces
static uint8_t pieceSymbols[] = { '|', '*', 'z', 'Z', 'L', 'J', 'T' };

//The wall kick data is keyed off of the *new* rotation, not the old
static superRotationTemplate wallKickNotI = { 
  { 
  { {-1, 0}, {-1,1}, {0, -2}, {-1, -2} }, // rotation (CW) 3 >> 0
  { {-1, 0}, {-1, -1}, {0, 2}, {-1, 2} }, // rotation (CW) 0 >> 1
  { { 1, 0}, {1, 1}, {0, -2}, {1, -2} }, // rotation (CW) 1 >> 2
  { { 1, 0}, {1, -1}, {0, 2}, {1, 2} }, // rotation (CW) 2 >> 3

  { {1, 0}, {1, 1}, {0, -2}, {1, -2} }, // rotation (CCW) 1 << 0
  { {-1, 0}, {-1, -1}, {0, 2}, {1, 2} }, // rotation (CCW) 2 << 1
  { {-1, 0}, {-1,1}, { 0, -2}, {-1, -2} }, // rotation (CCW) 3 << 2
  { {1, 0}, {1, -1}, {0, 2}, {1, 2} } // rotation (CCW) 0 << 3
  }
};

static superRotationTemplate wallKickI = { 
  {
  { { 1, 0}, {-2, 0}, { 1,2}, {-2, -1} }, // rotation (CW) 3 >> 0
  { {-2, 0}, {1, 0}, {-2, 1}, {1, -2} }, // rotation (CW) 0 >> 1
  { {-1, 0}, {2, 0}, {-1, -2}, {2, 1} }, // rotation (CW) 1 >> 2
  { { 2, 0}, {-1, 0}, { 2, -1}, {-1,2} }, // rotation (CW) 2 >> 3

  { { 2, 0}, {-1, 0}, {2, -1}, {-1, 2} }, // rotation (CCW) 1 << 0
  { { 1, 0}, {-2, 0}, { 1,2}, {-2, -1} }, // rotation (CCW) 2 << 1
  { {-2, 0}, { 1, 0}, {-2,1}, { 1, -2} }, // rotation (CCW) 3 << 2
  { {-1, 0}, { 2, 0}, {-1, -2}, { 2,1} } // rotation (CCW) 0 << 3
  }
};


tetTemplate tetromino[NUMPIECES] = {
  { { { {-1, 0}, { 1, 0}, { 2, 0}, { 0, 0} }, // I XOXX X XOXX X
      { { 0, 1}, { 0, 2}, { 0,-1}, { 0, 0} }, //        O      O
      { {-1, 0}, { 1, 0}, { 2, 0}, { 0, 0} }, //        X      X
      { { 0, 1}, { 0, 2}, { 0,-1}, { 0, 0} }  //        X      X
    },
  },

  { { { { 1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }, // O XX XX XX XX
      { { 1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }, //   OX OX OX OX
      { { 1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }, //
      { { 1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }  //
    },
  },

  { { { {-1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }, // S  XX X   XX X
      { {-1, 0}, { 0, 1}, {-1,-1}, { 0, 0} }, //   XO  XO XO  XO
      { {-1, 0}, { 0,-1}, { 1,-1}, { 0, 0} }, //        X      X
      { {-1, 0}, { 0, 1}, {-1,-1}, { 0, 0} } 
    },
  },

  { { { { 1, 0}, { 0,-1}, {-1,-1}, { 0, 0} }, // Z XX   X XX   X
      { { 1, 0}, { 0, 1}, { 1,-1}, { 0, 0} }, //    OX OX  OX OX
      { { 1, 0}, { 0,-1}, {-1,-1}, { 0, 0} }, //       X      X
      { { 1, 0}, { 0, 1}, { 1,-1}, { 0, 0} } 
    },
  },

  { { { {-1, 0}, { 1, 0}, { 1,-1}, { 0, 0} }, // L   X X       XX
      { { 0, 1}, { 1, 1}, { 0,-1}, { 0, 0} }, //   XOX O  XOXX  O
      { {-1, 1}, {-1, 0}, { 1, 0}, { 0, 0} }, //       XX X     X
      { { 0, 1}, {-1,-1}, { 0,-1}, { 0, 0} } 
    },
  },

  { { { { 1, 0}, {-1, 0}, {-1,-1}, { 0, 0} }, // J X   XX XXOX  X
      { { 0, 1}, { 0,-1}, { 1,-1}, { 0, 0} }, //   XOX O     X  O
      { { 1, 1}, { 1, 0}, {-1, 0}, { 0, 0} }, //       X       XX
      { {-1, 1}, { 0, 1}, { 0,-1}, { 0, 0} } 
    },
  },

  { { { {-1, 0}, { 1, 0}, { 0,-1}, }, // T  X  X       X
      { { 1, 0}, { 0, 1}, { 0,-1}, }, //   XOX OX XOX XO
      { {-1, 0}, { 1, 0}, { 0, 1}, }, //       X   X   X
      { {-1, 0}, { 0, 1}, { 0,-1}, }  //
    },
  },

};



static int RandomLessThan(int limit)
{
#ifdef UNIX
  int r, d = RAND_MAX / limit;
  limit *= d;
  do { r = rand(); } while (r >= limit);
  return r / d;
#else
  return random(limit-1);
#endif
}

Tetris::Tetris()
{
  previousPieceFlags = 0;
}

Tetris::~Tetris()
{
}

void Tetris::Init()
{
  for (int x=0; x<XSIZE; x++) {
    for (int y=0; y<YSIZE; y++) {
      board[y][x] = 0;
    }
  }

  currentScore = 0;
  numCompletedLinesThisStep = 0;
  previousPieceFlags = 0;
  nextPieceIdx = pickNextPiece();
  StartDroppingNextPiece();
}

int Tetris::GetSquare(int x, int y)
{
  if (board[y][x]) {
    return board[y][x];
  }

  return ' ';
}

void Tetris::SetPiece(uint8_t v)
{
  for (int8_t i=0; i<4; i++) {
    int8_t y = curPieceY;
    int8_t x = curPieceX;

    y += tetromino[curPieceIdx].pixelsInRotation[curPieceRotation][i].y;
    x += tetromino[curPieceIdx].pixelsInRotation[curPieceRotation][i].x;

    if (y >= 0 &&
	y < YSIZE &&
	x >= 0 &&
	x < XSIZE) {
      board[y][x] = v ? pieceSymbols[curPieceIdx] : 0;
    }
  }
}

bool Tetris::Step()
{
  numCompletedLinesThisStep = 0;
  pieceChangedThisTurn = false;
  SetPiece(0);

  // Move piece down and re-add to board
  curPieceY++;
  if (IsPieceBlocked()) {
    curPieceY--;
    SetPiece(1);

    return StartDroppingNextPiece();
  } else {
    SetPiece(1);
  }
  return true;
}

bool Tetris::StartDroppingNextPiece()
{
  pieceChangedThisTurn = true;

  CheckForFilledLines();

  curPieceY = 0;
  curPieceX = XSIZE / 2;
  curPieceIdx = nextPieceIdx;
  curPieceRotation = 0;
  nextPieceIdx = pickNextPiece();

  if (IsPieceBlocked()) {
    SetPiece(1);
    return false;
  }

  SetPiece(1);
  return true;
}

void Tetris::CheckForFilledLines()
{
  numCompletedLinesThisStep = 0;

  for (int8_t y=YSIZE-1; y>=0; y--) {
    uint8_t count = 0;
    for (int8_t x=0; x<XSIZE; x++) {
      if (board[y][x]) {
	count++;
      }
    }
    if (count == XSIZE) {
      // Row is full - delete it, move everything down, and repeat
      // this line's check
      lastCompletedLines[numCompletedLinesThisStep] = y - numCompletedLinesThisStep;
      numCompletedLinesThisStep++;
      currentScore++;

      for (int8_t y2=y; y2>0; y2--) {
	memcpy(&board[y2][0], &board[y2-1][0], sizeof(uint8_t) * XSIZE);
      }
      memset(&board[0][0], 0, sizeof(uint8_t) * XSIZE);
      y++;
    }
  }
}

// Jigger up curPieceX/Y to try the 4 superrotation positions for this piece.
// If none of them fits, return curPieceX/Y to the original state and return 
// false.
bool Tetris::TrySuperRotation(uint8_t isCCW, uint8_t isIPiece)
{
  // Save starting position, cause we're going to mess with it
  int8_t oldy = curPieceY;
  int8_t oldx = curPieceX;
  
  // Try the four offsets given our current rotation
  for (int i=0; i<4; i++) {
    if (isIPiece) {
      curPieceX = oldx + wallKickI.kickOffset[curPieceRotation+4*isCCW][i].x;
      curPieceY = oldy + wallKickI.kickOffset[curPieceRotation+4*isCCW][i].y;
    } else {
      curPieceX = oldx + wallKickNotI.kickOffset[curPieceRotation+4*isCCW][i].x;
      curPieceY = oldy + wallKickNotI.kickOffset[curPieceRotation+4*isCCW][i].y;
    }
    if (!IsPieceBlocked()) {
      // It fits here! Do it.
      return true;
    }
  }
   
  // None of them must have fit. Return to the original state.
  curPieceY = oldy;
  curPieceX = oldx;
  return false;
}

void Tetris::RotateLeft()
{
  pieceChangedThisTurn = false;
  SetPiece(0);
  curPieceRotation--;
  if (curPieceRotation < 0) 
    curPieceRotation += 4;
  if (IsPieceBlocked() && !TrySuperRotation(true, curPieceIdx == 0)) {
    curPieceRotation++;
    curPieceRotation %= 4;
  }
  SetPiece(1);
}

void Tetris::RotateRight()
{
  pieceChangedThisTurn = false;
  SetPiece(0);
  curPieceRotation++;
  curPieceRotation %= 4;
  if (IsPieceBlocked() && !TrySuperRotation(false, curPieceIdx == 0)) {
    curPieceRotation--;
    if (curPieceRotation < 0)
      curPieceRotation += 4;
  }
  SetPiece(1);
}

void Tetris::MoveLeft()
{
  pieceChangedThisTurn = false;
  SetPiece(0);

  curPieceX--;
  if (curPieceX < 0 || IsPieceBlocked()) {
    curPieceX++;
  }

  SetPiece(1);
}

void Tetris::MoveRight()
{
  pieceChangedThisTurn = false;
  SetPiece(0);

  curPieceX++;
  if (curPieceX >= XSIZE || IsPieceBlocked()) {
    curPieceX--;
  }

  SetPiece(1);
}

bool Tetris::Drop()
{
  pieceChangedThisTurn = false;
  int8_t origY = curPieceY;

  SetPiece(0);
  while (!IsPieceBlocked()) {
    curPieceY++;
  }
  curPieceY--;
  SetPiece(1);

  return StartDroppingNextPiece();
}

bool Tetris::IsPieceBlocked()
{
  for (int8_t i=0; i<4; i++) {
    int8_t y = curPieceY;
    int8_t x = curPieceX;
    if (board[y][x])
      return true;

    y += tetromino[curPieceIdx].pixelsInRotation[curPieceRotation][i].y;
    x += tetromino[curPieceIdx].pixelsInRotation[curPieceRotation][i].x;

    if (x < 0 || x >= XSIZE) {
      // horizontally offscreen is always a problem
      return true;
    }

    if (y >= 0) {
      // It's onscreen; test for collisions or out of bounds
      if (y >= YSIZE) {
	// Off the bottom of screen
	return true;
      }

      if (x < 0 || x >= XSIZE) {
	// horizontally offscreen
	return true;
      }

      if (board[y][x]) {
	// blocked by something onscreen
	return true;
      }
    }
  }

  return false;
}

uint8_t Tetris::pickNextPiece()
{
  uint8_t nrp = 0; // figure out how many pieces are left
  for (int i=0; i<NUMPIECES; i++) {
    if (! (previousPieceFlags & (1 << i)) ) {
      nrp++;
    }
  }
  if (nrp == 0) {
    // safety: reset b/c we found no available pieces
    nrp = 7;
    previousPieceFlags = 0;
  }

  // pick which piece we want
  uint8_t choice = RandomLessThan(nrp+1);

  uint8_t ret = 0;
  // find that piece in the list of what's free
  nrp = 0;
  for (int i=0; i<NUMPIECES; i++) {
    if (!(previousPieceFlags & (1 << i))) {
      if (nrp == choice) {
        ret = i;
        break;
      }
      nrp++;
    }
  }

  // don't repeat any pieces until we've had them all out
  previousPieceFlags |= (1 << ret);
  if (previousPieceFlags == 0x7F) {
    // picked all the pieces: restart
    previousPieceFlags = 0;
  }

  return ret;
}

uint8_t Tetris::numFilledLines()
{
  return numCompletedLinesThisStep;
}

uint8_t Tetris::lastFilledLineIndex(uint8_t idx)
{
  return lastCompletedLines[idx];
}

bool Tetris::changedPieceThisTurn()
{
  return pieceChangedThisTurn;
}

uint32_t Tetris::score()
{
  return currentScore;
}

void Tetris::SetupTest()
{
  for (int y=21; y<32; y++) {
    for (int x=0; x<5; x++) {
      board[y][x] = '*';
    }
    if (y > 22)
      board[y][7] = '*';
  }
  board[23][6] = '*';
  board[25][6] = board[26][6] = '*';
  board[28][6] = board[29][6] = '*';
  board[31][6] = '*';

  board[21][5] = '*';
  board[20][5] = '*';
}
