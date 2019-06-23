#ifndef __TETRIS_H
#define __TETRIS_H

#include <stdint.h> // for uint8_t

#define YSIZE 32
#define XSIZE 8

// piece types
#define NUMPIECES 7

typedef struct _offset {
  int8_t x;
  int8_t y;
} offset;

// Each piece has a single anchor pixel, which is always on; and three
// other pixels that are on, in each of four possible rotations. These 
// template files describe each of the other 3 pixels in each of those
// rotations.
typedef struct _tetTemplate {
  offset pixelsInRotation[4 /*rotations*/][4 /*offsets*/];
} tetTemplate;

// For the "super-rotation", we also try various offsets to force fit
// a piece when rotating it. The offsets differ by direction of
// rotation, hence 8 rotations
typedef struct _superRotationTemplate {
  offset kickOffset[8 /* rotations */][4 /* offsets */];
} superRotationTemplate;
  
class Tetris {
 public:
  Tetris();
  ~Tetris();

  void Init();

  void SetupTest();

  int GetSquare(int x, int y);
  bool Step(); // return true while game still going

  void RotateLeft();
  void RotateRight();
  void MoveLeft();
  void MoveRight();
  bool Drop();

  uint8_t pickNextPiece();

  uint8_t numFilledLines();
  uint8_t lastFilledLineIndex(uint8_t idx);
  bool changedPieceThisTurn();

  uint32_t score();

 private:
  bool StartDroppingNextPiece();

  void SetPiece(uint8_t v);

  bool IsPieceBlocked();
  bool TrySuperRotation(uint8_t isCCW, uint8_t isIPiece);

  void CheckForFilledLines();

 private:
  uint8_t board[YSIZE][XSIZE];
  uint8_t nextPieceIdx;
  uint8_t curPieceIdx;
  int8_t curPieceY;
  int8_t curPieceX;
  int8_t curPieceRotation;
  int8_t previousPieceFlags;

  uint8_t lastCompletedLines[4];
  uint8_t numCompletedLinesThisStep;
  bool pieceChangedThisTurn;

  uint32_t currentScore;
};

#endif
