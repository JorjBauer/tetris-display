#include <Arduino.h>

typedef unsigned char byte;

class RingPixels {
 public:
  RingPixels(int width, int length);
  ~RingPixels();

  void clear();

  bool isFull();
  bool hasData();
  int freeSpace();
  bool addLine(byte *l);
  byte *consumeLine();
  
 private:
  byte *buffer;
  int max;
  int width;
  int ptr;
  int fill;
};
