#include "snake.h"

#ifdef UNIX
#include <stdlib.h>
#include <ncurses.h>
#include <string.h> // for memcpy
#else
#include <Arduino.h>
#endif

#define SNAKE 'S'
#define FOOD '.'


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

Snake::Snake()
{
  snakeBlockList = NULL;
}

Snake::~Snake()
{
  while (snakeBlockList) {
    offsetList *o = snakeBlockList;
    snakeBlockList = snakeBlockList->next;
    delete o;
  }
}

void Snake::Init()
{
  while (snakeBlockList) {
    offsetList *o = snakeBlockList;
    snakeBlockList = snakeBlockList->next;
    delete o;
  }

  for (int x=0; x<XSIZE; x++) {
    for (int y=0; y<YSIZE; y++) {
      board[y][x] = 0;
    }
  }

  currentScore = 0;
  numFoodDisplayed = 0;
  currentLength = 3;
  curX = XSIZE/2;
  curY = YSIZE/2;
  dirX = 1;
  dirY = 0;

  AddSnakeBlock(curX, curY);
}

void Snake::AddSnakeBlock(int x, int y)
{
  offsetList *o = new offsetList;
  o->next = snakeBlockList;
  o->pos.x = x;
  o->pos.y = y;
  snakeBlockList = o;

  board[y][x] = SNAKE;
}

int Snake::BlockListSize()
{
  int i=0;

  offsetList *o = snakeBlockList;
  while (o) {
    i++;
    o = o->next;
  }
  return i;
}

void Snake::DeleteOldestSnakeBlock()
{
  offsetList *o = snakeBlockList;
  offsetList *p = NULL;
  while (o) {
    if (!o->next) {
      if (p) {
	p->next = NULL;
	board[o->pos.y][o->pos.x] = 0;
	delete o;
      }
      break;
    }
    p = o;
    o = o->next;
  }
}

int Snake::GetSquare(int x, int y)
{
  if (board[y][x]) {
    return board[y][x];
  }

  return ' ';
}

bool Snake::Step()
{
  curX = (curX + dirX) % (XSIZE);
  while (curX < 0) curX += XSIZE;
  curY = (curY + dirY) % (YSIZE);
  while (curY < 0) curY += YSIZE;

  if (board[curY][curX] == FOOD) {
    currentLength++;
    numFoodDisplayed--;
  }

  if (board[curY][curX] == SNAKE) {
    // game over!
    return false;
  }

  AddSnakeBlock(curX, curY);
  while (BlockListSize() > currentLength) {
    DeleteOldestSnakeBlock();
  }

  if (numFoodDisplayed < (currentLength/5)+1) {
    AddRandomFood();
  }

  return true;
}

void Snake::TurnLeft()
{
  if (dirX == 1) {
    dirX = 0;
    dirY = -1;
  }
  else if (dirY == -1) {
    dirY = 0;
    dirX = -1;
  }
  else if (dirX == -1) {
    dirX = 0;
    dirY = 1;
  }
  else {
    dirY = 0;
    dirX = 1;
  }
}

void Snake::TurnRight()
{
  if (dirX == 1) {
    dirX = 0;
    dirY = 1;
  } 
  else if (dirY == 1) {
    dirY = 0;
    dirX = -1;
  }
  else if (dirX == -1) {
    dirX = 0;
    dirY = -1;
  }
  else {
    dirY = 0;
    dirX = 1;
  }
}

uint32_t Snake::score()
{
  return currentLength;
}

void Snake::SetupTest()
{
}

void Snake::AddRandomFood()
{
  int8_t newX, newY;
  do {
    newX = RandomLessThan(XSIZE);
    newY = RandomLessThan(YSIZE);
  } while (board[newY][newX]);

  board[newY][newX] = FOOD;
  numFoodDisplayed++;

}
