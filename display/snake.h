#ifndef __SNAKE_H
#define __SNAKE_H

#include <stdint.h> // for uint8_t
#include "tetris.h"

#ifndef YSIZE
#define YSIZE 32
#endif
#ifndef XSIZE
#define XSIZE 8
#endif

// piece types
#define NUMPIECES 7

/*typedef struct _offset {
  int8_t x;
  int8_t y;
  } offset;*/

typedef struct _offsetList {
  offset pos;
  struct _offsetList *next;
} offsetList;

class Snake {
 public:
  Snake();
  ~Snake();

  void Init();

  void SetupTest();

  int GetSquare(int x, int y);
  bool Step(); // return true while game still going

  void TurnLeft();
  void TurnRight();

  uint32_t score();

  // private:
 public:
  bool IsPieceBlocked();
  
  void AddSnakeBlock(int x, int y);
  int BlockListSize();
  void DeleteOldestSnakeBlock();

  void AddRandomFood();

  // private:
 public:
  uint8_t board[YSIZE][XSIZE];
  offsetList *snakeBlockList;
  uint8_t currentLength;
  int8_t curX, curY;
  int8_t dirX, dirY;

  uint8_t numFoodDisplayed;

  uint32_t currentScore;
};

#endif
