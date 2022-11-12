#include <Arduino.h>
#include <stdint.h>

#define DELTA 0x9e3779b9
#define MX (((z>>5^y<<2) + (y>>3^z<<4)) ^ ((sum^y) + (key[(p&3)^e] ^ z)))

#define BLOCK_SIZE (128/8) // 128 bit (16 byte) blocks

void btea(uint32_t *v, int n, uint32_t const key[4]) {
  uint32_t y, z, sum;
  unsigned p, rounds, e;
  if (n > 1) {          /* Coding Part */
    rounds = 6 + 52/n;
    sum = 0;
    z = v[n-1];
    do {
      sum += DELTA;
      e = (sum >> 2) & 3;
      for (p=0; p<n-1; p++) {
        y = v[p+1]; 
        z = v[p] += MX;
      }
      y = v[0];
      z = v[n-1] += MX;
    } while (--rounds);
  } else if (n < -1) {  /* Decoding Part */
    n = -n;
    rounds = 6 + 52/n;
    sum = rounds*DELTA;
    y = v[0];
    do {
      e = (sum >> 2) & 3;
      for (p=n-1; p>0; p--) {
        z = v[p-1];
        y = v[p] -= MX;
      }
      z = v[n-1];
      y = v[0] -= MX;
      sum -= DELTA;
    } while (--rounds);
  }
}

int xxteaEncrypt(char *inputText, unsigned int inputTextLength, uint32_t const k[4])
{
  unsigned int numBlocks = (inputTextLength <= BLOCK_SIZE ? 1 : inputTextLength/BLOCK_SIZE);
  unsigned int offset, i;
  
  // Padding if necessary till a full block size
  if ((offset = inputTextLength % BLOCK_SIZE) != 0)
    memset(inputText+inputTextLength, 0x00, BLOCK_SIZE - offset);
  
  for (i=0; i<numBlocks;i++){
    btea((uint32_t *)inputText, BLOCK_SIZE/4,k);
    inputText+=BLOCK_SIZE;
  }
  
  return numBlocks;
}

int xxteaDecrypt(char *inputText, unsigned int inputTextLength, uint32_t const k[4])
{
  unsigned int numBlocks = (inputTextLength <= BLOCK_SIZE ? 1 : inputTextLength/BLOCK_SIZE), i;
  
  for (i=0; i<numBlocks;i++){
    btea((uint32_t *)inputText, BLOCK_SIZE/4*(-1),k);
    inputText+=BLOCK_SIZE;
  }
  
  return numBlocks;
}
