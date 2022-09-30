#include "RingPixels.h"

RingPixels::RingPixels(int width, int length)
{
  this->buffer = (byte *)malloc(length*width);
  this->max = length;
  this->width = width;
}

RingPixels::~RingPixels()
{
  free (this->buffer);
}

bool RingPixels::isFull()
{
  return (this->max == this->fill);
}

void RingPixels::clear()
{
  this->fill = 0;
}

bool RingPixels::hasData()
{
  return (this->fill != 0);
}

int RingPixels::freeSpace()
{
  return (this->max - this->fill);
}

bool RingPixels::addLine(byte *b)
{
  if (this->max == this->fill)
    return false;

  int idx = (this->ptr + this->fill) % this->max;
  for (int i=0; i<this->width; i++) {
    this->buffer[idx*this->width+i] = b[i];
  }

  this->fill++;
  return true;
}

byte *RingPixels::consumeLine()
{
  if (this->fill == 0)
    return 0;
  
  byte *ret = &this->buffer[this->ptr * this->width];
  this->fill--;
  this->ptr++;
  this->ptr %= this->max;
  return ret;
}
